#ifndef OPTISCALER_COMPAT_SAL_PARAMS_H
#define OPTISCALER_COMPAT_SAL_PARAMS_H

/*
 * Parameterized legacy (SAL1) buffer annotations.
 *
 * mingw-w64's sal.h implements the SAL2 "_In_"/"_Out_" family and a handful of
 * SAL1 names, but not the full __in_ecount/__deref_out_bcount/... set that
 * NVIDIA's nvapi.h uses heavily. Undefined, each one parses as a call
 * expression and swallows the parameter type that follows it.
 *
 * They are analysis-only annotations with no codegen meaning outside MSVC, so
 * they expand to nothing. Declared variadic so the 1-arg and 2-arg
 * (..._part(size, len)) forms are both accepted.
 */

#ifndef __deref_bcount
#define __deref_bcount(...)
#endif
#ifndef __deref_bcount_opt
#define __deref_bcount_opt(...)
#endif
#ifndef __deref_ecount
#define __deref_ecount(...)
#endif
#ifndef __deref_ecount_opt
#define __deref_ecount_opt(...)
#endif
#ifndef __deref_inout_bcount
#define __deref_inout_bcount(...)
#endif
#ifndef __deref_inout_bcount_full
#define __deref_inout_bcount_full(...)
#endif
#ifndef __deref_inout_bcount_full_opt
#define __deref_inout_bcount_full_opt(...)
#endif
#ifndef __deref_inout_bcount_nz
#define __deref_inout_bcount_nz(...)
#endif
#ifndef __deref_inout_bcount_nz_opt
#define __deref_inout_bcount_nz_opt(...)
#endif
#ifndef __deref_inout_bcount_opt
#define __deref_inout_bcount_opt(...)
#endif
#ifndef __deref_inout_bcount_part
#define __deref_inout_bcount_part(...)
#endif
#ifndef __deref_inout_bcount_part_opt
#define __deref_inout_bcount_part_opt(...)
#endif
#ifndef __deref_inout_bcount_z
#define __deref_inout_bcount_z(...)
#endif
#ifndef __deref_inout_bcount_z_opt
#define __deref_inout_bcount_z_opt(...)
#endif
#ifndef __deref_inout_ecount
#define __deref_inout_ecount(...)
#endif
#ifndef __deref_inout_ecount_full
#define __deref_inout_ecount_full(...)
#endif
#ifndef __deref_inout_ecount_full_opt
#define __deref_inout_ecount_full_opt(...)
#endif
#ifndef __deref_inout_ecount_nz
#define __deref_inout_ecount_nz(...)
#endif
#ifndef __deref_inout_ecount_nz_opt
#define __deref_inout_ecount_nz_opt(...)
#endif
#ifndef __deref_inout_ecount_opt
#define __deref_inout_ecount_opt(...)
#endif
#ifndef __deref_inout_ecount_part
#define __deref_inout_ecount_part(...)
#endif
#ifndef __deref_inout_ecount_part_opt
#define __deref_inout_ecount_part_opt(...)
#endif
#ifndef __deref_inout_ecount_z
#define __deref_inout_ecount_z(...)
#endif
#ifndef __deref_inout_ecount_z_opt
#define __deref_inout_ecount_z_opt(...)
#endif
#ifndef __deref_opt_bcount
#define __deref_opt_bcount(...)
#endif
#ifndef __deref_opt_bcount_opt
#define __deref_opt_bcount_opt(...)
#endif
#ifndef __deref_opt_ecount
#define __deref_opt_ecount(...)
#endif
#ifndef __deref_opt_ecount_opt
#define __deref_opt_ecount_opt(...)
#endif
#ifndef __deref_opt_inout_bcount
#define __deref_opt_inout_bcount(...)
#endif
#ifndef __deref_opt_inout_bcount_full
#define __deref_opt_inout_bcount_full(...)
#endif
#ifndef __deref_opt_inout_bcount_full_opt
#define __deref_opt_inout_bcount_full_opt(...)
#endif
#ifndef __deref_opt_inout_bcount_nz
#define __deref_opt_inout_bcount_nz(...)
#endif
#ifndef __deref_opt_inout_bcount_nz_opt
#define __deref_opt_inout_bcount_nz_opt(...)
#endif
#ifndef __deref_opt_inout_bcount_opt
#define __deref_opt_inout_bcount_opt(...)
#endif
#ifndef __deref_opt_inout_bcount_part
#define __deref_opt_inout_bcount_part(...)
#endif
#ifndef __deref_opt_inout_bcount_part_opt
#define __deref_opt_inout_bcount_part_opt(...)
#endif
#ifndef __deref_opt_inout_bcount_z
#define __deref_opt_inout_bcount_z(...)
#endif
#ifndef __deref_opt_inout_bcount_z_opt
#define __deref_opt_inout_bcount_z_opt(...)
#endif
#ifndef __deref_opt_inout_ecount
#define __deref_opt_inout_ecount(...)
#endif
#ifndef __deref_opt_inout_ecount_full
#define __deref_opt_inout_ecount_full(...)
#endif
#ifndef __deref_opt_inout_ecount_full_opt
#define __deref_opt_inout_ecount_full_opt(...)
#endif
#ifndef __deref_opt_inout_ecount_nz
#define __deref_opt_inout_ecount_nz(...)
#endif
#ifndef __deref_opt_inout_ecount_nz_opt
#define __deref_opt_inout_ecount_nz_opt(...)
#endif
#ifndef __deref_opt_inout_ecount_opt
#define __deref_opt_inout_ecount_opt(...)
#endif
#ifndef __deref_opt_inout_ecount_part
#define __deref_opt_inout_ecount_part(...)
#endif
#ifndef __deref_opt_inout_ecount_part_opt
#define __deref_opt_inout_ecount_part_opt(...)
#endif
#ifndef __deref_opt_inout_ecount_z
#define __deref_opt_inout_ecount_z(...)
#endif
#ifndef __deref_opt_inout_ecount_z_opt
#define __deref_opt_inout_ecount_z_opt(...)
#endif
#ifndef __deref_opt_out_bcount_full
#define __deref_opt_out_bcount_full(...)
#endif
#ifndef __deref_opt_out_bcount_full_opt
#define __deref_opt_out_bcount_full_opt(...)
#endif
#ifndef __deref_opt_out_bcount_nz_opt
#define __deref_opt_out_bcount_nz_opt(...)
#endif
#ifndef __deref_opt_out_bcount_opt
#define __deref_opt_out_bcount_opt(...)
#endif
#ifndef __deref_opt_out_bcount_part
#define __deref_opt_out_bcount_part(...)
#endif
#ifndef __deref_opt_out_bcount_part_opt
#define __deref_opt_out_bcount_part_opt(...)
#endif
#ifndef __deref_opt_out_bcount_z_opt
#define __deref_opt_out_bcount_z_opt(...)
#endif
#ifndef __deref_opt_out_ecount
#define __deref_opt_out_ecount(...)
#endif
#ifndef __deref_opt_out_ecount_full
#define __deref_opt_out_ecount_full(...)
#endif
#ifndef __deref_opt_out_ecount_full_opt
#define __deref_opt_out_ecount_full_opt(...)
#endif
#ifndef __deref_opt_out_ecount_nz_opt
#define __deref_opt_out_ecount_nz_opt(...)
#endif
#ifndef __deref_opt_out_ecount_opt
#define __deref_opt_out_ecount_opt(...)
#endif
#ifndef __deref_opt_out_ecount_part
#define __deref_opt_out_ecount_part(...)
#endif
#ifndef __deref_opt_out_ecount_part_opt
#define __deref_opt_out_ecount_part_opt(...)
#endif
#ifndef __deref_opt_out_ecount_z_opt
#define __deref_opt_out_ecount_z_opt(...)
#endif
#ifndef __deref_out_bcount
#define __deref_out_bcount(...)
#endif
#ifndef __deref_out_bcount_full
#define __deref_out_bcount_full(...)
#endif
#ifndef __deref_out_bcount_full_opt
#define __deref_out_bcount_full_opt(...)
#endif
#ifndef __deref_out_bcount_nz
#define __deref_out_bcount_nz(...)
#endif
#ifndef __deref_out_bcount_nz_opt
#define __deref_out_bcount_nz_opt(...)
#endif
#ifndef __deref_out_bcount_opt
#define __deref_out_bcount_opt(...)
#endif
#ifndef __deref_out_bcount_part
#define __deref_out_bcount_part(...)
#endif
#ifndef __deref_out_bcount_part_opt
#define __deref_out_bcount_part_opt(...)
#endif
#ifndef __deref_out_bcount_z
#define __deref_out_bcount_z(...)
#endif
#ifndef __deref_out_bcount_z_opt
#define __deref_out_bcount_z_opt(...)
#endif
#ifndef __deref_out_ecount_full_opt
#define __deref_out_ecount_full_opt(...)
#endif
#ifndef __deref_out_ecount_nz
#define __deref_out_ecount_nz(...)
#endif
#ifndef __deref_out_ecount_nz_opt
#define __deref_out_ecount_nz_opt(...)
#endif
#ifndef __deref_out_ecount_opt
#define __deref_out_ecount_opt(...)
#endif
#ifndef __deref_out_ecount_part
#define __deref_out_ecount_part(...)
#endif
#ifndef __deref_out_ecount_part_opt
#define __deref_out_ecount_part_opt(...)
#endif
#ifndef __deref_out_ecount_z
#define __deref_out_ecount_z(...)
#endif
#ifndef __deref_out_ecount_z_opt
#define __deref_out_ecount_z_opt(...)
#endif
#ifndef __in_bcount_nz_opt
#define __in_bcount_nz_opt(...)
#endif
#ifndef __in_bcount_opt
#define __in_bcount_opt(...)
#endif
#ifndef __in_bcount_z_opt
#define __in_bcount_z_opt(...)
#endif
#ifndef __in_ecount_nz_opt
#define __in_ecount_nz_opt(...)
#endif
#ifndef __in_ecount_opt
#define __in_ecount_opt(...)
#endif
#ifndef __in_ecount_z_opt
#define __in_ecount_z_opt(...)
#endif
#ifndef __inout_bcount_full_opt
#define __inout_bcount_full_opt(...)
#endif
#ifndef __inout_bcount_nz_opt
#define __inout_bcount_nz_opt(...)
#endif
#ifndef __inout_bcount_opt
#define __inout_bcount_opt(...)
#endif
#ifndef __inout_bcount_part_opt
#define __inout_bcount_part_opt(...)
#endif
#ifndef __inout_bcount_z_opt
#define __inout_bcount_z_opt(...)
#endif
#ifndef __inout_ecount_full_opt
#define __inout_ecount_full_opt(...)
#endif
#ifndef __inout_ecount_nz_opt
#define __inout_ecount_nz_opt(...)
#endif
#ifndef __inout_ecount_opt
#define __inout_ecount_opt(...)
#endif
#ifndef __inout_ecount_part_opt
#define __inout_ecount_part_opt(...)
#endif
#ifndef __inout_ecount_z_opt
#define __inout_ecount_z_opt(...)
#endif
#ifndef __in_range
#define __in_range(...)
#endif
#ifndef __out_bcount_full_opt
#define __out_bcount_full_opt(...)
#endif
#ifndef __out_bcount_full_z_opt
#define __out_bcount_full_z_opt(...)
#endif
#ifndef __out_bcount_nz_opt
#define __out_bcount_nz_opt(...)
#endif
#ifndef __out_bcount_opt
#define __out_bcount_opt(...)
#endif
#ifndef __out_bcount_part_opt
#define __out_bcount_part_opt(...)
#endif
#ifndef __out_bcount_part_z_opt
#define __out_bcount_part_z_opt(...)
#endif
#ifndef __out_bcount_z_opt
#define __out_bcount_z_opt(...)
#endif
#ifndef __out_ecount_full_opt
#define __out_ecount_full_opt(...)
#endif
#ifndef __out_ecount_full_z_opt
#define __out_ecount_full_z_opt(...)
#endif
#ifndef __out_ecount_nz_opt
#define __out_ecount_nz_opt(...)
#endif
#ifndef __out_ecount_opt
#define __out_ecount_opt(...)
#endif
#ifndef __out_ecount_part_opt
#define __out_ecount_part_opt(...)
#endif
#ifndef __out_ecount_part_z_opt
#define __out_ecount_part_z_opt(...)
#endif
#ifndef __out_ecount_z_opt
#define __out_ecount_z_opt(...)
#endif

#endif /* OPTISCALER_COMPAT_SAL_PARAMS_H */
