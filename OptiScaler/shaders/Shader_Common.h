#pragma once

#include "SysUtils.h"

#include <d3dcompiler.h>

ID3DBlob* CompileShader(const char* shaderCode, const char* entryPoint, const char* target,
                        const D3D_SHADER_MACRO* defines = nullptr);
