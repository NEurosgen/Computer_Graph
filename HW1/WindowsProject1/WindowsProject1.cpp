#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <assert.h>
#include <tchar.h>
#include <DirectXMath.h>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using namespace DirectX;

#define SAFE_RELEASE(p) { if (p) { (p)->Release(); (p) = nullptr; } }
#define DDS_MAGIC 0x20534444 

ID3D11Device* m_pDevice = nullptr;
ID3D11DeviceContext* m_pDeviceContext = nullptr;
IDXGISwapChain* m_pSwapChain = nullptr;
ID3D11RenderTargetView* m_pBackBufferRTV = nullptr;
ID3D11Texture2D* m_pDepthStencilBuffer = nullptr;
ID3D11DepthStencilView* m_pDepthStencilView = nullptr;

ID3D11DepthStencilState* m_pDepthStateOpaque = nullptr;
ID3D11DepthStencilState* m_pDepthStateSkybox = nullptr;
ID3D11DepthStencilState* m_pDepthStateTransparent = nullptr;
ID3D11BlendState* m_pBlendStateTransparent = nullptr;

ID3D11Buffer* m_pCubeVB = nullptr;
ID3D11Buffer* m_pCubeIB = nullptr;
ID3D11VertexShader* m_pCubeVS = nullptr;
ID3D11PixelShader* m_pCubePS = nullptr;
ID3D11PixelShader* m_pTransPS = nullptr; // Новый шейдер для прозрачности
ID3D11InputLayout* m_pCubeLayout = nullptr;
ID3D11ShaderResourceView* m_pCubeTextureView = nullptr;

ID3D11Buffer* m_pSkyboxVB = nullptr;
ID3D11Buffer* m_pSkyboxIB = nullptr;
ID3D11VertexShader* m_pSkyboxVS = nullptr;
ID3D11PixelShader* m_pSkyboxPS = nullptr;
ID3D11InputLayout* m_pSkyboxLayout = nullptr;
ID3D11ShaderResourceView* m_pSkyboxView = nullptr;
UINT m_skyboxIndexCount = 0;
ID3D11RasterizerState* m_pRasterizerStateSkybox = nullptr;

ID3D11Buffer* m_pGeomBuffer = nullptr;
ID3D11Buffer* m_pSceneBuffer = nullptr;
ID3D11SamplerState* m_pSampler = nullptr;

UINT m_width = 1280;
UINT m_height = 720;
ULONGLONG startTime = 0;
ULONGLONG lastTime = 0;

XMVECTOR camPosition = XMVectorSet(4.0f, 3.0f, -5.0f, 0.0f);
float camYaw = -0.65f;
float camPitch = 0.45f;

struct TextureVertex {
    float x, y, z;
    float u, v;
};

struct SkyboxVertex {
    float x, y, z;
};

struct GeomBuffer {
    XMMATRIX model;
    XMVECTOR size;
    XMVECTOR colorMultiplier;
};

struct SceneBuffer {
    XMMATRIX vp;
    XMVECTOR cameraPos;
};

struct TextureDesc {
    UINT32 pitch = 0;
    UINT32 mipmapsCount = 0;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    UINT32 width = 0;
    UINT32 height = 0;
    void* pData = nullptr;
};

struct DDS_PIXELFORMAT {
    uint32_t dwSize;
    uint32_t dwFlags;
    uint32_t dwFourCC;
    uint32_t dwRGBBitCount;
    uint32_t dwRBitMask;
    uint32_t dwGBitMask;
    uint32_t dwBBitMask;
    uint32_t dwABitMask;
};

struct DDS_HEADER {
    uint32_t        dwSize;
    uint32_t        dwFlags;
    uint32_t        dwHeight;
    uint32_t        dwWidth;
    uint32_t        dwPitchOrLinearSize;
    uint32_t        dwDepth;
    uint32_t        dwMipMapCount;
    uint32_t        dwReserved1[11];
    DDS_PIXELFORMAT ddspf;
    uint32_t        dwCaps;
    uint32_t        dwCaps2;
    uint32_t        dwCaps3;
    uint32_t        dwCaps4;
    uint32_t        dwReserved2;
};

bool LoadDDS(const wchar_t* filename, TextureDesc& desc, bool isCubemap = false) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    uint32_t magic;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (magic != DDS_MAGIC) return false;

    DDS_HEADER header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));

    desc.width = header.dwWidth;
    desc.height = header.dwHeight;
    desc.mipmapsCount = header.dwMipMapCount == 0 ? 1 : header.dwMipMapCount;

    if (header.ddspf.dwFlags & 0x4) {
        switch (header.ddspf.dwFourCC) {
        case 0x31545844: desc.fmt = DXGI_FORMAT_BC1_UNORM; break;
        case 0x33545844: desc.fmt = DXGI_FORMAT_BC2_UNORM; break;
        case 0x35545844: desc.fmt = DXGI_FORMAT_BC3_UNORM; break;
        default: desc.fmt = DXGI_FORMAT_UNKNOWN; break;
        }
    }
    else {
        desc.fmt = DXGI_FORMAT_B8G8R8A8_UNORM;
    }

    std::streamsize dataSize = size - sizeof(magic) - sizeof(header);
    desc.pData = new char[dataSize];
    file.read(reinterpret_cast<char*>(desc.pData), dataSize);

    return true;
}

HRESULT CreateTextureSRV(ID3D11Device* device, const TextureDesc& desc, bool isCubemap, ID3D11ShaderResourceView** ppSRV) {
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = desc.width;
    texDesc.Height = desc.height;
    texDesc.MipLevels = desc.mipmapsCount;
    texDesc.ArraySize = isCubemap ? 6 : 1;
    texDesc.Format = desc.fmt;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.MiscFlags = isCubemap ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;

    std::vector<D3D11_SUBRESOURCE_DATA> initData(texDesc.ArraySize * texDesc.MipLevels);
    size_t offset = 0;

    for (UINT arraySlice = 0; arraySlice < texDesc.ArraySize; ++arraySlice) {
        for (UINT mip = 0; mip < texDesc.MipLevels; ++mip) {
            UINT mipWidth = std::max(1u, desc.width >> mip);
            UINT mipHeight = std::max(1u, desc.height >> mip);
            UINT mipPitch = 0, mipLines = 0;

            if (desc.fmt == DXGI_FORMAT_BC1_UNORM) {
                mipPitch = std::max(1u, (mipWidth + 3) / 4) * 8;
                mipLines = std::max(1u, (mipHeight + 3) / 4);
            }
            else if (desc.fmt == DXGI_FORMAT_BC2_UNORM || desc.fmt == DXGI_FORMAT_BC3_UNORM) {
                mipPitch = std::max(1u, (mipWidth + 3) / 4) * 16;
                mipLines = std::max(1u, (mipHeight + 3) / 4);
            }
            else {
                mipPitch = mipWidth * 4;
                mipLines = mipHeight;
            }

            UINT index = arraySlice * texDesc.MipLevels + mip;
            initData[index].pSysMem = static_cast<const char*>(desc.pData) + offset;
            initData[index].SysMemPitch = mipPitch;

            offset += static_cast<size_t>(mipPitch) * mipLines;
        }
    }

    ID3D11Texture2D* pTexture = nullptr;
    HRESULT hr = device->CreateTexture2D(&texDesc, initData.data(), &pTexture);
    if (FAILED(hr)) return hr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = isCubemap ? D3D11_SRV_DIMENSION_TEXTURECUBE : D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = texDesc.MipLevels;

    hr = device->CreateShaderResourceView(pTexture, &srvDesc, ppSRV);
    SAFE_RELEASE(pTexture);

    return hr;
}

const char* ShadersSource = R"(
cbuffer GeomBuffer : register(b0) {
    float4x4 model;
    float4 size; 
    float4 colorMultiplier;
};

cbuffer SceneBuffer : register(b1) {
    float4x4 vp;
    float4 cameraPos;
};

Texture2D colorTexture : register(t0);
TextureCube skyboxTexture : register(t0);
SamplerState colorSampler : register(s0);

struct VSCubeInput {
    float3 pos : POSITION;
    float2 uv : TEXCOORD;
};

struct VSCubeOutput {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
};

VSCubeOutput vs_cube(VSCubeInput vertex) {
    VSCubeOutput result;
    float4 worldPos = mul(model, float4(vertex.pos, 1.0));
    result.pos = mul(vp, worldPos);
    result.uv = vertex.uv;
    return result;
}

// Обычный шейдер с текстурой
float4 ps_cube(VSCubeOutput pixel) : SV_Target0 {
    float4 texColor = colorTexture.Sample(colorSampler, pixel.uv);
    return texColor * colorMultiplier;
}

// Шейдер для полупрозрачных плоскостей без текстуры
float4 ps_trans(VSCubeOutput pixel) : SV_Target0 {
    return float4(colorMultiplier.xyz, colorMultiplier.w);
}

struct VSSkyboxInput {
    float3 pos : POSITION;
};

struct VSSkyboxOutput {
    float4 pos : SV_Position;
    float3 localPos : POSITION1;
};

VSSkyboxOutput vs_skybox(VSSkyboxInput vertex) {
    VSSkyboxOutput result;
    float3 pos = cameraPos.xyz + vertex.pos * size.x;
    result.pos = mul(vp, float4(pos, 1.0));
    result.pos.z = 0.0f; // Принудительно 0 для Reversed Depth
    result.localPos = vertex.pos;
    return result;
}

float4 ps_skybox(VSSkyboxOutput pixel) : SV_Target0 {
    return float4(skyboxTexture.Sample(colorSampler, pixel.localPos).xyz, 1.0);
}
)";

HRESULT CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT result = m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    if (SUCCEEDED(result)) {
        result = m_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_pBackBufferRTV);
        SAFE_RELEASE(pBackBuffer);
    }

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = m_width;
    depthDesc.Height = m_height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    result = m_pDevice->CreateTexture2D(&depthDesc, nullptr, &m_pDepthStencilBuffer);
    if (SUCCEEDED(result)) {
        result = m_pDevice->CreateDepthStencilView(m_pDepthStencilBuffer, nullptr, &m_pDepthStencilView);
    }

    return result;
}

void GenerateSphere(int latLines, int longLines, std::vector<SkyboxVertex>& vertices, std::vector<USHORT>& indices) {
    float phiStep = XM_PI / latLines;
    float thetaStep = 2.0f * XM_PI / longLines;

    vertices.push_back({ 0.0f, 1.0f, 0.0f });

    for (int i = 1; i <= latLines - 1; ++i) {
        float phi = i * phiStep;
        for (int j = 0; j <= longLines; ++j) {
            float theta = j * thetaStep;
            SkyboxVertex v;
            v.x = sinf(phi) * cosf(theta);
            v.y = cosf(phi);
            v.z = sinf(phi) * sinf(theta);
            vertices.push_back(v);
        }
    }
    vertices.push_back({ 0.0f, -1.0f, 0.0f });

    for (int i = 1; i <= longLines; ++i) {
        indices.push_back(0);
        indices.push_back(i + 1);
        indices.push_back(i);
    }

    int baseIndex = 1;
    int ringVertexCount = longLines + 1;
    for (int i = 0; i < latLines - 2; ++i) {
        for (int j = 0; j < longLines; ++j) {
            indices.push_back(baseIndex + i * ringVertexCount + j);
            indices.push_back(baseIndex + i * ringVertexCount + j + 1);
            indices.push_back(baseIndex + (i + 1) * ringVertexCount + j);

            indices.push_back(baseIndex + (i + 1) * ringVertexCount + j);
            indices.push_back(baseIndex + i * ringVertexCount + j + 1);
            indices.push_back(baseIndex + (i + 1) * ringVertexCount + j + 1);
        }
    }

    int southPoleIndex = static_cast<int>(vertices.size()) - 1;
    baseIndex = southPoleIndex - ringVertexCount;
    for (int i = 0; i < longLines; ++i) {
        indices.push_back(southPoleIndex);
        indices.push_back(baseIndex + i);
        indices.push_back(baseIndex + i + 1);
    }
}

#include <sys/stat.h>

inline bool FileExists(const std::wstring& name) {
    struct _stat buffer;
    return (_wstat(name.c_str(), &buffer) == 0);
}

std::wstring GetAssetPath(const std::wstring& filename) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring basePath(exePath);

    size_t lastSlash = basePath.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        basePath = basePath.substr(0, lastSlash + 1);
    }

    std::wstring directPath = basePath + L"Assets\\" + filename;
    if (FileExists(directPath)) {
        return directPath;
    }

    std::wstring idePath = basePath;
    for (int i = 0; i < 2; ++i) {
        if (!idePath.empty() && (idePath.back() == L'\\' || idePath.back() == L'/')) {
            idePath.pop_back();
        }
        size_t slash = idePath.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            idePath = idePath.substr(0, slash + 1);
        }
    }

    return idePath + L"Assets\\" + filename;
}

HRESULT InitScene() {
    HRESULT hr = S_OK;

    D3D11_DEPTH_STENCIL_DESC dssDesc = {};
    dssDesc.DepthEnable = TRUE;
    dssDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dssDesc.DepthFunc = D3D11_COMPARISON_GREATER;
    m_pDevice->CreateDepthStencilState(&dssDesc, &m_pDepthStateOpaque);

    dssDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dssDesc.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
    m_pDevice->CreateDepthStencilState(&dssDesc, &m_pDepthStateSkybox);

    dssDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dssDesc.DepthFunc = D3D11_COMPARISON_GREATER;
    m_pDevice->CreateDepthStencilState(&dssDesc, &m_pDepthStateTransparent);

    D3D11_BLEND_DESC bsDesc = {};
    bsDesc.RenderTarget[0].BlendEnable = TRUE;
    bsDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bsDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bsDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bsDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bsDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bsDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bsDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    m_pDevice->CreateBlendState(&bsDesc, &m_pBlendStateTransparent);

    static const TextureVertex CubeVertices[24] = {
        {-0.5, -0.5,  0.5, 0, 1}, { 0.5, -0.5,  0.5, 1, 1}, { 0.5, -0.5, -0.5, 1, 0}, {-0.5, -0.5, -0.5, 0, 0},
        {-0.5,  0.5, -0.5, 0, 1}, { 0.5,  0.5, -0.5, 1, 1}, { 0.5,  0.5,  0.5, 1, 0}, {-0.5,  0.5,  0.5, 0, 0},
        {-0.5, -0.5, -0.5, 0, 1}, { 0.5, -0.5, -0.5, 1, 1}, { 0.5,  0.5, -0.5, 1, 0}, {-0.5,  0.5, -0.5, 0, 0},
        { 0.5, -0.5,  0.5, 0, 1}, {-0.5, -0.5,  0.5, 1, 1}, {-0.5,  0.5,  0.5, 1, 0}, { 0.5,  0.5,  0.5, 0, 0},
        {-0.5, -0.5,  0.5, 0, 1}, {-0.5, -0.5, -0.5, 1, 1}, {-0.5,  0.5, -0.5, 1, 0}, {-0.5,  0.5,  0.5, 0, 0},
        { 0.5, -0.5, -0.5, 0, 1}, { 0.5, -0.5,  0.5, 1, 1}, { 0.5,  0.5,  0.5, 1, 0}, { 0.5,  0.5, -0.5, 0, 0}
    };
    static const UINT16 CubeIndices[36] = {
        0, 2, 1, 0, 3, 2,       4, 6, 5, 4, 7, 6,       8, 10, 9, 8, 11, 10,
        12, 14, 13, 12, 15, 14, 16, 18, 17, 16, 19, 18, 20, 22, 21, 20, 23, 22
    };

    D3D11_BUFFER_DESC vbDescCube = { sizeof(CubeVertices), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA vbDataCube = { CubeVertices, 0, 0 };
    m_pDevice->CreateBuffer(&vbDescCube, &vbDataCube, &m_pCubeVB);

    D3D11_BUFFER_DESC ibDescCube = { sizeof(CubeIndices), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA ibDataCube = { CubeIndices, 0, 0 };
    m_pDevice->CreateBuffer(&ibDescCube, &ibDataCube, &m_pCubeIB);

    std::vector<SkyboxVertex> sphereVertices;
    std::vector<USHORT> sphereIndices;
    GenerateSphere(20, 20, sphereVertices, sphereIndices);
    m_skyboxIndexCount = static_cast<UINT>(sphereIndices.size());

    D3D11_BUFFER_DESC vbDescSky = { (UINT)(sphereVertices.size() * sizeof(SkyboxVertex)), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA vbDataSky = { sphereVertices.data(), 0, 0 };
    m_pDevice->CreateBuffer(&vbDescSky, &vbDataSky, &m_pSkyboxVB);

    D3D11_BUFFER_DESC ibDescSky = { (UINT)(sphereIndices.size() * sizeof(USHORT)), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA ibDataSky = { sphereIndices.data(), 0, 0 };
    m_pDevice->CreateBuffer(&ibDescSky, &ibDataSky, &m_pSkyboxIB);

    D3D11_BUFFER_DESC geomDesc = { sizeof(GeomBuffer), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
    m_pDevice->CreateBuffer(&geomDesc, nullptr, &m_pGeomBuffer);

    D3D11_BUFFER_DESC sceneDesc = { sizeof(SceneBuffer), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
    m_pDevice->CreateBuffer(&sceneDesc, nullptr, &m_pSceneBuffer);

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ID3DBlob* pVSBlob = nullptr; ID3DBlob* pPSBlob = nullptr; ID3DBlob* pErrorBlob = nullptr;

    D3DCompile(ShadersSource, strlen(ShadersSource), nullptr, nullptr, nullptr, "vs_cube", "vs_5_0", flags, 0, &pVSBlob, &pErrorBlob);
    m_pDevice->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), nullptr, &m_pCubeVS);
    D3D11_INPUT_ELEMENT_DESC layoutCube[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    m_pDevice->CreateInputLayout(layoutCube, 2, pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), &m_pCubeLayout);
    SAFE_RELEASE(pVSBlob);

    D3DCompile(ShadersSource, strlen(ShadersSource), nullptr, nullptr, nullptr, "ps_cube", "ps_5_0", flags, 0, &pPSBlob, &pErrorBlob);
    m_pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &m_pCubePS);
    SAFE_RELEASE(pPSBlob);

    // Компилируем шейдер для полупрозрачности
    D3DCompile(ShadersSource, strlen(ShadersSource), nullptr, nullptr, nullptr, "ps_trans", "ps_5_0", flags, 0, &pPSBlob, &pErrorBlob);
    m_pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &m_pTransPS);
    SAFE_RELEASE(pPSBlob);

    D3DCompile(ShadersSource, strlen(ShadersSource), nullptr, nullptr, nullptr, "vs_skybox", "vs_5_0", flags, 0, &pVSBlob, &pErrorBlob);
    m_pDevice->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), nullptr, &m_pSkyboxVS);
    D3D11_INPUT_ELEMENT_DESC layoutSky[] = { {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0} };
    m_pDevice->CreateInputLayout(layoutSky, 1, pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), &m_pSkyboxLayout);
    SAFE_RELEASE(pVSBlob);

    D3DCompile(ShadersSource, strlen(ShadersSource), nullptr, nullptr, nullptr, "ps_skybox", "ps_5_0", flags, 0, &pPSBlob, &pErrorBlob);
    m_pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &m_pSkyboxPS);
    SAFE_RELEASE(pPSBlob);

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_ANISOTROPIC;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.MaxAnisotropy = 16;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = -FLT_MAX;
    sampDesc.MaxLOD = FLT_MAX;
    m_pDevice->CreateSamplerState(&sampDesc, &m_pSampler);

    D3D11_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D11_FILL_SOLID;
    rastDesc.CullMode = D3D11_CULL_NONE;
    m_pDevice->CreateRasterizerState(&rastDesc, &m_pRasterizerStateSkybox);

    TextureDesc cubeDesc;
    std::wstring cubePath = GetAssetPath(L"vect.dds");
    if (LoadDDS(cubePath.c_str(), cubeDesc, false)) {
        CreateTextureSRV(m_pDevice, cubeDesc, false, &m_pCubeTextureView);
        delete[] static_cast<char*>(cubeDesc.pData);
    }

    TextureDesc skyboxDesc;
    std::wstring skyboxPath = GetAssetPath(L"skybox.dds");
    if (LoadDDS(skyboxPath.c_str(), skyboxDesc, true)) {
        CreateTextureSRV(m_pDevice, skyboxDesc, true, &m_pSkyboxView);
        delete[] static_cast<char*>(skyboxDesc.pData);
    }

    return hr;
}

HRESULT InitDirectX(HWND hWnd) {
    HRESULT result;
    IDXGIFactory* pFactory = nullptr;
    result = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory);
    if (FAILED(result)) return result;

    IDXGIAdapter* pSelectedAdapter = nullptr;
    IDXGIAdapter* pAdapter = nullptr;
    UINT adapterIdx = 0;
    while (SUCCEEDED(pFactory->EnumAdapters(adapterIdx, &pAdapter))) {
        DXGI_ADAPTER_DESC desc;
        pAdapter->GetDesc(&desc);
        if (wcscmp(desc.Description, L"Microsoft Basic Render Driver") != 0) {
            pSelectedAdapter = pAdapter;
            break;
        }
        pAdapter->Release();
        adapterIdx++;
    }

    if (!pSelectedAdapter) return E_FAIL;
    D3D_FEATURE_LEVEL level;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    result = D3D11CreateDevice(pSelectedAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, flags, levels, 1, D3D11_SDK_VERSION, &m_pDevice, &level, &m_pDeviceContext);
    SAFE_RELEASE(pSelectedAdapter);
    if (FAILED(result)) return result;

    DXGI_SWAP_CHAIN_DESC swapChainDesc = { 0 };
    swapChainDesc.BufferCount = 2;
    swapChainDesc.BufferDesc.Width = m_width;
    swapChainDesc.BufferDesc.Height = m_height;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = hWnd;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.Windowed = true;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    result = pFactory->CreateSwapChain(m_pDevice, &swapChainDesc, &m_pSwapChain);
    SAFE_RELEASE(pFactory);
    if (FAILED(result)) return result;

    result = CreateRenderTarget();
    if (FAILED(result)) return result;

    startTime = GetTickCount64();
    lastTime = startTime;

    return InitScene();
}

void Render() {
    if (!m_pDeviceContext || !m_pSwapChain) return;

    ULONGLONG currentTime = GetTickCount64();
    float elapsedSec = (currentTime - startTime) / 1000.0f;
    float deltaTime = (currentTime - lastTime) / 1000.0f;
    lastTime = currentTime;

    m_pDeviceContext->ClearState();

    ID3D11RenderTargetView* views[] = { m_pBackBufferRTV };
    m_pDeviceContext->OMSetRenderTargets(1, views, m_pDepthStencilView);

    static const FLOAT BackColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
    m_pDeviceContext->ClearRenderTargetView(m_pBackBufferRTV, BackColor);
    m_pDeviceContext->ClearDepthStencilView(m_pDepthStencilView, D3D11_CLEAR_DEPTH, 0.0f, 0);

    float speed = 5.0f * deltaTime;
    float rotSpeed = 2.0f * deltaTime;

    if (GetAsyncKeyState(VK_UP) & 0x8000)    camPitch -= rotSpeed;
    if (GetAsyncKeyState(VK_DOWN) & 0x8000)  camPitch += rotSpeed;
    if (GetAsyncKeyState(VK_LEFT) & 0x8000)  camYaw -= rotSpeed;
    if (GetAsyncKeyState(VK_RIGHT) & 0x8000) camYaw += rotSpeed;

    if (camPitch > XM_PIDIV2 - 0.01f)  camPitch = XM_PIDIV2 - 0.01f;
    if (camPitch < -XM_PIDIV2 + 0.01f) camPitch = -XM_PIDIV2 + 0.01f;

    XMMATRIX rotation = XMMatrixRotationRollPitchYaw(camPitch, camYaw, 0.0f);
    XMVECTOR forward = XMVector3TransformCoord(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotation);
    XMVECTOR right = XMVector3TransformCoord(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), rotation);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    if (GetAsyncKeyState('W') & 0x8000) camPosition += forward * speed;
    if (GetAsyncKeyState('S') & 0x8000) camPosition -= forward * speed;
    if (GetAsyncKeyState('D') & 0x8000) camPosition += right * speed;
    if (GetAsyncKeyState('A') & 0x8000) camPosition -= right * speed;

    XMMATRIX view = XMMatrixLookAtLH(camPosition, camPosition + forward, up);
    float fov = XM_PI / 3.0f;
    float aspectRatio = (float)m_width / (float)m_height;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;

    XMMATRIX proj = XMMatrixPerspectiveFovLH(fov, aspectRatio, farPlane, nearPlane);

    float width = tanf(fov / 2.0f) * nearPlane * 2.0f;
    float height = width / aspectRatio;
    float sphereRadius = sqrtf(nearPlane * nearPlane + (width / 2.0f) * (width / 2.0f) + (height / 2.0f) * (height / 2.0f)) * 1.1f;

    D3D11_MAPPED_SUBRESOURCE subresource;
    if (SUCCEEDED(m_pDeviceContext->Map(m_pSceneBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &subresource))) {
        SceneBuffer* pSceneBuffer = reinterpret_cast<SceneBuffer*>(subresource.pData);
        pSceneBuffer->vp = XMMatrixMultiply(view, proj);
        pSceneBuffer->cameraPos = camPosition;
        m_pDeviceContext->Unmap(m_pSceneBuffer, 0);
    }

    D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (FLOAT)m_width, (FLOAT)m_height, 0.0f, 1.0f };
    m_pDeviceContext->RSSetViewports(1, &viewport);

    // Добавляем привязку для пиксельного шейдера
    ID3D11Buffer* constBuffers[] = { m_pGeomBuffer, m_pSceneBuffer };
    m_pDeviceContext->VSSetConstantBuffers(0, 2, constBuffers);
    m_pDeviceContext->PSSetConstantBuffers(0, 1, &m_pGeomBuffer);

    ID3D11SamplerState* samplers[] = { m_pSampler };
    m_pDeviceContext->PSSetSamplers(0, 1, samplers);

    // --- Шаг 1. Отрисовка непрозрачных объектов ---
    m_pDeviceContext->OMSetDepthStencilState(m_pDepthStateOpaque, 0);
    m_pDeviceContext->RSSetState(nullptr);
    m_pDeviceContext->VSSetShader(m_pCubeVS, nullptr, 0);
    m_pDeviceContext->PSSetShader(m_pCubePS, nullptr, 0);

    UINT strideCube = sizeof(TextureVertex);
    UINT offsetCube = 0;
    m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pCubeVB, &strideCube, &offsetCube);
    m_pDeviceContext->IASetIndexBuffer(m_pCubeIB, DXGI_FORMAT_R16_UINT, 0);
    m_pDeviceContext->IASetInputLayout(m_pCubeLayout);
    m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D11ShaderResourceView* cubeRes[] = { m_pCubeTextureView };
    m_pDeviceContext->PSSetShaderResources(0, 1, cubeRes);

    GeomBuffer cubeGeom;
    cubeGeom.colorMultiplier = XMVectorSet(1.0f, 1.0f, 1.0f, 1.0f);

    // Куб 1
    cubeGeom.model = XMMatrixRotationY(elapsedSec) * XMMatrixTranslation(-1.5f, 0.0f, 0.0f);
    m_pDeviceContext->UpdateSubresource(m_pGeomBuffer, 0, nullptr, &cubeGeom, 0, 0);
    m_pDeviceContext->DrawIndexed(36, 0, 0);

    // Куб 2
    cubeGeom.model = XMMatrixRotationY(-elapsedSec) * XMMatrixTranslation(1.5f, 0.0f, 0.0f);
    m_pDeviceContext->UpdateSubresource(m_pGeomBuffer, 0, nullptr, &cubeGeom, 0, 0);
    m_pDeviceContext->DrawIndexed(36, 0, 0);

    // --- Шаг 2. Отрисовка Скайбокса ---
    m_pDeviceContext->OMSetDepthStencilState(m_pDepthStateSkybox, 0);
    m_pDeviceContext->RSSetState(m_pRasterizerStateSkybox);

    GeomBuffer skyboxGeom;
    skyboxGeom.model = XMMatrixIdentity();
    skyboxGeom.size = XMVectorSet(sphereRadius, 0.0f, 0.0f, 0.0f);
    m_pDeviceContext->UpdateSubresource(m_pGeomBuffer, 0, nullptr, &skyboxGeom, 0, 0);

    ID3D11ShaderResourceView* skyboxRes[] = { m_pSkyboxView };
    m_pDeviceContext->PSSetShaderResources(0, 1, skyboxRes);

    m_pDeviceContext->VSSetShader(m_pSkyboxVS, nullptr, 0);
    m_pDeviceContext->PSSetShader(m_pSkyboxPS, nullptr, 0);
    m_pDeviceContext->IASetIndexBuffer(m_pSkyboxIB, DXGI_FORMAT_R16_UINT, 0);
    UINT strideSky = sizeof(SkyboxVertex);
    UINT offsetSky = 0;
    m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pSkyboxVB, &strideSky, &offsetSky);
    m_pDeviceContext->IASetInputLayout(m_pSkyboxLayout);
    m_pDeviceContext->DrawIndexed(m_skyboxIndexCount, 0, 0);
    m_pDeviceContext->OMSetDepthStencilState(m_pDepthStateTransparent, 0);
    m_pDeviceContext->OMSetBlendState(m_pBlendStateTransparent, nullptr, 0xFFFFFFFF);
    m_pDeviceContext->RSSetState(nullptr);
    m_pDeviceContext->VSSetShader(m_pCubeVS, nullptr, 0);
    m_pDeviceContext->PSSetShader(m_pTransPS, nullptr, 0);
    m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pCubeVB, &strideCube, &offsetCube);
    m_pDeviceContext->IASetIndexBuffer(m_pCubeIB, DXGI_FORMAT_R16_UINT, 0);
    m_pDeviceContext->IASetInputLayout(m_pCubeLayout);

    struct TransObj {
        XMVECTOR position;
        XMVECTOR color;
        float distanceToCam;
    };

    std::vector<TransObj> transObjects = {
        { XMVectorSet(0.0f, 0.0f, -0.3f, 1.0f), XMVectorSet(0.8f, 0.2f, 0.8f, 0.5f), 0.0f }, // Фиолетовая плоскость 
        { XMVectorSet(0.0f, 0.0f, 0.3f, 1.0f), XMVectorSet(0.8f, 0.8f, 0.2f, 0.5f), 0.0f }  // Желтая плоскость 
    };


    for (auto& obj : transObjects) {
        XMVECTOR diff = XMVectorSubtract(obj.position, camPosition);
        obj.distanceToCam = XMVectorGetX(XMVector3LengthSq(diff));
    }
    std::sort(transObjects.begin(), transObjects.end(), [](const TransObj& a, const TransObj& b) {
        return a.distanceToCam > b.distanceToCam;
        });

    for (const auto& obj : transObjects) {
        cubeGeom.colorMultiplier = obj.color;
        cubeGeom.model = XMMatrixScaling(4.0f, 4.0f, 0.01f) * XMMatrixTranslationFromVector(obj.position);
        m_pDeviceContext->UpdateSubresource(m_pGeomBuffer, 0, nullptr, &cubeGeom, 0, 0);
        m_pDeviceContext->DrawIndexed(36, 0, 0);
    }

    m_pDeviceContext->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);

    m_pSwapChain->Present(1, 0);
}

void Cleanup() {
    if (m_pDeviceContext) m_pDeviceContext->ClearState();

    SAFE_RELEASE(m_pTransPS);
    SAFE_RELEASE(m_pDepthStateOpaque);
    SAFE_RELEASE(m_pDepthStateSkybox);
    SAFE_RELEASE(m_pDepthStateTransparent);
    SAFE_RELEASE(m_pBlendStateTransparent);

    SAFE_RELEASE(m_pRasterizerStateSkybox);
    SAFE_RELEASE(m_pCubeTextureView);
    SAFE_RELEASE(m_pSkyboxView);
    SAFE_RELEASE(m_pSampler);
    SAFE_RELEASE(m_pSkyboxLayout);
    SAFE_RELEASE(m_pSkyboxPS);
    SAFE_RELEASE(m_pSkyboxVS);
    SAFE_RELEASE(m_pSkyboxIB);
    SAFE_RELEASE(m_pSkyboxVB);
    SAFE_RELEASE(m_pCubeLayout);
    SAFE_RELEASE(m_pCubePS);
    SAFE_RELEASE(m_pCubeVS);
    SAFE_RELEASE(m_pCubeIB);
    SAFE_RELEASE(m_pCubeVB);

    SAFE_RELEASE(m_pGeomBuffer);
    SAFE_RELEASE(m_pSceneBuffer);
    SAFE_RELEASE(m_pDepthStencilView);
    SAFE_RELEASE(m_pDepthStencilBuffer);
    SAFE_RELEASE(m_pBackBufferRTV);
    SAFE_RELEASE(m_pSwapChain);
    SAFE_RELEASE(m_pDeviceContext);
    SAFE_RELEASE(m_pDevice);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_SIZE:
        if (m_pSwapChain && m_pDevice && wParam != SIZE_MINIMIZED) {
            SAFE_RELEASE(m_pBackBufferRTV);
            SAFE_RELEASE(m_pDepthStencilView);
            SAFE_RELEASE(m_pDepthStencilBuffer);
            m_width = LOWORD(lParam);
            m_height = HIWORD(lParam);
            HRESULT hr = m_pSwapChain->ResizeBuffers(0, m_width, m_height, DXGI_FORMAT_UNKNOWN, 0);
            if (SUCCEEDED(hr)) {
                CreateRenderTarget();
            }
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"DX11Lesson", nullptr };
    RegisterClassEx(&wc);
    RECT rc = { 0, 0, (LONG)m_width, (LONG)m_height };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);

    HWND hWnd = CreateWindow(L"DX11Lesson", L"DirectX 11 3D Cube & Skybox", WS_OVERLAPPEDWINDOW,
        100, 100, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, wc.hInstance, nullptr);

    if (FAILED(InitDirectX(hWnd))) {
        Cleanup();
        return 0;
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);
    MSG msg = { 0 };
    bool exit = false;
    while (!exit) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) exit = true;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else {
            Render();
        }
    }

    Cleanup();
    return (int)msg.wParam;
}