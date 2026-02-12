/*
 * norm.c
 *
 *  Created on: 16 feb. 2019
 *      Author: Daniel Mårtensson
 */

#include "declareFunctions.h"

/*
 * Do norm of a vector A with size row x 1. Specify what kind of norm with P
 */

double norm(double* A, int row, norm_type_t norm_type) {

	/*
	 *  P = "1" - L1 Norm - Sum
	 *  P = "2" - L2 Norm - Abs
	 */

	double sum = 0; // Initial

	if (norm_type == L_1) {
		for (int i = 0; i < row; i++)
			sum += (*(A + i));
		return sum;
	} else if (norm_type == L_2) {
		for (int i = 0; i < row; i++)
			sum += fabs((*(A + i)));
		return sum;
	} else {
		return 0;
	}
}
