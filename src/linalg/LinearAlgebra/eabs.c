/*
 * fabse.c
 *
 *  Created on: 15 feb. 2019
 *      Author: Daniel Mårtensson
 */
#include "declareFunctions.h"

/*
 * Take the fabsolue values of every element of matrix A, size row x column
 */
void efabs(float* A, int row, int column) {
    for (int i = 0; i < row; i++)
        for (int j = 0; j < column; j++) {
            *A = fabs(*(A));
            A++;
        }
}
