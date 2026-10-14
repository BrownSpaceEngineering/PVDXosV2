#include "uplink_commands.h"

#include "cfdp_task.h"
#include "cfdp_utils.h"
#include "globals.h"
#include "libraries/cmd_dispatcher/cmd_dispatcher.h"
#include "logging.h"
#include "task_list.h"

static inline uint32_t big_endian_to_uint32(const uint8_t arr[4]) {
    return ((uint32_t)arr[0] << 24) | ((uint32_t)arr[1] << 16) | ((uint32_t)arr[2] << 8) | ((uint32_t)arr[3]);
}

static inline uint32_t big_endian_to_uint16(const uint8_t arr[2]) {
    return ((uint32_t)arr[2] << 8) | ((uint32_t)arr[3]);
}

const char callsign[6] = {'P', 'V', 'D', 'X', 'G', 'S'}; // TODO: replace with actual callsign.

int parse_uplink_packet(cfdp_buff_header_t *buff, size_t n) {
    if (!buff) {
        return -1;
    }

    if (n < 16)
        return -2;

    uint8_t *raw = buff->buff;

    // Note: SPP Packet Header should be parsed/removed prior to data being forwared to CFDP, so this function assumes it is not present

    for (size_t i = 0; i < 5; i++) {
        if (raw[5 - i] != callsign[i]) {
            warning("Packet has unknown callsign!\n");
            return -3;
        }
    }

    size_t message_size = big_endian_to_uint32(raw + 6);
    size_t n_cmds = big_endian_to_uint16(raw + 10);
    size_t timestamp = big_endian_to_uint32(raw + 12);
    (void)timestamp; // TODO: push to wherever we store this for telemetry.

    // Note: this assumes message size does not include preamble
    if (n - 16 < message_size) {
        warning("unable to parse entire uplink packet, data size: %lu less than promised %lu", (n - 16), message_size);
        return -4;
    }

    size_t remaining_size = message_size;
    size_t remaining_cmds = n_cmds;

    size_t offset = 16;

    while (remaining_size > 0 && remaining_cmds > 0) {
        int ret = parse_uplink_cmd(buff, offset, remaining_size);
        if (ret < 0) {
            warning("unable to parse commannd with offset: %d\n", offset);
            return ret;
        }

        if (ret < 6) {
            warning("should never parse command smaller than command metadata!\n");
            return -5;
        }

        offset += (size_t)ret;
        remaining_size -= ((size_t)ret - 6);
        remaining_cmds--;
    }
    return 0;
}

int parse_uplink_cmd(cfdp_buff_header_t *buff, size_t offset, size_t n) {
    if (!buff)
        return -1;

    if (n < 6)
        return -2;

    uint8_t *raw = buff->buff + offset;

    uplink_cmd_type_t cmd_type = (uplink_cmd_type_t)big_endian_to_uint16(raw);
    size_t cmd_sz = big_endian_to_uint32(raw + 2);

    if (n - 6 < cmd_sz)
        return -3;

    pvdx_task_t *target;
    operation_t op;
    status_t result = SUCCESS;

    switch (cmd_type) {
        case (ENABLE_DISABLE_DEVICE):
            target = NULL; // TODO
            op = OPERATION_ENABLE_DISABLE_DEVICE;
            break;
        case (SET_DEVICE_BROKEN_UNBROKEN):
            target = NULL; // TODO
            op = OPERATION_SET_DEVICE_BROKEN_UNBROKEN;
            break;
        case (UPDATE_DISPLAY):
            target = p_display_task;
            op = OPERATION_DISPLAY_IMAGE;
            break;
        case (DISABLE_TRANSMIT):
            target = p_task_manager_task;
            op = OPERATION_DISABLE_TRANSMIT;
            break;
        case (SLEEP_TRANSMIT):
            target = p_task_manager_task;
            op = OPERATION_SLEEP_TRANSMIT;
            break;
        case (REBOOT):
            target = p_task_manager_task;
            op = OPERATION_REBOOT;
            break;
        case (TAKE_PHOTO):
            target = NULL; // camera
            op = OPERATION_TAKE_PHOTO;
            break;
        case (SEND_PHOTO):
            target = NULL; // camera;
            op = OPERATION_SEND_PHOTO;
            break;
        case (SET_KEPLER_COEFF):
            target = p_adcs_task;
            op = OPERATION_SET_KEPLER_COEFF;
            break;
        case (SET_UNIX_TIME):
            target = p_adcs_task;
            op = OPERATION_SET_UNIX_TIME;
            break;
        case (SET_ADCS_OP_MODE):
            target = p_adcs_task;
            op = OPERATION_SET_ADCS_OP_MODE;
            break;
        case (ADCS_UPDATE_PARAMS):
            target = p_adcs_task;
            op = OPERATION_ADCS_UPDATE_PARAMS;
            break;
        case (SET_PWR_MODE):
            target = p_task_manager_task;
            op = OPERATION_SET_POWER_MODE;
            break;
        default:
            return -4;
    }

    command_t cmd = (command_t){.data = {(uplink_cmd_t){.header = buff, .buff_offset = offset + 6}},
                                .data_type = CMD_DATA_UPLINK,
                                .operation = op,
                                .result = result,
                                .target = target};

    enqueue_command(&cmd);
    return 6 + cmd_sz;
}
