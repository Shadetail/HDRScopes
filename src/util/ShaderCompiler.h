// Runtime HLSL compilation via d3dcompiler. Shaders load from a shaders/ folder
// next to the exe (shipped builds) or from HDRSCOPES_SHADER_DIR (dev builds,
// so .hlsl edits take effect without a rebuild).
#pragma once

#include "util/Common.h"
#include <d3d11.h>

namespace shader {

// Compile a .hlsl file (filename relative to the shader dir) at the given entry
// point and target (e.g. "cs_5_0", "vs_5_0", "ps_5_0"). Returns the bytecode blob.
ComPtr<ID3DBlob> CompileFromFile(const wchar_t* filename,
                                 const char* entry,
                                 const char* target);

ComPtr<ID3D11ComputeShader>  MakeCompute (ID3D11Device* dev, const wchar_t* file, const char* entry);
ComPtr<ID3D11VertexShader>   MakeVertex  (ID3D11Device* dev, const wchar_t* file, const char* entry, ComPtr<ID3DBlob>* outBlob = nullptr);
ComPtr<ID3D11PixelShader>    MakePixel   (ID3D11Device* dev, const wchar_t* file, const char* entry);

} // namespace shader
