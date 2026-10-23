#ifndef FUEL_GAUGE_BENCH_CONFIG_H
#define FUEL_GAUGE_BENCH_CONFIG_H

// Force-included only by the explicit SERCOM7 bench build; never a production pin fix.
#if !defined(DEVBUILD) || defined(RELEASE) || defined(UNITTEST)
#error "SERCOM7 mux workaround is only supported in the development bench build"
#endif
#if !FG_SERCOM7_BENCH || (!FG_MUX_ONLY_TEST && !FG_MUX_GAUGE_TEST)
#error "SERCOM7 mux workaround requires an explicit mux bench test"
#endif
#if FG_MUX_GAUGE_TEST && (!FG_USE_MUX || FG_TEST_MUX_CHANNEL < 0 || FG_TEST_MUX_CHANNEL > 3)
#error "Gauge bench test requires mux support and a channel from 0 to 3"
#endif

#include <peripheral_clk_config.h>

#if CONF_GCLK_SERCOM7_CORE_FREQUENCY != CONF_GCLK_SERCOM6_CORE_FREQUENCY
#error "Recalculate SERCOM7 I2C baud rate: SERCOM6 and SERCOM7 clocks differ"
#endif

// Add an I2C configuration to ASF's SERCOM7 table without editing generated files.
// Reuse the known 100 kHz SERCOM6 timing with the equal-frequency SERCOM7 clock.
#define CONF_SERCOM_7_I2CM_ENABLE 1
#define CONF_SERCOM_7_I2CM_RUNSTDBY CONF_SERCOM_6_I2CM_RUNSTDBY
#define CONF_SERCOM_7_I2CM_SPEED CONF_SERCOM_6_I2CM_SPEED
#define CONF_SERCOM_7_I2CM_MEXTTOEN CONF_SERCOM_6_I2CM_MEXTTOEN
#define CONF_SERCOM_7_I2CM_SEXTTOEN CONF_SERCOM_6_I2CM_SEXTTOEN
#define CONF_SERCOM_7_I2CM_INACTOUT CONF_SERCOM_6_I2CM_INACTOUT
#define CONF_SERCOM_7_I2CM_LOWTOUT CONF_SERCOM_6_I2CM_LOWTOUT
#define CONF_SERCOM_7_I2CM_SDAHOLD CONF_SERCOM_6_I2CM_SDAHOLD
#define CONF_SERCOM_7_I2CM_BAUD_RATE CONF_SERCOM_6_I2CM_BAUD_RATE
#define CONF_SERCOM_7_I2CM_DEBUG_STOP_MODE CONF_SERCOM_6_I2CM_DEBUG_STOP_MODE
#define CONF_SERCOM_7_I2CM_TRISE CONF_SERCOM_6_I2CM_TRISE

#endif
