/*
 * print.c
 *
 *  Created on: 8 feb. 2019
 *      Author: Daniel Mårtensson
 */
#include "declareFunctions.h"

/*
 * Print a matrix A, with the dimension row x column
 */
void print(float* A, int row, int column) {
    for (int i = 0; i < row; i++) {
        for (int j = 0; j < column; j++) {
            printf("%0.18f ", *(A++));
        }
        printf("\n");
    }
    printf("\n");
}

/**
 * \fn debug_matrix
 *
 * \brief Debug prints a matrix A, with the dimension row x column
 *
 * \param A the matrix to print (as a 1D array in row-major order)
 * \param row the number of rows in the matrix
 * \param column the number of columns in the matrix
 */
void debug_matrix(float* A, int row, int column) {
    for (int i = 0; i < row; i++) {
        for (int j = 0; j < column; j++) {
            printf("%0.18f ", *(A++));
        }
        printf("\n");
    }
    printf("\n");
}