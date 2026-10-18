/**
 * test_magnetorquer.c
 *
 * Tests the magnetorquer driver's voltage -> slow-decay compare value conversion and its resistance/current models.
 * Only exercises the pure conversion helpers, so it never energizes the coils.
 */

#include "tests/test_magnetorquer.h"

#include "drivers/magnetorquer/magnetorquer_driver.h"
#include "logging.h"
#include "tests/test.h"

#if defined(UNITTEST) && defined(TEST_MAGNETORQUER)
    #define MTQ_TEST_PERIOD 600U

static bool mtq_close(float a, float b, float tolerance) {
    float diff = a - b;
    return diff < tolerance && diff > -tolerance;
}

static void test_magnetorquer_compare_values(void) {
    uint32_t in1 = 0;
    uint32_t in2 = 0;

    // Zero volts brakes the coil: both inputs held high
    magnetorquer_compute_compare_values(0.0f, 8.0f, MTQ_TEST_PERIOD, &in1, &in2);
    PVDX_ASSERT_MSG(in1 == MTQ_TEST_PERIOD && in2 == MTQ_TEST_PERIOD, "mtq zero voltage brakes\n");

    // Forward: IN1 held high, IN2 low (drive) for duty * period
    magnetorquer_compute_compare_values(2.0f, 8.0f, MTQ_TEST_PERIOD, &in1, &in2);
    PVDX_ASSERT_MSG(in1 == MTQ_TEST_PERIOD && in2 == MTQ_TEST_PERIOD - 150U, "mtq 25% forward\n");

    // Reverse mirrors forward
    magnetorquer_compute_compare_values(-2.0f, 8.0f, MTQ_TEST_PERIOD, &in1, &in2);
    PVDX_ASSERT_MSG(in1 == MTQ_TEST_PERIOD - 150U && in2 == MTQ_TEST_PERIOD, "mtq 25% reverse\n");

    // Full scale and beyond saturate at 100% drive
    magnetorquer_compute_compare_values(8.0f, 8.0f, MTQ_TEST_PERIOD, &in1, &in2);
    PVDX_ASSERT_MSG(in1 == MTQ_TEST_PERIOD && in2 == 0U, "mtq full forward\n");
    magnetorquer_compute_compare_values(-20.0f, 8.0f, MTQ_TEST_PERIOD, &in1, &in2);
    PVDX_ASSERT_MSG(in1 == 0U && in2 == MTQ_TEST_PERIOD, "mtq saturated reverse\n");

    // An invalid supply voltage commands nothing
    magnetorquer_compute_compare_values(3.0f, 0.0f, MTQ_TEST_PERIOD, &in1, &in2);
    PVDX_ASSERT_MSG(in1 == MTQ_TEST_PERIOD && in2 == MTQ_TEST_PERIOD, "mtq zero supply brakes\n");

    PVDX_ASSERT_MSG(mtq_close(magnetorquer_clamp_voltage(9.0f, 8.0f), 8.0f, 1e-6f), "mtq clamp high\n");
    PVDX_ASSERT_MSG(mtq_close(magnetorquer_clamp_voltage(-9.0f, 8.0f), -8.0f, 1e-6f), "mtq clamp low\n");
    PVDX_ASSERT_MSG(mtq_close(magnetorquer_clamp_voltage(-3.0f, 8.0f), -3.0f, 1e-6f), "mtq clamp passthrough\n");
}

static void test_magnetorquer_models(void) {
    // At the coil reference temperature only the driver on-resistance moves
    float expected = MAGNETORQUER_X_COIL_RESISTANCE_OHM + MAGNETORQUER_DRIVER_RDS_ON_OHM +
        MAGNETORQUER_DRIVER_RDS_ON_TEMP_COEFF_OHM_PER_C *
            (MAGNETORQUER_COIL_REFERENCE_TEMP_C - MAGNETORQUER_DRIVER_RDS_ON_REFERENCE_TEMP_C);
    PVDX_ASSERT_MSG(mtq_close(magnetorquer_resistance(MAGNETORQUER_X, MAGNETORQUER_COIL_REFERENCE_TEMP_C), expected, 1e-4f),
                    "mtq reference resistance\n");

    // Resistance rises with temperature, so the guaranteed current falls
    PVDX_ASSERT_MSG(magnetorquer_resistance(MAGNETORQUER_Y, 80.0f) > magnetorquer_resistance(MAGNETORQUER_Y, -20.0f),
                    "mtq resistance increases with temperature\n");
    float_3d_t cold = magnetorquer_max_currents(5.0f, -20.0f);
    float_3d_t hot = magnetorquer_max_currents(5.0f, 80.0f);
    PVDX_ASSERT_MSG(hot.x < cold.x && hot.y < cold.y && hot.z < cold.z, "mtq max current decreases with temperature\n");
    PVDX_ASSERT_MSG(mtq_close(hot.z, 5.0f / magnetorquer_resistance(MAGNETORQUER_Z, 80.0f), 1e-4f) ||
                        mtq_close(hot.z, MAGNETORQUER_MAX_CURRENT_A, 1e-6f),
                    "mtq max current is V / R or the driver limit\n");

    // A huge supply is capped by the DRV8837C current rating
    float_3d_t capped = magnetorquer_max_currents(1000.0f, 20.0f);
    PVDX_ASSERT_MSG(mtq_close(capped.x, MAGNETORQUER_MAX_CURRENT_A, 1e-6f), "mtq max current capped by driver\n");
}

void test_magnetorquer(void) {
    test_log("----- testing magnetorquer -----\n");
    test_magnetorquer_compare_values();
    test_magnetorquer_models();
}
#endif // UNITTEST && TEST_MAGNETORQUER
