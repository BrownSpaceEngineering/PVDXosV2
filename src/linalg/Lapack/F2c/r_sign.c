#include "../../Lapack/Include/f2c.h"
#ifdef __cplusplus
extern "C" {
#endif

#ifdef KR_headers
float r_sign(a, b) real *a, *b;
#else
float r_sign(real* a, real* b)
#endif
{
    float x;
    x = (*a >= 0 ? *a : -*a);
    return (*b >= 0 ? x : -x);
}
#ifdef __cplusplus
}
#endif
