#include "util/ShaderCompiler.h"
#include <d3dcompiler.h>
#include <string>

namespace shader {

static std::wstring ShaderPath(const wchar_t* filename) {
    // HDRSCOPES_SHADER_DIR is a narrow string baked in by CMake (forward slashes
    // are fine for Win32 file APIs). Widen it and append the filename.
    const char* dir = HDRSCOPES_SHADER_DIR;
    std::wstring p;
    for (const char* c = dir; *c; ++c) p += (wchar_t)*c;
    p += L"/";
    p += filename;
    return p;
}

ComPtr<ID3DBlob> CompileFromFile(const wchar_t* filename,
                                 const char* entry,
                                 const char* target) {
    std::wstring full = ShaderPath(filename);

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

    ComPtr<ID3DBlob> code, errors;
    HRESULT hr = D3DCompileFromFile(
        full.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entry, target, flags, 0, &code, &errors);

    if (errors) {
        HDRLog("[shader] %ls (%s/%s):\n%s", filename, entry, target,
               (const char*)errors->GetBufferPointer());
    }
    if (FAILED(hr)) {
        HDRLog("[shader] compile FAILED hr=0x%08lx : %ls %s", (unsigned long)hr, filename, entry);
        return nullptr;
    }
    return code;
}

ComPtr<ID3D11ComputeShader> MakeCompute(ID3D11Device* dev, const wchar_t* file, const char* entry) {
    auto blob = CompileFromFile(file, entry, "cs_5_0");
    if (!blob) return nullptr;
    ComPtr<ID3D11ComputeShader> cs;
    if (FAILED(dev->CreateComputeShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &cs)))
        return nullptr;
    return cs;
}

ComPtr<ID3D11VertexShader> MakeVertex(ID3D11Device* dev, const wchar_t* file, const char* entry, ComPtr<ID3DBlob>* outBlob) {
    auto blob = CompileFromFile(file, entry, "vs_5_0");
    if (!blob) return nullptr;
    ComPtr<ID3D11VertexShader> vs;
    if (FAILED(dev->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &vs)))
        return nullptr;
    if (outBlob) *outBlob = blob;
    return vs;
}

ComPtr<ID3D11PixelShader> MakePixel(ID3D11Device* dev, const wchar_t* file, const char* entry) {
    auto blob = CompileFromFile(file, entry, "ps_5_0");
    if (!blob) return nullptr;
    ComPtr<ID3D11PixelShader> ps;
    if (FAILED(dev->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &ps)))
        return nullptr;
    return ps;
}

} // namespace shader
