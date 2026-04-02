/*
 * scale.c
 *
 *  Created on: 10 feb. 2019
 *      Author: Daniel Mårtensson
 */

#include "declareFunctions.h"

/*
 * Scale a matrix A with a scalar value. Size of matrix A is row x column
 */
void scale(float* A, float scalar, int row, int column) {

    for (int i = 0; i < row * column; i++)
        *(A + i) = *(A + i) * scalar;
}
