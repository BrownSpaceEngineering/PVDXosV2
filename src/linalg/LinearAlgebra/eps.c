/**
 * eps.c
 *
 * Defines convenience function for comparing float and doubles (equality within tolerance)
 * and float and double matrices (elementwise equality within tolerance). 
 *
 * Created: Friday, 13th February 2026
 * Authors: Siddharta Laloux
 */
#include "declareFunctions.h"

/****************** FLOAT EPSILON COMPARISON FUNCTIONS ******************/

/**
 * \fn eps_close
 * 
 * \brief Compares two floats for "closeness" within a specified tolerance 
 * (epsilon) accounting for the magnitude of the numbers. 
 * 
 * \param a the first float to compare
 * \param b the second float to compare
 * \param epsilon the tolerance for comparison (e.g. FLT_EPSILON)
 */
inline bool f_eps_close(float a, float b, float epsilon) {
    float diff = fabsf(a - b);
    if (diff < epsilon) {
        return true; // Identical or very close
    } 
    
    // Scale epsilon by the magnitude of the numbers
    float largest = (fabsf(b) > fabsf(a)) ? fabsf(b) : fabsf(a);
    return diff <= (largest * epsilon);
}

/**
 * \fn eps_close_default
 * 
 * \brief Compares two floats for "closeness" within a specified tolerance 
 * (epsilon) accounting for the magnitude of the numbers. 
 * 
 * \note This function uses the default epsilon value of FLT_EPSILON, 
 * which is the smallest difference between two representable floats.
 * 
 * \param a the first float to compare
 * \param b the second float to compare
 */
inline bool f_eps_close_default(float a, float b) {
    return f_eps_close(a, b, FLT_EPSILON);
}

/**
 * \fn eps_close_matrix
 * 
 * \brief Compares two matrices of floats for "closeness" within a specified tolerance 
 * (epsilon) accounting for the magnitude of the numbers. 
 * 
 * \param A the first matrix to compare (as a 1D array in row-major order)
 * \param B the second matrix to compare (as a 1D array in row-major order)
 * \param row the number of rows in each matrix
 * \param column the number of columns in each matrix
 * \param epsilon the tolerance for comparison (e.g. FLT_EPSILON)
 */
inline bool f_eps_close_matrix(float* A, float* B, int row, int column, float epsilon) {
    for(int i = 0; i < row * column; i++){
        if (!f_eps_close(A[i], B[i], epsilon)) {
            return false;
        }
    }
    return true;
}   

/**
 * \fn eps_close_matrix_default
 * 
 * \brief Compares two matrices of floats for "closeness" within a specified tolerance 
 * (epsilon) accounting for the magnitude of the numbers. 
 * 
 * \note This function uses the default epsilon value of FLT_EPSILON, 
 * which is the smallest difference between two representable floats.
 * 
 * \param A the first matrix to compare (as a 1D array in row-major order)
 * \param B the second matrix to compare (as a 1D array in row-major order)
 * \param row the number of rows in each matrix
 * \param column the number of columns in each matrix
 */
inline bool f_eps_close_matrix_default(float* A, float* B, int row, int column) {
    return f_eps_close_matrix(A, B, row, column, FLT_EPSILON);
}

/****************** DOUBLE EPSILON COMPARISON FUNCTIONS ******************/

/**
 * \fn eps_close
 * 
 * \brief Compares two doubles for "closeness" within a specified tolerance 
 * (epsilon) accounting for the magnitude of the numbers. 
 * 
 * \param a the first double to compare
 * \param b the second double to compare
 * \param epsilon the tolerance for comparison (e.g. DBL_EPSILON)
 */
inline bool dbl_eps_close(double a, double b, double epsilon) {
    double diff = fabs(a - b);
    if (diff < epsilon) {
        return true; // Identical or very close
    } 
    
    // Scale epsilon by the magnitude of the numbers
    double largest = (fabs(b) > fabs(a)) ? fabs(b) : fabs(a);
    return diff <= (largest * epsilon);
}

/**
 * \fn eps_close_default
 * 
 * \brief Compares two floats for "closeness" within a specified tolerance 
 * (epsilon) accounting for the magnitude of the numbers. 
 * 
 * \note This function uses the default epsilon value of FLT_EPSILON, 
 * which is the smallest difference between two representable floats.
 * 
 * \param a the first float to compare
 * \param b the second float to compare
 */
inline bool dbl_eps_close_default(double a, double b) {
    return dbl_eps_close(a, b, DBL_EPSILON);
}

/**
 * \fn eps_close_matrix
 * 
 * \brief Compares two matrices of floats for "closeness" within a specified tolerance 
 * (epsilon) accounting for the magnitude of the numbers. 
 * 
 * \param A the first matrix to compare (as a 1D array in row-major order)
 * \param B the second matrix to compare (as a 1D array in row-major order)
 * \param row the number of rows in each matrix
 * \param column the number of columns in each matrix
 * \param epsilon the tolerance for comparison (e.g. FLT_EPSILON)
 */
inline bool dbl_eps_close_matrix(double* A, double* B, int row, int column, double epsilon) {
    for(int i = 0; i < row * column; i++){
        if (!dbl_eps_close(A[i], B[i], epsilon)) {
            return false;
        }
    }
    return true;
}

/**
 * \fn eps_close_matrix_default
 * 
 * \brief Compares two matrices of floats for "closeness" within a specified tolerance 
 * (epsilon) accounting for the magnitude of the numbers. 
 * 
 * \note This function uses the default epsilon value of FLT_EPSILON, 
 * which is the smallest difference between two representable floats.
 * 
 * \param A the first matrix to compare (as a 1D array in row-major order)
 * \param B the second matrix to compare (as a 1D array in row-major order)
 * \param row the number of rows in each matrix
 * \param column the number of columns in each matrix
 */
inline bool dbl_eps_close_matrix_default(double* A, double* B, int row, int column) {
    return dbl_eps_close_matrix(A, B, row, column, DBL_EPSILON);
}
