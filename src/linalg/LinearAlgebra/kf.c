#include <stdbool.h>
#include <stddef.h>
#include "declareFunctions.h"
/*
 * Linear kalman filter - Suitable for systems that requires numerical stability e.g microcontrollers. 
 * Use multiple models or linearize the model before you uising Kalman Filter, if you need nonlinear estimation
 * A[row_a*row_a]
 * B[row_a*column_b]
 * C[row_c*row_a]
 * u[columb_b]
 * y[row_c]
 * Q[row_a*row_a]
 * R[row_c*row_c]
 * xhat[row_a]
 * P[row_a*row_a]
 * Return true if state was estimated, else false
 */

bool kf(
    const float A[], 
    const float B[], 
    const float C[], 
    const float u[], 
    const float y[], 
    const float Q[], 
    const float R[], 
    float xhat[], 
    float P[], 
    const size_t row_a, 
    const size_t row_c, 
    const size_t column_b) {

	/* Prediction */
	// float* dx = (float*)malloc(row_a * sizeof(float));
	// float* Ax = (float*)malloc(row_a * sizeof(float));
	// float* Bu = (float*)malloc(row_a * sizeof(float));
    float dx[row_a]; 
    float Ax[row_a]; 
    float Bu[row_a]; 

	mul(A, xhat, false, Ax, row_a, row_a, 1);
	mul(B, u, false, Bu, row_a, column_b, 1);

	size_t i;

	for (i = 0; i < row_a; i++) {
		dx[i] = Ax[i] + Bu[i];
	}

	/* Compute covaraiance */
	const size_t row_a_row_a = row_a * row_a;
	float* AT = (float*)malloc(row_a_row_a * sizeof(float));
	memcpy(AT, A, row_a * row_a * sizeof(float));
	tran(AT, row_a, row_a);
	float* PAT = (float*)malloc(row_a_row_a * sizeof(float));
	mul(P, AT, PAT, row_a, row_a, row_a);
	float* APAT = (float*)malloc(row_a_row_a * sizeof(float));
	mul(A, PAT, APAT, row_a, row_a, row_a);
	for (i = 0; i < row_a_row_a; i++) {
		P[i] = APAT[i] + Q[i];
	}

	/* Innovation covaraiance */
	float* CT = (float*)malloc(row_c * row_a * sizeof(float));
	memcpy(CT, C, row_c * row_a * sizeof(float));
	tran(CT, row_c, row_a);
	float* PCT = (float*)malloc(row_a * row_c * sizeof(float));
	mul(P, CT, PCT, row_a, row_a, row_c);
	float* CPCT = (float*)malloc(row_c * row_c * sizeof(float));
	mul(C, PCT, CPCT, row_c, row_a, row_c);
	float* S = (float*)malloc(row_c * row_c * sizeof(float));
	for (i = 0; i < row_c * row_c; i++) {
		S[i] = CPCT[i] + R[i];
	}

	/* Find kalman gain */
	float* K = (float*)malloc(row_a * row_c * sizeof(float));
	const bool status = inv(S, row_c);
	mul(PCT, S, K, row_a, row_c, row_c);

	/* Update state */
	float* Cx = (float*)malloc(row_c * sizeof(float));
	mul(C, dx, Cx, row_c, row_a, 1);
	float* y_Cx = (float*)malloc(row_c * sizeof(float));
	for (i = 0; i < row_c; i++) {
		y_Cx[i] = y[i] - Cx[i];
	}
	float* Ky_Cx = (float*)malloc(row_a * sizeof(float));
	mul(K, y_Cx, Ky_Cx, row_a, row_c, 1);
	for (i = 0; i < row_a; i++) {
		xhat[i] = dx[i] + Ky_Cx[i];
	}

	/* Update covaraiance */
	float* KC = (float*)malloc(row_a_row_a * sizeof(float));
	mul(K, C, KC, row_a, row_c, row_a);
	for (i = 0; i < row_a; i++) {
		KC[i*row_a] = 1.0f - KC[i*row_a];
	}
	float* P_copy = (float*)malloc(row_a_row_a * sizeof(float));
	memcpy(P_copy, P, row_a_row_a * sizeof(float));
	mul(KC, P_copy, P, row_a, row_a, row_a);

	/* Free */
	free(dx);
	free(Ax);
	free(Bu);
	free(AT);
	free(PAT);
	free(APAT);
	free(CT);
	free(PCT);
	free(CPCT);
	free(S);
	free(K);
	free(Cx);
	free(y_Cx);
	free(Ky_Cx);
	free(KC);
	free(P_copy);

	/* Return the status */
	return status;
}