/*
 * vertcat.c
 *
 *  Created on: 16 feb. 2019
 *      Author: Daniel Mårtensson
 */

#include "declareFunctions.h"

/*
 * Concate A with B into C.
 * A size row_a x column_a
 * B size row_B x column_A
 * C size (row_a + row_b) x column_a
 */

void vertcat(float* A, float* B, float* C, int row_a, int column_a, int row_b) {

    /*
     * C = [A; B]
     */

    // We include the first matrix a
    for (int i = 0; i < row_a; i++) {
        for (int j = 0; j < column_a; j++) {
            *(C++) = *((A + i * column_a) + j);
        }
    }

    // We change the matrix now - We use b matrix now
    for (int i = 0; i < row_b; i++) {
        for (int j = 0; j < column_a; j++) {
            *(C++) = *((B + i * column_a) + j);
        }
    }
}
