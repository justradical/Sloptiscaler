/*
 * MSVC intrinsic / language-extension gap-filler for clang + mingw-w64.
 *
 * Force-included ahead of everything else (clang -include).
 */
#ifndef OPTISCALER_COMPAT_MSVC_H
#define OPTISCALER_COMPAT_MSVC_H

#if !defined(_MSC_VER)

/*
 * _ReturnAddress() is an MSVC intrinsic. OptiScaler uses it (~40 sites) to work
 * out which module called a hooked function, e.g. Util::WhoIsTheCaller().
 * __builtin_return_address(0) is the clang/gcc equivalent.
 *
 * clang-cl's <intrin.h> declares _ReturnAddress, but the mingw-w64 intrin.h
 * does not, so define it here.
 */
#ifndef _ReturnAddress
#define _ReturnAddress() (__builtin_return_address(0))
#endif

#ifndef _AddressOfReturnAddress
#define _AddressOfReturnAddress() (__builtin_frame_address(0))
#endif


/*
 * MSVC implicitly converts a function pointer to void* as a language
 * extension. OptiScaler's proxy helpers rely on it heavily:
 *
 *     static PFN_RtlGetVersion Hook_RtlGetVersion(PVOID method)
 *     ...
 *     NtdllProxy::Hook_RtlGetVersion(hkRtlGetVersion);   // bare function name
 *
 * Clang rejects that. There are ~97 such call sites but only 28 declarations,
 * so the parameter type is widened instead of casting at every call.
 * OptiFnArg accepts either a void* or any function pointer and converts to
 * void*, preserving the original behaviour.
 *
 * Declared in terms of void* rather than PVOID so this header stays usable as
 * a forced include, ahead of <windows.h>.
 */

/*
 * NVIDIA's Streamline headers (sl_consts.h, sl_pcl.h, sl_reflex.h) use
 * std::to_underlying without including <utility>. MSVC's STL pulls it in
 * transitively; libc++ does not.
 */
#ifdef __cplusplus
#include <utility>
#endif

#ifdef __cplusplus
struct OptiFnArg
{
    void* p;

    OptiFnArg(void* v) : p(v) {}
    OptiFnArg(decltype(nullptr)) : p(nullptr) {}

    template <class R, class... A> OptiFnArg(R (*f)(A...)) : p(reinterpret_cast<void*>(f)) {}

    operator void*() const { return p; }
};
#endif /* __cplusplus */


/*
 * OptiScaler uses the single-underscore spelling _uuidof(T) in a few places.
 * It is provided by MSVC's headers; mingw-w64 only supplies the standard
 * __uuidof (backed by __CRT_UUID_DECL on each interface).
 */
#ifdef __cplusplus
#ifndef _uuidof
#define _uuidof(T) __uuidof(T)
#endif
#endif

#endif /* !_MSC_VER */

#endif /* OPTISCALER_COMPAT_MSVC_H */
