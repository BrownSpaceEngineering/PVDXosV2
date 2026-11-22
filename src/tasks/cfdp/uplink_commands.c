#include "uplink_commands.h"

#include "cfdp_task.h"
#include "cfdp_utils.h"
#include "drivers/mram/mram_map.h"
#include "globals.h"
#include "libraries/cmd_dispatcher/cmd_dispatcher.h"
#include "logging.h"
#include "state_machine.h"
#include "task_list.h"
#include "tasks/display/display_task.h"
#include "tasks/perovskite/perovskite_task.h"
#include "tasks/uhf_radio/uhf_radio_task.h"
#include "telemetry/telemetry.h"

/*
 * Commands for ADCS and CFDP are sent straight away. Every other command is for a task that doesn't run during a pass,
 * so it's turned into what it should change and kept in `pending` until the pass ends: uplink_commands_pass_exit(), run
 * by the CFDP task as it's suspended on leaving the pass state, sends it. Commands take what they need from the uplink as
 * they're parsed (the UPDATE_DISPLAY bitmap goes to MRAM), so nothing points into cfdp_file_buff once this returns.
 */
static struct {
    input_update_data_t inputs; // State machine inputs to update. input_update_bitmask lists the ones set this pass
    bool step_count_set;        // TAKE_IV_CURVE
    uint32_t step_count;
    bool transmit_set; // TRANSMIT_ON_OFF
    bool transmit;
    bool reboot; // REBOOT
} pending;

// The inputs sent at the last pass exit. A copy, since the task manager reads it after the command is sent (see
// get_state_input_update_command()), while `pending` starts filling again in the next pass
static input_update_data_t pass_exit_inputs;

/*
 * SEND_PHOTO and SEND_IV_CURVE are run here, in the CFDP task, instead of being sent to the camera or perovskite
 * task: those are only enabled in some states, while the CFDP task always is (when in a pass, and thus recieving a command). Each checks
 * that there's data in MRAM to send, then queues a put to the CFDP task, which starts it once the transaction in progress (e.g. this
 * uplink) is done.
 */

// Queues a put of a file, if it's in MRAM
static void uplink_queue_put(cfdp_file_t file, uint8_t index, radio_stream_t stream) {
    cfdp_put_t put = {.file = (uint8_t)file, .index = index, .stream = (uint8_t)stream};
    mram_region_t region;
    uint32_t offset;
    uint32_t size;
    if (cfdp_find_file(&put, &region, &offset, &size) != SUCCESS) {
        info("uplink: no file %u (index %u) in MRAM to send\n", file, index);
        return;
    }

    command_t cmd = get_cfdp_put_command(file, index, stream);
    status_t status = enqueue_command(&cmd);
    if (status != SUCCESS) {
        warning("uplink: failed to queue put of file %u (status %d)\n", file, status);
        return;
    }
    info("uplink: queued put of file %u (index %u), %lu bytes\n", file, index, size);
}

static void uplink_send_photo(int32_t photo_count) {
    if (photo_count <= 0) {
        warning("uplink: asked to send %ld pictures, sending none\n", photo_count);
        return;
    }
    // TODO: MRAM only holds the last picture capture_mram() took, so that's all that's sent, however many are asked for
    if (photo_count > 1) {
        warning("uplink: asked to send %ld pictures, but only the last one is kept\n", photo_count);
    }
    uplink_queue_put(CFDP_FILE_IMAGE, 0, RADIO_STREAM_IMAGE);
}

static void uplink_send_iv_curve(void) {
    // TODO: only the newest sweep is sent. Older sweeps still in MRAM that were never sent need their sequence numbers
    // tracked (see perovskite_send_telemetry())
    uint8_t slot;
    if (perovskite_newest_sweep_slot(&slot) != SUCCESS) {
        info("uplink: no IV curve in MRAM to send\n");
        return;
    }
    uplink_queue_put(CFDP_FILE_PEROVSKITE, slot, RADIO_STREAM_PEROVSKITE);
}

_Static_assert(UPLINK_MRAM_KEPLER_OFFSET + MRAM_RECORD_OVERHEAD + sizeof(float) * UPLINK_KEPLER_COEFFICIENTS <=
                   UPLINK_MRAM_ADCS_PARAMS_OFFSET,
               "Kepler record overlaps the ADCS parameters record");
_Static_assert(UPLINK_MRAM_ADCS_PARAMS_OFFSET + MRAM_RECORD_OVERHEAD + sizeof(uplink_adcs_parameters_t) <= MRAM_ADCS_UPLINK_SIZE,
               "ADCS parameters record must fit MRAM_REGION_ADCS_UPLINK");

// Sends a command whose data fits in the command itself, so it doesn't hold the uplink buffer
static void uplink_send_direct(command_t cmd) {
    status_t status = enqueue_command(&cmd);
    if (status != SUCCESS) {
        warning("uplink: operation %d not sent (status %d)\n", cmd.operation, status);
    }
}

// Saves command data too big for a command_t to MRAM_REGION_ADCS_UPLINK, then tells ADCS with `op` to read it
static void uplink_save_for_adcs(operation_t op, uint32_t offset, const void *data, uint32_t size) {
    if (!mram_record_save(MRAM_REGION_ADCS_UPLINK, offset, data, size)) {
        warning("uplink: failed to save %lu bytes of ADCS data to MRAM offset 0x%lx\n", size, offset);
        return;
    }
    uplink_send_direct((command_t){.target = p_adcs_task, .operation = op, .data_type = CMD_DATA_NONE, .result = PROCESSING});
}

void uplink_commands_dispatch(const uint8_t *data, size_t n) {
    if (data == NULL) {
        return;
    }

    static uplink_packet_t packet; // static, since it is too big for the CFDP task's stack
    if (uplink_packet_parse(&packet, data, n)) {
        warning("uplink: rejected malformed uplink of %lu bytes\n", n);
        return;
    }
    telemetry_update_command(packet.timestamp);

    for (uint16_t i = 0; i < packet.n_cmds; i++) {
        const uplink_parsed_cmd_t *parsed = &packet.cmds[i];

        switch (parsed->type) {
            /* ---- Sent straight away: ADCS and CFDP ---- */
            case SEND_IV_CURVE:
                uplink_send_iv_curve();
                break;
            case SEND_PHOTO:
                uplink_send_photo(parsed->args.photo_count);
                break;
            case SET_KEPLER_COEFF:
                uplink_save_for_adcs(OPERATION_SET_KEPLER_COEFF, UPLINK_MRAM_KEPLER_OFFSET, parsed->args.kepler_coefficients,
                                     sizeof(parsed->args.kepler_coefficients));
                break;
            case SET_UNIX_TIME:
                uplink_send_direct((command_t){.target = p_adcs_task,
                                               .operation = OPERATION_ADCS_SET_UTC,
                                               .data = {.unix_time = parsed->args.unix_time},
                                               .data_type = CMD_DATA_UNIX_TIME,
                                               .result = PROCESSING});
                break;
            case SET_ADCS_OP_MODE:
                uplink_send_direct((command_t){.target = p_adcs_task,
                                               .operation = (parsed->args.adcs_op_mode == TEL_ADCS_OPMODE_DETUMBLE_ONLY)
                                                   ? OPERATION_ADCS_DETUMBLE_ONLY
                                                   : OPERATION_ADCS_DO_POINT,
                                               .data_type = CMD_DATA_NONE,
                                               .result = PROCESSING});
                break;
            case ADCS_UPDATE_PARAMS:
                uplink_save_for_adcs(OPERATION_ADCS_UPDATE_PARAMS, UPLINK_MRAM_ADCS_PARAMS_OFFSET, &parsed->args.adcs_parameters,
                                     sizeof(parsed->args.adcs_parameters));
                break;

            /* ---- Kept until the pass ends ---- */
            case UPDATE_DISPLAY:
                // Saved now, while the uplink is still in cfdp_file_buff. Once the pass ends the student picture state
                // shows it and photographs it (when in eclipse and no photo is waiting for downlink)
                if (display_store_image((const color_t *)parsed->data) == SUCCESS) {
                    pending.inputs.input_update_bitmask |= INPUT_UPDATE_BIT(IMAGE_READY_DISPLAY_UPDATE);
                    pending.inputs.new_values.image_ready_to_display = true;
                    info("uplink: display image saved, shown once the pass ends\n");
                }
                break;
            case TAKE_PHOTO:
                // STATE_STUDENT_CAPTURE_OVERRIDE takes the student picture at this time, whatever else is going on
                pending.inputs.input_update_bitmask |= INPUT_UPDATE_BIT(STUDENT_CAPTURE_OVERRIDE_UPDATE);
                pending.inputs.new_values.student_capture_override = true;
                pending.inputs.new_values.student_capture_time = parsed->args.photo_timestamp;
                info("uplink: photo override at %lu once the pass ends\n", (unsigned long)parsed->args.photo_timestamp);
                break;
            case TAKE_IV_CURVE:
                // Only sets the sampling resolution of the following sweeps; it doesn't start one
                pending.step_count_set = true;
                pending.step_count = parsed->args.iv_sampling_resolution;
                break;
            case TRANSMIT_ON_OFF:
                // Backup for PUSP_NOTIF_ENABLE_TX / PUSP_NOTIF_DISABLE_TX. TODO(temp): 1 = on, 0 = off, until the sheet
                // gives this byte a meaning
                if (parsed->args.transmit > 1) {
                    warning("uplink: TRANSMIT_ON_OFF with value %u ignored, expected 0 or 1\n", parsed->args.transmit);
                    break;
                }
                pending.transmit_set = true;
                pending.transmit = parsed->args.transmit == 1;
                break;
            case REBOOT:
                if (parsed->args.reboot != 0) {
                    pending.reboot = true;
                    info("uplink: reboot once the pass ends\n");
                }
                break;
            case ENABLE_DISABLE_DEVICE:
                // TODO: to be done through the device mock system
            case SET_PWR_MODE:
                // TODO: power mode will be set through PUSP
                info("uplink: command type %u not implemented yet, ignored\n", parsed->type);
                break;
            default:
                break; // uplink_packet_parse already rejects unknown types
        }
    }
}

void uplink_commands_pass_exit(void) {
    if (pending.inputs.input_update_bitmask != 0) {
        pass_exit_inputs = pending.inputs;
        command_t cmd = get_state_input_update_command(&pass_exit_inputs);
        const status_t status = enqueue_command(&cmd);
        if (status != SUCCESS) {
            warning("uplink: failed to send the pass's state machine updates (status %d)\n", status);
        }
    }
    if (pending.step_count_set) {
        perovskite_set_step_count(pending.step_count);
    }
    if (pending.transmit_set) {
        uhf_request_transmit(pending.transmit);
    }
    // Last, since the task manager stops once it gets it
    if (pending.reboot) {
        uplink_send_direct(
            (command_t){.target = p_task_manager_task, .operation = OPERATION_REBOOT, .data_type = CMD_DATA_NONE, .result = PROCESSING});
    }
    pending = (typeof(pending)){0};
}
