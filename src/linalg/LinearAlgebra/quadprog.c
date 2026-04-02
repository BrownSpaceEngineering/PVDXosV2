/*
 * quadprog.c
 *
 *  Created on: 28 oct. 2019
 *      Author: Daniel Mårtensson
 */

#include "declareFunctions.h"

/*
 * This will minimize
 *  1/2*u^THu + g^Tu
 *
 *  With S.t
 *  ulb_vec <= u <= uub_vec
 *  ylb_vec <= Au <= yub_vec
 *
 *  H is a hessian(symmetric) positive positive semidefinite matrix
 *
 *  Notice that if you want to solve Ax = b with constraints, just set
 *  H = A^T*A
 *  g = A^T*b
 *
 *  Set nWSR to a number > 0 so QP can do its sets
 *  Set SHOW_QP_OUTPUT to TRUE to begin with in declareFunctions.h
 *  I recommend to read the qpOASES_e manual
 */
// void quadprog(float* H, float* g, float* A, float* ulb_vec, float* uub_vec, float* ylb_vec,
// float* yub_vec, int* nWSR, float* u, int columnH, int rowA) {

// 	// Setting up QProblem object J = U^T*H*U + g^T*U
// 	QProblem objective;
// 	Options options;
// 	int nV = columnH;
// 	int nC = rowA;
// 	QProblemCON(&objective, nV, nC, HST_UNKNOWN);
// 	Options_setToDefault(&options);
// 	QProblem_setOptions(&objective, options);

// 	// Solve
// 	QProblem_init(&objective, H, g, A, ulb_vec, uub_vec, ylb_vec, yub_vec, nWSR, 0);

// 	// Get the u
// 	QProblem_getPrimalSolution(&objective, u);

// }
