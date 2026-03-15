/*
 * eye.c
 *
 *  Created on: 10 feb. 2019
 *      Author: Daniel Mårtensson
 */

#include "declareFunctions.h"

/*
 * Create a identity matrix with size row x column
 */
void eye(float* A, int row, int column) {
    // Reset first
    memset(A, 0, row * column * sizeof(float));

    for (int i = 0; i < row; i++) {
        *A = 1.0;
        A += row + 1;
    }
}
