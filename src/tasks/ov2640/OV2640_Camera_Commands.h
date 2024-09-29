/*
 * OV2640_Camera_Commands.c
 * Created: 12/29/21
 * Author: Brown Space Engineering
 */

#ifndef PVDX_ARDUCAM_DRIVER_OV2640_CAMERA_COMMANDS_H
#define PVDX_ARDUCAM_DRIVER_OV2640_CAMERA_COMMANDS_H

#include <atmel_start.h>

// Chip select (alternately "CS," "SS," or "slave select") pin
#define OV2640_CS GPIO(GPIO_PORTA, 5)

// Possible camera resolutions
// TODO: decide which of these to keep
#define OV2640_160x120 0
#define OV2640_320x240 1
#define OV2640_640x480 2
#define OV2640_1024x768 3

// Default resolution for the camera
#define OV2640_DEFAULT_RESOLUTION OV2640_320x240

// TODO: all of these need status codes
void OV2640_init(void);
uint32_t OV2640_capture(uint8_t *buffer);
void OV2640_set_resolution(uint8_t resolution);
void OV2640_set_light_mode(uint8_t mode);
void OV2640_set_saturation(int8_t saturation);
void OV2640_set_brightness(int8_t brightness);
void OV2640_set_contrast(int8_t contrast);
void OV2640_set_exposure(int8_t exposure);




#endif //PVDX_ARDUCAM_DRIVER_OV2640_CAMERA_COMMANDS_H
