/*
 * Agility-SDK-era D3D12 root signature 1.2 definitions.
 *
 * mingw-w64's d3d12.h stops at D3D_ROOT_SIGNATURE_VERSION_1_1, so
 * D3D12_STATIC_SAMPLER_DESC1, D3D12_ROOT_SIGNATURE_DESC2 and the 1_2 version
 * enumerator are missing. OptiScaler's D3D12 root-signature hooks need them.
 *
 * D3D12_VERSIONED_ROOT_SIGNATURE_DESC is a union, and its Desc_1_2 arm occupies
 * the same storage as Desc_1_0/Desc_1_1, so the accessors below reinterpret the
 * existing union rather than trying to add a member to a type we do not own.
 *
 * Layout mirrors microsoft/DirectX-Headers (MIT).
 */
#ifndef OPTISCALER_COMPAT_D3D12_AGILITY_H
#define OPTISCALER_COMPAT_D3D12_AGILITY_H

#include <d3d12.h>

#ifndef D3D_ROOT_SIGNATURE_VERSION_1_2
#define D3D_ROOT_SIGNATURE_VERSION_1_2 ((D3D_ROOT_SIGNATURE_VERSION) 0x3)
#endif

#ifndef D3D12_SAMPLER_FLAG_NONE
typedef enum D3D12_SAMPLER_FLAGS
{
    D3D12_SAMPLER_FLAG_NONE = 0,
    D3D12_SAMPLER_FLAG_UINT_BORDER_COLOR = 0x1,
    D3D12_SAMPLER_FLAG_NON_NORMALIZED_COORDINATES = 0x2
} D3D12_SAMPLER_FLAGS;
#endif

typedef struct D3D12_STATIC_SAMPLER_DESC1
{
    D3D12_FILTER Filter;
    D3D12_TEXTURE_ADDRESS_MODE AddressU;
    D3D12_TEXTURE_ADDRESS_MODE AddressV;
    D3D12_TEXTURE_ADDRESS_MODE AddressW;
    FLOAT MipLODBias;
    UINT MaxAnisotropy;
    D3D12_COMPARISON_FUNC ComparisonFunc;
    D3D12_STATIC_BORDER_COLOR BorderColor;
    FLOAT MinLOD;
    FLOAT MaxLOD;
    UINT ShaderRegister;
    UINT RegisterSpace;
    D3D12_SHADER_VISIBILITY ShaderVisibility;
    D3D12_SAMPLER_FLAGS Flags;
} D3D12_STATIC_SAMPLER_DESC1;

typedef struct D3D12_ROOT_SIGNATURE_DESC2
{
    UINT NumParameters;
    const D3D12_ROOT_PARAMETER1* pParameters;
    UINT NumStaticSamplers;
    const D3D12_STATIC_SAMPLER_DESC1* pStaticSamplers;
    D3D12_ROOT_SIGNATURE_FLAGS Flags;
} D3D12_ROOT_SIGNATURE_DESC2;


/*
 * mingw-w64's d3d12.h predates the device-factory CLSIDs. Defined as a file
 * static so including this header from several TUs cannot produce duplicate
 * symbols (DEFINE_GUID would, without INITGUID discipline).
 * Value from microsoft/DirectX-Headers.
 */
#ifndef OPTI_HAVE_CLSID_D3D12DEVICEFACTORY
#define OPTI_HAVE_CLSID_D3D12DEVICEFACTORY
static const GUID CLSID_D3D12DeviceFactory = { 0x114863bf,
                                               0xc386,
                                               0x4aee,
                                               { 0xb3, 0x9d, 0x8f, 0x0b, 0xbb, 0x06, 0x29, 0x55 } };
#endif

#ifdef __cplusplus

// Desc_1_2 accessors over the existing (1_0/1_1) union storage.
inline D3D12_ROOT_SIGNATURE_DESC2& OptiDesc_1_2(D3D12_VERSIONED_ROOT_SIGNATURE_DESC& d)
{
    return *reinterpret_cast<D3D12_ROOT_SIGNATURE_DESC2*>(&d.Desc_1_0);
}

inline const D3D12_ROOT_SIGNATURE_DESC2& OptiDesc_1_2(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC& d)
{
    return *reinterpret_cast<const D3D12_ROOT_SIGNATURE_DESC2*>(&d.Desc_1_0);
}

#endif /* __cplusplus */

#endif /* OPTISCALER_COMPAT_D3D12_AGILITY_H */
