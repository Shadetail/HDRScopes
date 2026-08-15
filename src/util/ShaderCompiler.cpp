#include "util/ShaderCompiler.h"
#include <d3dcompiler.h>
#include <string>

namespace shader {

static const std::wstring& ShaderDir() {
    static const std::wstring dir = [] {
        // Shipped builds carry a shaders/ folder next to the exe; prefer it.
        wchar_t exe[MAX_PATH];
        if (GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
            std::wstring d(exe);
            size_t slash = d.find_last_of(L"\\/");
            if (slash != std::wstring::npos) {
                d.resize(slash + 1);
                d += L"shaders";
                if (GetFileAttributesW((d + L"\\colorspaces.hlsli").c_str()) != INVALID_FILE_ATTRIBUTES)
                    return d;
            }
        }
        // Dev fallback: the source-tree path baked in by CMake (narrow string,
        // forward slashes are fine for Win32 file APIs). Keeps .hlsl edits
        // rebuild-free when running from the build tree.
        std::wstring d;
        for (const char* c = HDRSCOPES_SHADER_DIR; *c; ++c) d += (wchar_t)*c;
        return d;
    }();
    return dir;
}

static std::wstring ShaderPath(const wchar_t* filename) {
    return ShaderDir() + L"/" + filename;
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
