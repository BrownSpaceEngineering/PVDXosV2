/*
 * eig.c
 *
 *  Created on: 16 feb. 2019
 *      Author: Daniel Mårtensson
 */

#include "declareFunctions.h"

void loadEigen(integer row, floatreal* wr, floatreal* wi, float* Ereal, float* Eimag);
void loadVector(integer N, floatreal* wi, floatreal* v, float* Vreal, float* Vimag);

/*
 *	Solve the eigenvalue problem
 *	A size row x row
 *	Ereal size row x 1 - Eigenvalues real part
 *	Eimag size row x 1 - Eigenvalues imag part
 *	Vreal_left size row x row - Eigenvectors real left
 *	Vimag_left size row x row - Eigenvectors imag left
 *	Vreal_right size row x row - Eigenvectors real right
 *	Vimag_right size row x row  - Eigenvectors imag right
 */

void eig(float* A, float* Ereal, float* Eimag, float* Vreal_left, float* Vimag_left,
         float* Vreal_right, float* Vimag_right, int row) {
    /* Locals */
    integer N = row;
    integer LDA = row;
    integer LDVL = row;
    integer LDVR = row;
    integer INFO_EIG;
    integer LWORK;
    floatreal wkopt;
    floatreal WORK[row * row];
    floatreal WR[row];
    floatreal WI[row];
    floatreal VL[row * row];
    floatreal VR[row * row];

    // Load the A_ matrix
    floatreal A_[row * row];
    memcpy(A_, A, row * row * sizeof(float));
    tran(A_, row, row);

    LWORK = -1; // This will load values
    dgeev_("V", "V", &N, A_, &LDA, WR, WI, VL, &LDVL, VR, &LDVR, &wkopt, &LWORK, &INFO_EIG);
    LWORK = (int)wkopt;

    /* Solve eigenproblem */
    dgeev_("V", "V", &N, A_, &LDA, WR, WI, VL, &LDVL, VR, &LDVR, WORK, &LWORK, &INFO_EIG);

    tran(VL, N, N);
    tran(VR, N, N);

    /*
     * Load the values into the mats
     */
    loadEigen(N, WR, WI, Ereal, Eimag);
    loadVector(N, WI, VL, Vreal_left, Vimag_left);
    loadVector(N, WI, VR, Vreal_right, Vimag_right);
}

void loadEigen(integer N, floatreal* WR, floatreal* WI, float* Ereal, float* Eimag) {

    for (int j = 0; j < N; j++) {
// TODO: fix these pragmas
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"

        if (WI[j] == (float)0.0) {
            // printf(" %6.2f", WR[j]);
            *(Ereal++) = WR[j];
            *(Eimag++) = 0;

#pragma GCC diagnostic pop

        } else {
            // printf(" (%6.2f,%6.2f)", WR[j], WI[j]);
            *(Ereal++) = WR[j];
            *(Eimag++) = WI[j];
        }
    }
}

void loadVector(integer N, floatreal* WI, floatreal* V, float* Vreal, float* Vimag) {

    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {

// TODO: fix these pragmas
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"

            if (WI[j] == (float)0.0) {
                *(Vreal + i * N + j) = *(V + i * N + j);
                *(Vimag + i * N + j) = 0.0;

#pragma GCC diagnostic pop

            } else {
                *(Vreal + i * N + j) = *(V + i * N + j);
                *(Vreal + i * N + j + 1) = -*(V + i * N + j);
                *(Vimag + i * N + j) = *(V + i * N + j + 1);
                *(Vimag + i * N + j + 1) = -*(V + i * N + j + 1);
                j++;
            }
        }
    }
}
