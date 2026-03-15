/*
 * chol.c
 *
 *  Created on: 12 feb. 2019
 *      Author: Daniel Mårtensson
 */

#include "declareFunctions.h"

/*
 * Create a lower triangular matrix L with size row x row from a symmetric real square matrix A with
 * size row x row
 */

void chol(float* A, float* L, int row) {

    memset(L, 0, row * row * sizeof(float));

    for (int i = 0; i < row; i++)
        for (int j = 0; j < (i + 1); j++) {
            float sum = 0;
            for (int k = 0; k < j; k++)
                sum += L[i * row + k] * L[j * row + k];
            L[i * row + j] = (i == j) ? sqrtf(A[i * row + i] - sum)
                                      : (1.0 / L[j * row + j] * (A[i * row + j] - sum));
        }
}
