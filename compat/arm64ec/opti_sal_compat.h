/*
 * SAL annotation gap-filler for mingw-w64 / clang builds.
 *
 * mingw-w64's <sal.h> covers most of the Microsoft source-annotation macros,
 * but not __success(...), which NVIDIA's nvapi.h relies on:
 *
 *   #define NVAPI_INTERFACE extern __success(return == NVAPI_OK) NvAPI_Status __cdecl
 *
 * Left undefined it parses as a call expression and breaks every declaration
 * in the header. SAL annotations carry no codegen meaning outside MSVC's static
 * analyser, so defining it away is safe.
 *
 * Force-included ahead of everything else (clang -include).
 */
#ifndef OPTISCALER_COMPAT_SAL_H
#define OPTISCALER_COMPAT_SAL_H

#include <sal.h>

#ifndef __success
#define __success(expr)
#endif

#ifndef __nullterminated
#define __nullterminated
#endif

#ifndef __nullnullterminated
#define __nullnullterminated
#endif


/*
 * Legacy (SAL1) parameter annotations. mingw-w64's sal.h provides the SAL2
 * "_In_" family but not the older "__in" family that nvapi.h still uses.
 */
#ifndef __in
#define __in
#endif
#ifndef __out
#define __out
#endif
#ifndef __inout
#define __inout
#endif
#ifndef __in_opt
#define __in_opt
#endif
#ifndef __out_opt
#define __out_opt
#endif
#ifndef __inout_opt
#define __inout_opt
#endif
#ifndef __reserved
#define __reserved
#endif
#ifndef __notnull
#define __notnull
#endif
#ifndef __maybenull
#define __maybenull
#endif

#include "opti_sal_params.h"
#include "opti_msvc_compat.h"
#include "opti_atomic_compat.h"

#endif /* OPTISCALER_COMPAT_SAL_H */
