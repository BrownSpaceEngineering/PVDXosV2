/*
 * cofact.c
 *
 *  Created on: 15 feb. 2019
 *      Author: Daniel Mårtensson
 */

#include "declareFunctions.h"
/*
 * Create the cofactor matrix of A
 */

void cofact(float* A, float* CO, int row, int column) {

    /*
     * This using the formula A^(-1) = 1/det(A)*Adj(A) = 1/det(A)*Cofact(A)^T
     */

    // Create the inverse of A
    float A_inv[row * column];                      // Column == row in this case
    memcpy(A_inv, A, row * column * sizeof(float)); // copy(A, A_inv, row, column);
    inv(A_inv, row);
    float determinant = det(A, row);
    scale(A_inv, determinant, row, column);
    tran(A_inv, row, column);
    memcpy(CO, A_inv, row * column * sizeof(float)); // copy(A_inv, CO, row, column); // Done
}
