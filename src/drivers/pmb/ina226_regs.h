#ifndef INA226_REGS_H
#define INA226_REGS_H

// INA226 Registers (INA226 pg. 22)
#define INA226_CONFIG 0x00U          /* R/W  Configuration          (POR 0x4127) */
#define INA226_SHUNT_VOLTAGE 0x01U   /* R    Shunt voltage, 2.5 uV/LSB, signed   */
#define INA226_BUS_VOLTAGE 0x02U     /* R    Bus voltage, 1.25 mV/LSB            */
#define INA226_POWER 0x03U           /* R    Power, 25 x Current_LSB             */
#define INA226_CURRENT 0x04U         /* R    Current, Current_LSB, signed        */
#define INA226_CALIBRATION 0x05U     /* R/W  Calibration (15-bit, POR 0x0000)    */
#define INA226_MASK_ENABLE 0x06U     /* R/W  Alert config / flags (read clears)  */
#define INA226_ALERT_LIMIT 0x07U     /* R/W  Alert limit                          */
#define INA226_MANUFACTURER_ID 0xFEU /* R   Manufacturer ID (0x5449)            */
#define INA226_DIE_ID 0xFFU          /* R    Die ID (0x2260 or 0x2261)           */

// Power-on reset values (INA226 pg. 22)
#define INA226_CONFIG_POR 0x4127U
#define INA226_MFG_ID 0x5449U /* "TI" */
#define INA226_DIE_ID_VALUE 0x2260U

// Configuration register fields (INA226 pg. 23)
#define INA226_CONFIG_RST (1U << 15)        /* Self-clearing; resets all registers to POR */
#define INA226_CONFIG_FIXED (1U << 14)      /* Bits 14:12 always read back as 0b100      */
#define INA226_CONFIG_AVG_SHIFT 9U          /* Averaging mode, 3 bits                     */
#define INA226_CONFIG_VBUSCT_SHIFT 6U       /* Bus voltage conversion time, 3 bits        */
#define INA226_CONFIG_VSHCT_SHIFT 3U        /* Shunt voltage conversion time, 3 bits      */
#define INA226_CONFIG_MODE_SHIFT 0U         /* Operating mode, 3 bits                     */
#define INA226_CONFIG_WRITABLE_MASK 0x0FFFU /* AVG | VBUSCT | VSHCT | MODE               */

// AVG field values: number of samples averaged
#define INA226_AVG_1 0x0U
#define INA226_AVG_4 0x1U
#define INA226_AVG_16 0x2U
#define INA226_AVG_64 0x3U
#define INA226_AVG_128 0x4U
#define INA226_AVG_256 0x5U
#define INA226_AVG_512 0x6U
#define INA226_AVG_1024 0x7U

// VBUSCT / VSHCT field values: conversion time
#define INA226_CT_140US 0x0U
#define INA226_CT_204US 0x1U
#define INA226_CT_332US 0x2U
#define INA226_CT_588US 0x3U
#define INA226_CT_1100US 0x4U
#define INA226_CT_2116US 0x5U
#define INA226_CT_4156US 0x6U
#define INA226_CT_8244US 0x7U

// MODE field values
#define INA226_MODE_POWER_DOWN 0x0U
#define INA226_MODE_SHUNT_TRIGGERED 0x1U
#define INA226_MODE_BUS_TRIGGERED 0x2U
#define INA226_MODE_SHUNT_BUS_TRIGGERED 0x3U
#define INA226_MODE_SHUNT_CONTINUOUS 0x5U
#define INA226_MODE_BUS_CONTINUOUS 0x6U
#define INA226_MODE_SHUNT_BUS_CONTINUOUS 0x7U

#define INA226_CONFIG_VALUE(avg, vbusct, vshct, mode)                                                                                      \
    ((uint16_t)(INA226_CONFIG_FIXED | ((avg) << INA226_CONFIG_AVG_SHIFT) | ((vbusct) << INA226_CONFIG_VBUSCT_SHIFT) |                      \
                ((vshct) << INA226_CONFIG_VSHCT_SHIFT) | ((mode) << INA226_CONFIG_MODE_SHIFT)))

// Calibration register is 15 bits; bit 15 is reserved and reads 0 (INA226 pg. 25)
#define INA226_CALIBRATION_MASK 0x7FFFU

// Mask/Enable register fields (INA226 pg. 25)
#define INA226_ME_CVRF (1U << 3) /* Conversion ready flag; cleared on Mask/Enable read or Config write */
#define INA226_ME_OVF (1U << 2)  /* Math overflow flag                                                */

// Fixed register LSBs (INA226 pg. 15-16)
#define INA226_SHUNT_LSB_NV 2500  /* 2.5 uV  */
#define INA226_BUS_LSB_UV 1250    /* 1.25 mV */
#define INA226_POWER_LSB_RATIO 25 /* Power_LSB = 25 * Current_LSB */

#endif // INA226_REGS_H
