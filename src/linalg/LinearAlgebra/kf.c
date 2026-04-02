#include "declareFunctions.h"
#include <stdbool.h>
#include <stddef.h>
/*
 * Linear kalman filter - Suitable for systems that requires numerical stability e.g
 * microcontrollers. Use multiple models or linearize the model before you uising Kalman Filter, if
 * you need nonlinear estimation A[row_a*row_a] B[row_a*column_b] C[row_c*row_a] u[columb_b]
 * y[row_c]
 * Q[row_a*row_a]
 * R[row_c*row_c]
 * xhat[row_a]
 * P[row_a*row_a]
 * Return true if state was estimated, else false
 */

bool kf(float A[], float B[], float C[], float u[], float y[], float Q[], float R[], float xhat[],
        float P[], size_t row_a, size_t row_c, size_t column_b) {

    /* Prediction */
    float dx[row_a];
    float Ax[row_a];
    float Bu[row_a];

    mulf(A, xhat, false, Ax, row_a, row_a, 1);
    mulf(B, u, false, Bu, row_a, column_b, 1);

    size_t i;

    for (i = 0; i < row_a; i++) {
        dx[i] = Ax[i] + Bu[i];
    }

    /* Compute covaraiance */
    const size_t row_a_row_a = row_a * row_a;
    float AT[row_a_row_a];
    memcpy(AT, A, row_a * row_a * sizeof(float));
    tranf(AT, row_a, row_a);
    float PAT[row_a_row_a];
    mulf(P, AT, false, PAT, row_a, row_a, row_a);
    float APAT[row_a_row_a];
    mulf(A, PAT, false, APAT, row_a, row_a, row_a);
    for (i = 0; i < row_a_row_a; i++) {
        P[i] = APAT[i] + Q[i];
    }

    /* Innovation covaraiance */
    float CT[row_c * row_a];
    memcpy(CT, C, row_c * row_a * sizeof(float));
    tranf(CT, row_c, row_a);
    float PCT[row_a * row_c];
    mulf(P, CT, false, PCT, row_a, row_a, row_c);
    float CPCT[row_c * row_c];
    mulf(C, PCT, false, CPCT, row_c, row_a, row_c);
    float S[row_c * row_c];
    for (i = 0; i < row_c * row_c; i++) {
        S[i] = CPCT[i] + R[i];
    }

    /* Find kalman gain */
    float K[row_a * row_c];
    invf(S, row_c);
    mulf(PCT, S, false, K, row_a, row_c, row_c);

    /* Update state */
    float Cx[row_c];
    mulf(C, dx, false, Cx, row_c, row_a, 1);
    float y_Cx[row_c];
    for (i = 0; i < row_c; i++) {
        y_Cx[i] = y[i] - Cx[i];
    }
    float Ky_Cx[row_a];
    mulf(K, y_Cx, false, Ky_Cx, row_a, row_c, 1);
    for (i = 0; i < row_a; i++) {
        xhat[i] = dx[i] + Ky_Cx[i];
    }

    /* Update covaraiance */
    float KC[row_a_row_a];
    mulf(K, C, false, KC, row_a, row_c, row_a);
    for (i = 0; i < row_a; i++) {
        KC[i * row_a] = 1.0f - KC[i * row_a];
    }
    float P_copy[row_a_row_a];
    memcpy(P_copy, P, row_a_row_a * sizeof(float));
    mulf(KC, P_copy, false, P, row_a, row_a, row_a);

    /* Return the status */
    return true;
}
