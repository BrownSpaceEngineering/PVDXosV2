/*
 * zeros.c
 *
 *  Created on: 16 feb. 2019
 *      Author: Daniel Mårtensson
 */
#include "declareFunctions.h"
/*
 * Turn all elements of A, size row x column, into 0.
 */

void zeros(float* A, int row, int column) { memset(A, 0, row * column * sizeof(float)); }
