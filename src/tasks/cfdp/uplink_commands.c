#include "uplink_commands.h"

#include "cfdp_task.h"
#include "cfdp_utils.h"
#include "drivers/mram/mram_map.h"
#include "globals.h"
#include "libraries/cmd_dispatcher/cmd_dispatcher.h"
#include "logging.h"
#include "task_list.h"
#include "tasks/perovskite/perovskite_task.h"
#include "telemetry/telemetry.h"

/*
 * SEND_PHOTO and SEND_IV_CURVE are run here, in the CFDP task, instead of being sent to the camera or perovskite
 * task: those are only enabled in some states, while the CFDP task always is. Each checks that there's data in MRAM to
 * send, then queues a put to the CFDP task, which starts it once the transaction in progress (e.g. this uplink) is done.
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

void uplink_commands_dispatch(cfdp_buff_header_t *buff, size_t n) {
    if (buff == NULL) {
        return;
    }

    static uplink_packet_t packet; // static, since it is too big for the CFDP task's stack
    if (uplink_packet_parse(&packet, buff->buff, n)) {
        warning("uplink: rejected malformed uplink of %lu bytes\n", n);
        return;
    }
    telemetry_update_command(packet.timestamp);

    for (uint16_t i = 0; i < packet.n_cmds; i++) {
        const uplink_parsed_cmd_t *parsed = &packet.cmds[i];
        pvdx_task_t *target;
        operation_t op;

        switch (parsed->type) {
            case ENABLE_DISABLE_DEVICE:
                // TODO (uplink sheet): add device enabled/disabled to the state machine inputs, and send this there
                target = NULL;
                op = OPERATION_ENABLE_DISABLE_DEVICE;
                break;
            case UPDATE_DISPLAY:
                target = p_display_task;
                op = OPERATION_DISPLAY_IMAGE;
                break;
            case TAKE_IV_CURVE:
                // TODO: perovskite task. OPERATION_PEROVSKITE_SWEEP takes no data and always sweeps
                // PEROVSKITE_STEP_COUNT steps, so it can't use the sampling resolution yet
                target = NULL;
                op = OPERATION_PEROVSKITE_SWEEP;
                break;
            case SEND_IV_CURVE:
                uplink_send_iv_curve();
                continue; // run here, so the command doesn't need the buffer
            case TRANSMIT_ON_OFF:
                // TODO (uplink sheet): "Done through PUSP notifications?" The UHF task already turns transmit on/off
                // from PUSP_NOTIF_DISABLE_TX/PUSP_NOTIF_ENABLE_TX
                target = p_task_manager_task;
                op = OPERATION_TRANSMIT_ON_OFF;
                break;
            case REBOOT:
                target = p_task_manager_task;
                op = OPERATION_REBOOT;
                break;
            case TAKE_PHOTO:
                target = NULL; // camera
                op = OPERATION_TAKE_PHOTO;
                break;
            case SEND_PHOTO:
                uplink_send_photo(parsed->args.photo_count);
                continue; // run here, so the command doesn't need the buffer
            case SET_KEPLER_COEFF:
                uplink_save_for_adcs(OPERATION_SET_KEPLER_COEFF, UPLINK_MRAM_KEPLER_OFFSET, parsed->args.kepler_coefficients,
                                     sizeof(parsed->args.kepler_coefficients));
                continue;
            case SET_UNIX_TIME:
                uplink_send_direct((command_t){.target = p_adcs_task,
                                               .operation = OPERATION_ADCS_SET_UTC,
                                               .data = {.unix_time = parsed->args.unix_time},
                                               .data_type = CMD_DATA_UNIX_TIME,
                                               .result = PROCESSING});
                continue;
            case SET_ADCS_OP_MODE:
                uplink_send_direct((command_t){.target = p_adcs_task,
                                               .operation = (parsed->args.adcs_op_mode == TEL_ADCS_OPMODE_DETUMBLE_ONLY)
                                                   ? OPERATION_ADCS_DETUMBLE_ONLY
                                                   : OPERATION_ADCS_DO_POINT,
                                               .data_type = CMD_DATA_NONE,
                                               .result = PROCESSING});
                continue;
            case ADCS_UPDATE_PARAMS:
                uplink_save_for_adcs(OPERATION_ADCS_UPDATE_PARAMS, UPLINK_MRAM_ADCS_PARAMS_OFFSET, &parsed->args.adcs_parameters,
                                     sizeof(parsed->args.adcs_parameters));
                continue;
            case SET_PWR_MODE:
                target = p_task_manager_task;
                op = OPERATION_SET_POWER_MODE;
                break;
            default:
                continue; // uplink_packet_parse already rejects unknown types
        }

        command_t cmd = (command_t){
            .data = {.uplink_cmd = (uplink_cmd_t){.header = buff, .buff_offset = (size_t)(parsed->data - (const uint8_t *)buff->buff)}},
            .data_type = CMD_DATA_UPLINK,
            .operation = op,
            .result = SUCCESS,
            .target = target};

        // The command keeps the buffer alive until its task is done with it
        // TODO (uplink sheet): ensure nice error handling in all tasks. A command that's dropped or fails is only
        // logged here or by its task; nothing tells the ground
        cfdp_add_buff_reference(buff);
        if (enqueue_command(&cmd) != SUCCESS) {
            debug("uplink: command %u (type %u) not sent, its task is missing or disabled\n", i, parsed->type);
            cfdp_release_buff(buff);
        }
    }
}

void uplink_cmd_release(const uplink_cmd_t *uplink_cmd) {
    if (uplink_cmd != NULL && uplink_cmd->header != NULL) {
        cfdp_release_buff(uplink_cmd->header);
    }
}
