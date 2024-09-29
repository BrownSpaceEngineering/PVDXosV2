/*
 * OV2640_sccb_bus.h
 * Contains functions for communicating with the OV2640's Serial Camera Control
 * Bus (SCCB) over I2C.
 *
 * Created: 12/30/21
 * Author: Brown Space Engineering
 */
#ifndef PVDX_ARDUCAM_DRIVER_OV2640_SCCB_H
#define PVDX_ARDUCAM_DRIVER_OV2640_SCCB_H

#include <atmel_start.h>
#include "OV2640_regs.h"

// TODO: all of these need status codes
void OV2640_sccb_start_tx(void);
void OV2640_sccb_stop_tx(void);
void OV2640_sccb_send_noack(void);
void OV2640_sccb_send_ack(void);
void OV2640_sccb_read_8bit_reg(uint8_t reg_id, unsigned char *reg_data_buf);
void OV2640_sccb_read_16bit_reg(uint16_t reg_id, unsigned char *reg_data_buf);
void OV2640_sccb_write_8bit_reg(uint8_t reg_id, uint8_t reg_data);
void OV2640_sccb_write_16bit_reg(uint16_t reg_id, uint8_t reg_data);
void OV2640_sccb_write_8bit_reg_array(const struct sensor_reg* reglist);

#endif //PVDX_ARDUCAM_DRIVER_OV2640_SCCB_H
