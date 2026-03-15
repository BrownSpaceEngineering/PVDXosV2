/*
 * pinv.c
 *
 *  Created on: 16 feb. 2019
 *      Author: Daniel Mårtensson
 */
#include "declareFunctions.h"

/*
 * Pseudo inverse of A with size row x column
 * Notice that pseudo inverse of A will return the size column x row if A is size row x column
 */
void pinv(float* A, int row, int column) {
    /*
     * Do SVD
     */
    float U[row * row];
    float S[row * column];
    float V[column * column];
    svd(A, U, S, V, row, column);

    /*
     * Economic matrix from SVD - S need to be square!
     */

    /*
     * This follows the equation pinv(A) = V*inv(S)*U^T
     */

    if (row > column) {
        float U_econ[row * column];
        cut(U, row, row, U_econ, 0, row - 1, 0, column - 1);
        float S_econ[column * column];
        cut(S, row, column, S_econ, 0, column - 1, 0, column - 1);
        float T[column * row];

        // Inverse of S
        inv(S_econ, column);

        // Transpose of U
        tran(U_econ, row, column);

        // Multiply inv(s)*u'
        mul(S_econ, U_econ, false, T, column, column, row);

        // Now find pinv
        mul(V, T, false, A, column, column, row);

    } else {
        float S_econ[row * row];
        cut(S, row, column, S_econ, 0, row - 1, 0, row - 1);
        float V_econ[column * row];
        cut(V, column, column, V_econ, 0, column - 1, 0, row - 1);
        float T[row * row];

        // Inverse of S
        inv(S_econ, row);

        // Transpose of U
        tran(U, row, row);

        // Multiply inv(s)*u'
        mul(S_econ, U, false, T, row, row, row);

        // Now find pinv
        mul(V_econ, T, false, A, column, row, row);
    }
}
