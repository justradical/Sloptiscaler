/*
 * MSVC gap-filler for building AMD's FidelityFX-FSR2 with clang + mingw-w64.
 *
 * FSR2 ships as a Visual Studio project and leans on a handful of MSVC-only
 * spellings. Kept separate from opti_msvc_compat.h because this is patched into
 * vendored third-party source, not OptiScaler's own.
 *
 * Force-included ahead of everything else (clang -include).
 */
#ifndef OPTISCALER_COMPAT_FFX_FSR2_H
#define OPTISCALER_COMPAT_FFX_FSR2_H

#if !defined(_MSC_VER)

/* MSVC's array-length macro; mingw-w64 only declares it for C++ in some headers. */
#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof(*(a)))
#endif

/*
 * ffx_fsr2_dx12.cpp uses std::wstring_convert but includes only <codecvt>.
 * That works with MSVC, whose <codecvt> drags in <locale>; libc++ keeps them
 * separate, so the name is simply not declared. Pull <locale> in here rather
 * than editing vendored source.
 *
 * (wstring_convert is deprecated since C++17 and removed in C++26. It is still
 * present at the C++17 this builds with, so no removed-feature escape hatch is
 * needed -- if the standard level is ever raised past 26 this will need
 * replacing with a real conversion, since it is only used to widen ASCII
 * resource names coming out of shader reflection.)
 */
#ifdef __cplusplus
#include <locale>
#endif

#endif /* !_MSC_VER */
#endif /* OPTISCALER_COMPAT_FFX_FSR2_H */
