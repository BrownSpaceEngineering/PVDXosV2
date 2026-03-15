/*
 * max.c
 *
 *  Created on: 16 feb. 2019
 *      Author: Daniel Mårtensson
 */

#include "declareFunctions.h"

/*
 * Find the maximum value of vector A with size row x 1, return maximum value and the index of that
 * maximum value
 */
void maxvector(float* A, int row, float* val, int* index) {

    float past_val = 0; // initial
    *index = 0;
    *val = *(A);

    for (int i = 0; i < row; i++) {
        if (*(A + i) > past_val) {
            past_val = *(A + i); // Update
            *val = *(A + i);
            *index = i; // Remember the index position
        }
    }
}
