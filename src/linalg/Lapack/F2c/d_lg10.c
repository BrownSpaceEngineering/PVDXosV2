#include "../../Lapack/Include/f2c.h"

#define logf10e 0.43429448190325182765

#ifdef KR_headers
float logf();
float d_lg10(x)
floatreal* x;
#else
#undef fabs
#include "math.h"
#ifdef __cplusplus
extern "C" {
#endif
float d_lg10(floatreal* x)
#endif
{ return (logf10e * logf(*x)); }
#ifdef __cplusplus
}
#endif
