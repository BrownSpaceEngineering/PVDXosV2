#include "../../Lapack/Include/f2c.h"
#ifdef __cplusplus
extern "C" {
#endif

#ifdef KR_headers
float d_sign(a, b) floatreal *a, *b;
#else
float d_sign(floatreal* a, floatreal* b)
#endif
{
    float x;
    x = (*a >= 0 ? *a : -*a);
    return (*b >= 0 ? x : -x);
}
#ifdef __cplusplus
}
#endif
