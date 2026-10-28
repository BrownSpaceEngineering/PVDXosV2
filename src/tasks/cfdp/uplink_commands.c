#include "uplink_commands.h"

#include "cfdp_task.h"
#include "cfdp_utils.h"
#include "globals.h"
#include "libraries/cmd_dispatcher/cmd_dispatcher.h"
#include "logging.h"
#include "task_list.h"

void uplink_commands_dispatch(cfdp_buff_header_t *buff, size_t n) {
    if (buff == NULL) {
        return;
    }

    static uplink_packet_t packet; // static, since it is too big for the CFDP task's stack
    if (uplink_packet_parse(&packet, buff->buff, n)) {
        warning("uplink: rejected malformed uplink of %lu bytes\n", n);
        return;
    }
    // TODO: push packet.timestamp to wherever we store this for telemetry.

    for (uint16_t i = 0; i < packet.n_cmds; i++) {
        const uplink_parsed_cmd_t *parsed = &packet.cmds[i];
        pvdx_task_t *target;
        operation_t op;

        switch (parsed->type) {
            case ENABLE_DISABLE_DEVICE:
                target = NULL; // TODO
                op = OPERATION_ENABLE_DISABLE_DEVICE;
                break;
            case SET_DEVICE_BROKEN_UNBROKEN:
                target = NULL; // TODO
                op = OPERATION_SET_DEVICE_BROKEN_UNBROKEN;
                break;
            case UPDATE_DISPLAY:
                target = p_display_task;
                op = OPERATION_DISPLAY_IMAGE;
                break;
            case DISABLE_TRANSMIT:
                target = p_task_manager_task;
                op = OPERATION_DISABLE_TRANSMIT;
                break;
            case SLEEP_TRANSMIT:
                target = p_task_manager_task;
                op = OPERATION_SLEEP_TRANSMIT;
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
                target = NULL; // camera
                op = OPERATION_SEND_PHOTO;
                break;
            case SET_KEPLER_COEFF:
                target = p_adcs_task;
                op = OPERATION_SET_KEPLER_COEFF;
                break;
            case SET_UNIX_TIME:
                target = p_adcs_task;
                op = OPERATION_SET_UNIX_TIME;
                break;
            case SET_ADCS_OP_MODE:
                target = p_adcs_task;
                op = OPERATION_SET_ADCS_OP_MODE;
                break;
            case ADCS_UPDATE_PARAMS:
                target = p_adcs_task;
                op = OPERATION_ADCS_UPDATE_PARAMS;
                break;
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
