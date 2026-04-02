/*
 * qr.c
 *
 *  Created on: 10 feb. 2019
 *      Author: Daniel Mårtensson
 */

#include "declareFunctions.h"

/*
 * Solve A=QR where A is a matrix of size row x column and Q is size of row x row and R is size of
 * row x column
 */
void qr(float* A, float* Q, float* R, int row, int column) {

    integer M = row;
    integer N = column;
    floatreal A_[row * column];
    memcpy(A_, A, row * column * sizeof(float));
    tran(A_, row, column);
    integer LDA = row;
    floatreal TAU[column];
    memset(TAU, 0, column * sizeof(float));
    floatreal WORK[column];
    memset(WORK, 0, column * sizeof(float));
    integer INFO_QR;

    // Solve nowc code
    dgeqr2_(&M, &N, A_, &LDA, TAU, WORK, &INFO_QR);

    // Take transpose of A_ - again
    tran(A_, column, row);

    // Get the upper triangular matrix from A_ with zero shift
    triu(A_, R, 0, row, column);

    // Now slove for Q
    eye(Q, row, row); // Create initial identity matrix
    float I[row * row];
    eye(I, row, row);
    float v[row]; // Create initial v vector
    float C[row * row];
    float E[row * row];
    memset(C, 0, row * row * sizeof(float)); // Initial zeros
    memset(E, 0, row * row * sizeof(float)); // Initial zeros

    for (int i = 0; i < column; i++) {
        memset(v, 0, row * sizeof(float)); // Reset with zeros
        v[i] = 1;
        for (int j = i + 1; j < row; j++)
            *(v + j) = *(A_ + j * (column) + i); // MATLAB code v(i+1:m) = A(i+1:m,i);
        mul(v, v, false, C, row, 1, row);        // MATLAB code C = v'*v
        scale(C, TAU[i], row, row);              // MATLAB code C = tau(i)*C
        sub(I, C, C, row, row, row);             // MATLAB code C = I-C
        mul(Q, C, false, E, row, row, row);      // MATLAB code E = Q*C
        memcpy(Q, E, row * row * sizeof(float)); // Q = E
    }
}
