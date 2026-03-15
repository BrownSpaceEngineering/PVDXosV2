/*
 * declareFunctions.h
 *
 *  Created on: 8 feb. 2019
 *      Author: Daniel Mårtensson
 */

#ifndef LINEARALGEBRA_DECLAREFUNCTIONS_H_
#define LINEARALGEBRA_DECLAREFUNCTIONS_H_

#define TRUE 1
#define FALSE 0

#include <float.h> // required for DBL_EPSILON
#include <math.h>
#include <stdbool.h>
#include <stdint.h> // For uint8_t and others
#include <stdio.h>
#include <string.h>

// This is for qpOASES - If you want red output results or not
// #define SHOW_QP_OUTPUT FALSE

/*
 * Lapack library
 */
#include "linalg/Lapack/Include/clapack.h"
#include "linalg/Lapack/Include/f2c.h"
// #include "../../src/Lapack/Include/blaswrap.h" // No need in this project! :)
// #include "../../src/qpOASES/Header/qpOASES_e.h"

typedef enum {
    L_1,
    L_2
} norm_type_t;

/*
 * Linear algebra package
 */
void tran(float *A, int row, int column);
void tranf(float *A, int row, int column);
void print(float *A, int row, int column);
void linsolve(float *A, float *X, float *B, int row, int column_b);
void linsolvef(float *A, float *X, float *B, int row, int column_b);
void svd(float *A, float *U, float *S, float *V, int row, int column);
void diag(float *A, float *B, int row_b, int column_b);
void qr(float *A, float *Q, float *R, int row, int column);
void triu(float *A, float *B, int shift, int row, int column);
void eye(float *A, int row, int column);
void mul(float *A, float *B, bool elementWise, float *C, int row_a, int column_a, int column_b);
void mulf(float *A, float *B, bool elementWise, float *C, int row_a, int column_a, int column_b);
void scale(float *A, float scalar, int row, int column);
void sub(float *A, float *B, float *C, int row_a, int column_a, int column_b);
void lu(float *A, float *L, float *U, float *P, int row, int column);
void tril(float *A, float *B, int shift, int row, int column);
void inv(float *A, int row);
void invf(float *A, int row);
void chol(float *A, float *L, int row);
float det(float *A, int row);
void toeplitz(float *A, float *B, int length);
void hankel(float *A, float *H, int length, int step);
void cut(float *A, int row, int column, float *B, int start_row, int stop_row, int start_column, int stop_column);
void diagpower(float *A, float p, int row, int column);
void efabs(float *A, int row, int column);
void add(float *A, float *B, float *C, int row_a, int column_a, int column_b);
void copy(float *A, float *B, int row, int column);
void cofact(float *A, float *CO, int row, int column);
void mdiag(float *A, float *B, int row, int column);
float dot(float *A, float *B, int row);
void horzcat(float *A, float *B, float *C, int row_a, int column_a, int column_b);
void maxvector(float *A, int row, float *val, int *index);
void minvector(float *A, int row, float *val, int *index);
float norm(float *A, int row, norm_type_t norm_type);
void ones(float *A, int row, int column);
void pinv(float *A, int row, int column);
void power(float *A, int row, int column, float value);
void repmat(float *A, int row, int column, int horz, int vert, float *B);
void sqrtfe(float *A, int row, int column);
void sumrows(float *A, float *B, int row, int column);
void vec(float *A, float *B, int row, int column);
void vertcat(float *A, float *B, float *C, int row_a, int column_a, int row_b);
void zeros(float *A, int row, int column);
int rank(float *A, int row);
void eig(float *A, float *Ereal, float *Eimag, float *Vreal_left, float *Vimag_left, float *Vreal_right, float *Vimag_right, int row);
void mpower(float *A, int row, int n);
void insert(float *A, float *B, int row_a, int column_a, int column_b, int startRow_b, int startColumn_b);
void move(float *A, int row, int column, int down, int right);
// void quadprog(float* H, float* g, float* A, float* ulb_vec, float*
// uub_vec,  float* ylb_vec, float* yub_vec, int* nWSR, float* u, int
// columnH, int rowA);
void linprog(float *c, float *A, float *b, float *x, int row_a, int column_a, uint8_t max_or_min, int iteration_limit);
bool f_eps_close(float a, float b, float epsilon);
bool f_eps_close_default(float a, float b);
bool f_eps_close_matrix(float *A, float *B, int row, int column, float epsilon);
bool f_eps_close_matrix_default(float *A, float *B, int row, int column);
bool dbl_eps_close(float a, float b, float epsilon);
bool dbl_eps_close_default(float a, float b);
bool dbl_eps_close_matrix(float *A, float *B, int row, int column, float epsilon);
bool dbl_eps_close_matrix_default(float *A, float *B, int row, int column);
void debug_matrix(float *A, int row, int column);

#endif /* LINEARALGEBRA_DECLAREFUNCTIONS_H_ */
