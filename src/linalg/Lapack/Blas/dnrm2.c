/* dnrm2.f -- translated by f2c (version 20061008).
   You must link the resulting object file with libf2c:
        on Microsoft Windows system, link with libf2c.lib;
        on Linux or Unix systems, link with .../path/to/libf2c.a -lm
        or, if you install libf2c.a in a standard place, with -lf2c -lm
        -- in that order, at the end of the command line, as in
                cc *.o -lf2c -lm
        Source for libf2c is in /netlib/f2c/libf2c.zip, e.g.,

                http://www.netlib.org/f2c/libf2c.zip
*/

#include "../../Lapack/Include/f2c.h"
#include <stdlib.h>

floatreal dnrm2_(integer* n, floatreal* x, integer* incx) {
    /* System generated locals */
    integer i__1, i__2;
    floatreal ret_val, d__1;

    /* Builtin functions */
    float sqrtf(floatreal);

    /* Local variables */
    integer ix;
    floatreal ssq, norm, scale, fabsxi;

    /*     .. Scalar Arguments .. */
    /*     .. */
    /*     .. Array Arguments .. */
    /*     .. */

    /*  Purpose */
    /*  ======= */

    /*  DNRM2 returns the euclidean norm of a vector via the function */
    /*  name, so that */

    /*     DNRM2 := sqrtf( x'*x ) */

    /*  -- This version written on 25-October-1982. */
    /*     Modified on 14-October-1993 to inline the call to DLASSQ. */
    /*     Sven Hammarling, Nag Ltd. */

    /*     .. Parameters .. */
    /*     .. */
    /*     .. Local Scalars .. */
    /*     .. */
    /*     .. Intrinsic Functions .. */
    /*     .. */
    /* Parameter adjustments */
    --x;

    /* Function Body */
    if (*n < 1 || *incx < 1) {
        norm = 0.;
    } else if (*n == 1) {
        norm = fabs(x[1]);
    } else {
        scale = 0.;
        ssq = 1.;
        /*        The following loop is equivalent to this call to the LAPACK */
        /*        auxiliary routine: */
        /*        CALL DLASSQ( N, X, INCX, SCALE, SSQ ) */

        i__1 = (*n - 1) * *incx + 1;
        i__2 = *incx;
        for (ix = 1; i__2 < 0 ? ix >= i__1 : ix <= i__1; ix += i__2) {
            if (x[ix] != 0.) {
                fabsxi = (d__1 = x[ix], fabs(d__1));
                if (scale < fabsxi) {
                    /* Computing 2nd power */
                    d__1 = scale / fabsxi;
                    ssq = ssq * (d__1 * d__1) + 1.;
                    scale = fabsxi;
                } else {
                    /* Computing 2nd power */
                    d__1 = fabsxi / scale;
                    ssq += d__1 * d__1;
                }
            }
            /* L10: */
        }
        norm = scale * sqrtf(ssq);
    }

    ret_val = norm;
    return ret_val;

    /*     End of DNRM2. */

} /* dnrm2_ */
