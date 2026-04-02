/*
 * vec.c
 *
 *  Created on: 16 feb. 2019
 *      Author: Daniel Mårtensson
 */

#include "declareFunctions.h"

/*
 * Create a vector B with the size row*column x 1 from a matrix A size row x column
 */
void vec(float* A, float* B, int row, int column) {

    // This is the same as the GNU octave vec(a) or MATLAB command a(:)
    for (int i = 0; i < column; i++) {
        for (int j = 0; j < row; j++) {
            *(B++) = *((A + j * column) + i);
        }
    }
}
