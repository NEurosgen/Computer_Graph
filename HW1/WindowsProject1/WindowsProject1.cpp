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
#include <algorithm>
#include <cmath>
#include <sys/stat.h>

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

ID3D11Texture2D* m_pColorTargetTexture = nullptr;
ID3D11RenderTargetView* m_pColorTargetRTV = nullptr;
ID3D11ShaderResourceView* m_pColorTargetSRV = nullptr;

ID3D11DepthStencilState* m_pDepthStateOpaque = nullptr;
ID3D11DepthStencilState* m_pDepthStateSkybox = nullptr;

ID3D11Buffer* m_pCubeVB = nullptr;
ID3D11Buffer* m_pCubeIB = nullptr;
ID3D11VertexShader* m_pCubeVS = nullptr;
ID3D11PixelShader* m_pCubePS = nullptr;
ID3D11InputLayout* m_pCubeLayout = nullptr;

ID3D11VertexShader* m_pPostVS = nullptr;
ID3D11PixelShader* m_pPostPS = nullptr;

ID3D11ShaderResourceView* m_pTextureArrayView = nullptr;
ID3D11ShaderResourceView* m_pNormalMapTextureView = nullptr;

ID3D11Buffer* m_pSkyboxVB = nullptr;
ID3D11Buffer* m_pSkyboxIB = nullptr;
ID3D11VertexShader* m_pSkyboxVS = nullptr;
ID3D11PixelShader* m_pSkyboxPS = nullptr;
ID3D11InputLayout* m_pSkyboxLayout = nullptr;
ID3D11ShaderResourceView* m_pSkyboxView = nullptr;
UINT m_skyboxIndexCount = 0;
ID3D11RasterizerState* m_pRasterizerStateSkybox = nullptr;

ID3D11Buffer* m_pSceneBuffer = nullptr;
ID3D11Buffer* m_pInstanceBuffer = nullptr;
ID3D11Buffer* m_pVisibilityBuffer = nullptr;
ID3D11SamplerState* m_pSampler = nullptr;

UINT m_width = 1280;
UINT m_height = 720;
ULONGLONG startTime = 0;
ULONGLONG lastTime = 0;

XMVECTOR camPosition = XMVectorSet(4.0f, 5.0f, -10.0f, 0.0f);
float camYaw = -0.4f;
float camPitch = 0.45f;

struct TextureVertex { float x, y, z; float tx, ty, tz; float nx, ny, nz; float u, v; };
struct SkyboxVertex { float x, y, z; };
struct Light { XMFLOAT4 pos; XMFLOAT4 color; };

struct SceneBuffer {
    XMMATRIX vp;
    XMVECTOR cameraPos;
    XMINT4 lightCount;
    Light lights[10];
    XMVECTOR ambientColor;
};

struct InstanceData {
    XMMATRIX model;
    XMFLOAT4 params;
};

struct VisibilityBuffer {
    XMUINT4 visibleIds[100];
};

struct AABB {
    XMFLOAT3 minExtents;
    XMFLOAT3 maxExtents;
};

struct Plane { XMFLOAT4 p; };

std::vector<InstanceData> g_Instances;
std::vector<AABB> g_InstanceAABBs;

struct TextureDesc {
    UINT32 pitch = 0;
    UINT32 mipmapsCount = 0;
    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
    UINT32 width = 0;
    UINT32 height = 0;
    void* pData = nullptr;
};

struct DDS_PIXELFORMAT {
    uint32_t dwSize; uint32_t dwFlags; uint32_t dwFourCC; uint32_t dwRGBBitCount;
    uint32_t dwRBitMask; uint32_t dwGBitMask; uint32_t dwBBitMask; uint32_t dwABitMask;
};

struct DDS_HEADER {
    uint32_t dwSize; uint32_t dwFlags; uint32_t dwHeight; uint32_t dwWidth;
    uint32_t dwPitchOrLinearSize; uint32_t dwDepth; uint32_t dwMipMapCount;
    uint32_t dwReserved1[11]; DDS_PIXELFORMAT ddspf; uint32_t dwCaps; uint32_t dwCaps2;
    uint32_t dwCaps3; uint32_t dwCaps4; uint32_t dwReserved2;
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
    else { desc.fmt = DXGI_FORMAT_B8G8R8A8_UNORM; }

    std::streamsize dataSize = size - sizeof(magic) - sizeof(header);
    desc.pData = new char[dataSize];
    file.read(reinterpret_cast<char*>(desc.pData), dataSize);
    return true;
}

HRESULT CreateTextureArraySRV(ID3D11Device* device, const std::vector<TextureDesc>& descs, bool isCubemap, ID3D11ShaderResourceView** ppSRV) {
    if (descs.empty()) return E_INVALIDARG;

    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = descs[0].width;
    texDesc.Height = descs[0].height;
    texDesc.MipLevels = descs[0].mipmapsCount;
    texDesc.ArraySize = isCubemap ? 6 : descs.size();
    texDesc.Format = descs[0].fmt;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_DEFAULT;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    texDesc.MiscFlags = isCubemap ? D3D11_RESOURCE_MISC_TEXTURECUBE : 0;

    std::vector<D3D11_SUBRESOURCE_DATA> initData(texDesc.ArraySize * texDesc.MipLevels);

    size_t cubemapOffset = 0;

    for (UINT arraySlice = 0; arraySlice < texDesc.ArraySize; ++arraySlice) {
        size_t offset = isCubemap ? cubemapOffset : 0;

        const TextureDesc& currentDesc = isCubemap ? descs[0] : descs[arraySlice];

        if (!isCubemap && (currentDesc.fmt != texDesc.Format || currentDesc.mipmapsCount != texDesc.MipLevels)) {
            OutputDebugStringA("Error: Texture array elements must have identical formats and mipmap counts.\n");
            return E_FAIL;
        }

        for (UINT mip = 0; mip < texDesc.MipLevels; ++mip) {
            UINT mipWidth = std::max(1u, currentDesc.width >> mip);
            UINT mipHeight = std::max(1u, currentDesc.height >> mip);
            UINT mipPitch = 0, mipLines = 0;

            if (currentDesc.fmt == DXGI_FORMAT_BC1_UNORM) {
                mipPitch = std::max(1u, (mipWidth + 3) / 4) * 8;
                mipLines = std::max(1u, (mipHeight + 3) / 4);
            }
            else if (currentDesc.fmt == DXGI_FORMAT_BC2_UNORM || currentDesc.fmt == DXGI_FORMAT_BC3_UNORM) {
                mipPitch = std::max(1u, (mipWidth + 3) / 4) * 16;
                mipLines = std::max(1u, (mipHeight + 3) / 4);
            }
            else {
                mipPitch = mipWidth * 4;
                mipLines = mipHeight;
            }

            UINT index = arraySlice * texDesc.MipLevels + mip;
            initData[index].pSysMem = static_cast<const char*>(currentDesc.pData) + offset;
            initData[index].SysMemPitch = mipPitch;

            offset += static_cast<size_t>(mipPitch) * mipLines;
        }

        if (isCubemap) {
            cubemapOffset = offset;
        }
    }

    ID3D11Texture2D* pTexture = nullptr;
    HRESULT hr = device->CreateTexture2D(&texDesc, initData.data(), &pTexture);
    if (FAILED(hr)) return hr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = texDesc.Format;
    srvDesc.ViewDimension = isCubemap ? D3D11_SRV_DIMENSION_TEXTURECUBE : (descs.size() > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DARRAY : D3D11_SRV_DIMENSION_TEXTURE2D);
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.MipLevels = texDesc.MipLevels;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize = texDesc.ArraySize;

    hr = device->CreateShaderResourceView(pTexture, &srvDesc, ppSRV);
    SAFE_RELEASE(pTexture);
    return hr;
}


const char* ShadersSource = R"(
struct Light { float4 pos; float4 color; };

cbuffer SceneBuffer : register(b0) {
    float4x4 vp;
    float4 cameraPos;
    int4 lightCount;
    Light lights[10];
    float4 ambientColor;
};

struct InstanceData {
    float4x4 model;
    float4 params;
};

cbuffer InstanceBuffer : register(b1) { InstanceData instances[100]; };
cbuffer VisibilityBuffer : register(b2) { uint4 visibleIds[100]; };

Texture2DArray colorTextureArray : register(t0);
Texture2DArray normalMapTextureArray : register(t1);
TextureCube skyboxTexture : register(t2); 
SamplerState colorSampler : register(s0);


struct VSCubeInput {
    float3 pos : POSITION;
    float3 tang : TANGENT;
    float3 norm : NORMAL;
    float2 uv : TEXCOORD;
    uint instanceId : SV_InstanceID;
};

struct VSCubeOutput {
    float4 pos : SV_Position;
    float4 worldPos : POSITION;
    float3 tang : TANGENT;
    float3 norm : NORMAL;
    float2 uv : TEXCOORD;
    nointerpolation uint instanceId : INST_ID;
};

VSCubeOutput vs_cube_inst(VSCubeInput vertex) {
    VSCubeOutput result;
    uint idx = visibleIds[vertex.instanceId].x;
    float4x4 model = instances[idx].model;
    
    float4 worldPos = mul(model, float4(vertex.pos, 1.0));
    result.worldPos = worldPos;
    result.pos = mul(vp, worldPos);
    
    result.norm = mul((float3x3)model, vertex.norm);
    result.tang = mul((float3x3)model, vertex.tang);
    result.uv = vertex.uv;
    result.instanceId = idx;
    return result;
}

float4 ps_cube_inst(VSCubeOutput pixel) : SV_Target0 {
    uint idx = pixel.instanceId;
    float texIndex = instances[idx].params.y;
    float shine = instances[idx].params.x;

    float4 texColor = colorTextureArray.Sample(colorSampler, float3(pixel.uv, texIndex));
    float3 color = texColor.xyz;
    float3 finalColor = ambientColor.xyz * color;

    float3 normal = normalize(pixel.norm);
    float3 tang = normalize(pixel.tang);
    tang = normalize(tang - dot(tang, normal) * normal);
    float3 binorm = cross(normal, tang);
    
    float3 sampledNorm = normalMapTextureArray.Sample(colorSampler, float3(pixel.uv, texIndex)).xyz;
    float3 localNorm;
    if (sampledNorm.x == 0 && sampledNorm.y == 0 && sampledNorm.z == 0) {
        localNorm = float3(0.0, 0.0, 1.0); 
    } else {
        localNorm = sampledNorm * 2.0 - 1.0;
    }
    normal = normalize(localNorm.x * tang + localNorm.y * binorm + localNorm.z * normal);

    for (int i = 0; i < lightCount.x; i++) {
        float3 lightDir = lights[i].pos.xyz - pixel.worldPos.xyz;
        float lightDist = length(lightDir);
        lightDir /= lightDist;
        float atten = clamp(15.0 / (lightDist * lightDist), 0.0, 1.0);
        
        finalColor += color * max(dot(lightDir, normal), 0.0) * atten * lights[i].color.xyz;
        float3 viewDir = normalize(cameraPos.xyz - pixel.worldPos.xyz);
        float3 reflectDir = reflect(-lightDir, normal);
        float spec = shine > 0 ? pow(max(dot(viewDir, reflectDir), 0.0), shine) : 0.0;
        finalColor += color * spec * atten * lights[i].color.xyz;
    }
    return float4(finalColor, 1.0);
}


struct VSSkyboxInput { float3 pos : POSITION; };
struct VSSkyboxOutput { float4 pos : SV_Position; float3 localPos : POSITION1; };

VSSkyboxOutput vs_skybox(VSSkyboxInput vertex) {
    VSSkyboxOutput result;
    float3 pos = cameraPos.xyz + vertex.pos * 50.0f;
    result.pos = mul(vp, float4(pos, 1.0));
    result.pos.z = 0.0f;
    result.localPos = vertex.pos;
    return result;
}

float4 ps_skybox(VSSkyboxOutput pixel) : SV_Target0 {
    return float4(skyboxTexture.Sample(colorSampler, pixel.localPos).xyz, 1.0);
}

Texture2D screenTexture : register(t0);

struct VSPostOutput { float4 pos : SV_Position; float2 uv : TEXCOORD; };

VSPostOutput vs_postprocess(uint vertexId : SV_VertexID) {
    VSPostOutput result;
    float2 texcoords = float2((vertexId << 1) & 2, vertexId & 2);
    result.pos = float4(texcoords * float2(2, -2) + float2(-1, 1), 0, 1);
    result.uv = texcoords;
    return result;
}

float4 ps_postprocess(VSPostOutput pixel) : SV_Target0 {
    float4 color = screenTexture.Sample(colorSampler, pixel.uv);
    float gray = dot(color.rgb, float3(0.299, 0.587, 0.114));
    return float4(gray, gray, gray, 1.0);
}
)";

HRESULT CreateRenderTargets() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    HRESULT result = m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    if (SUCCEEDED(result)) {
        result = m_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &m_pBackBufferRTV);
        SAFE_RELEASE(pBackBuffer);
    }

    SAFE_RELEASE(m_pColorTargetTexture);
    SAFE_RELEASE(m_pColorTargetRTV);
    SAFE_RELEASE(m_pColorTargetSRV);

    D3D11_TEXTURE2D_DESC colorDesc = {};
    colorDesc.Width = m_width;
    colorDesc.Height = m_height;
    colorDesc.MipLevels = 1;
    colorDesc.ArraySize = 1;
    colorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    colorDesc.SampleDesc.Count = 1;
    colorDesc.Usage = D3D11_USAGE_DEFAULT;
    colorDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    result = m_pDevice->CreateTexture2D(&colorDesc, nullptr, &m_pColorTargetTexture);
    if (SUCCEEDED(result)) {
        m_pDevice->CreateRenderTargetView(m_pColorTargetTexture, nullptr, &m_pColorTargetRTV);
        m_pDevice->CreateShaderResourceView(m_pColorTargetTexture, nullptr, &m_pColorTargetSRV);
    }

    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = m_width;
    depthDesc.Height = m_height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthDesc.SampleDesc.Count = 1;
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
            vertices.push_back({ sinf(phi) * cosf(theta), cosf(phi), sinf(phi) * sinf(theta) });
        }
    }
    vertices.push_back({ 0.0f, -1.0f, 0.0f });

    for (int i = 1; i <= longLines; ++i) { indices.push_back(0); indices.push_back(i + 1); indices.push_back(i); }

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
        indices.push_back(southPoleIndex); indices.push_back(baseIndex + i); indices.push_back(baseIndex + i + 1);
    }
}

inline bool FileExists(const std::wstring& name) {
    struct _stat buffer;
    return (_wstat(name.c_str(), &buffer) == 0);
}

std::wstring GetAssetPath(const std::wstring& filename) {
    wchar_t exePath[MAX_PATH]; GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring basePath(exePath);
    size_t lastSlash = basePath.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) basePath = basePath.substr(0, lastSlash + 1);
    std::wstring directPath = basePath + L"Assets\\" + filename;
    if (FileExists(directPath)) return directPath;

    std::wstring idePath = basePath;
    for (int i = 0; i < 2; ++i) {
        if (!idePath.empty() && (idePath.back() == L'\\' || idePath.back() == L'/')) idePath.pop_back();
        size_t slash = idePath.find_last_of(L"\\/");
        if (slash != std::wstring::npos) idePath = idePath.substr(0, slash + 1);
    }
    return idePath + L"Assets\\" + filename;
}


void ExtractPlanes(XMMATRIX vp, Plane planes[6]) {
    XMFLOAT4X4 m; XMStoreFloat4x4(&m, vp);
    planes[0].p = XMFLOAT4(m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41);
    planes[1].p = XMFLOAT4(m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41);
    planes[2].p = XMFLOAT4(m._14 + m._12, m._24 + m._21, m._34 + m._32, m._44 + m._42);
    planes[3].p = XMFLOAT4(m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42);
    planes[4].p = XMFLOAT4(m._13, m._23, m._33, m._43);
    planes[5].p = XMFLOAT4(m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43);

    for (int i = 0; i < 6; ++i) {
        float length = sqrtf(planes[i].p.x * planes[i].p.x + planes[i].p.y * planes[i].p.y + planes[i].p.z * planes[i].p.z);
        planes[i].p.x /= length; planes[i].p.y /= length; planes[i].p.z /= length; planes[i].p.w /= length;
    }
}

bool IsBoxInside(const Plane planes[6], const AABB& box) {
    for (int i = 0; i < 6; i++) {
        XMFLOAT3 norm = { planes[i].p.x, planes[i].p.y, planes[i].p.z };
        XMFLOAT3 p = {
            std::signbit(norm.x) ? box.minExtents.x : box.maxExtents.x,
            std::signbit(norm.y) ? box.minExtents.y : box.maxExtents.y,
            std::signbit(norm.z) ? box.minExtents.z : box.maxExtents.z
        };
        float d = p.x * norm.x + p.y * norm.y + p.z * norm.z + planes[i].p.w;
        if (d < 0.0f) return false;
    }
    return true;
}


HRESULT InitScene() {
    HRESULT hr = S_OK;

    D3D11_DEPTH_STENCIL_DESC dssDesc = {};
    dssDesc.DepthEnable = TRUE; dssDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; dssDesc.DepthFunc = D3D11_COMPARISON_GREATER;
    m_pDevice->CreateDepthStencilState(&dssDesc, &m_pDepthStateOpaque);

    dssDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO; dssDesc.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
    m_pDevice->CreateDepthStencilState(&dssDesc, &m_pDepthStateSkybox);

    static const TextureVertex CubeVertices[24] = {
        {-0.5f, -0.5f, -0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, -1.0f,   0.0f, 1.0f},
        { 0.5f, -0.5f, -0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, -1.0f,   1.0f, 1.0f},
        { 0.5f,  0.5f, -0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, -1.0f,   1.0f, 0.0f},
        {-0.5f,  0.5f, -0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, -1.0f,   0.0f, 0.0f},
        { 0.5f, -0.5f,  0.5f,  -1.0f, 0.0f, 0.0f,   0.0f, 0.0f,  1.0f,   0.0f, 1.0f},
        {-0.5f, -0.5f,  0.5f,  -1.0f, 0.0f, 0.0f,   0.0f, 0.0f,  1.0f,   1.0f, 1.0f},
        {-0.5f,  0.5f,  0.5f,  -1.0f, 0.0f, 0.0f,   0.0f, 0.0f,  1.0f,   1.0f, 0.0f},
        { 0.5f,  0.5f,  0.5f,  -1.0f, 0.0f, 0.0f,   0.0f, 0.0f,  1.0f,   0.0f, 0.0f},
        {-0.5f,  0.5f, -0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f,  0.0f,   0.0f, 1.0f},
        { 0.5f,  0.5f, -0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f,  0.0f,   1.0f, 1.0f},
        { 0.5f,  0.5f,  0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f,  0.0f,   1.0f, 0.0f},
        {-0.5f,  0.5f,  0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f,  0.0f,   0.0f, 0.0f},
        {-0.5f, -0.5f,  0.5f,   1.0f, 0.0f, 0.0f,   0.0f, -1.0f, 0.0f,   0.0f, 1.0f},
        { 0.5f, -0.5f,  0.5f,   1.0f, 0.0f, 0.0f,   0.0f, -1.0f, 0.0f,   1.0f, 1.0f},
        { 0.5f, -0.5f, -0.5f,   1.0f, 0.0f, 0.0f,   0.0f, -1.0f, 0.0f,   1.0f, 0.0f},
        {-0.5f, -0.5f, -0.5f,   1.0f, 0.0f, 0.0f,   0.0f, -1.0f, 0.0f,   0.0f, 0.0f},
        { 0.5f, -0.5f, -0.5f,   0.0f, 0.0f, 1.0f,   1.0f, 0.0f,  0.0f,   0.0f, 1.0f},
        { 0.5f, -0.5f,  0.5f,   0.0f, 0.0f, 1.0f,   1.0f, 0.0f,  0.0f,   1.0f, 1.0f},
        { 0.5f,  0.5f,  0.5f,   0.0f, 0.0f, 1.0f,   1.0f, 0.0f,  0.0f,   1.0f, 0.0f},
        { 0.5f,  0.5f, -0.5f,   0.0f, 0.0f, 1.0f,   1.0f, 0.0f,  0.0f,   0.0f, 0.0f},
        {-0.5f, -0.5f,  0.5f,   0.0f, 0.0f, -1.0f, -1.0f, 0.0f,  0.0f,   0.0f, 1.0f},
        {-0.5f, -0.5f, -0.5f,   0.0f, 0.0f, -1.0f, -1.0f, 0.0f,  0.0f,   1.0f, 1.0f},
        {-0.5f,  0.5f, -0.5f,   0.0f, 0.0f, -1.0f, -1.0f, 0.0f,  0.0f,   1.0f, 0.0f},
        {-0.5f,  0.5f,  0.5f,   0.0f, 0.0f, -1.0f, -1.0f, 0.0f,  0.0f,   0.0f, 0.0f}
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

    std::vector<SkyboxVertex> sphereVertices; std::vector<USHORT> sphereIndices;
    GenerateSphere(20, 20, sphereVertices, sphereIndices);
    m_skyboxIndexCount = static_cast<UINT>(sphereIndices.size());

    D3D11_BUFFER_DESC vbDescSky = { (UINT)(sphereVertices.size() * sizeof(SkyboxVertex)), D3D11_USAGE_IMMUTABLE, D3D11_BIND_VERTEX_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA vbDataSky = { sphereVertices.data(), 0, 0 };
    m_pDevice->CreateBuffer(&vbDescSky, &vbDataSky, &m_pSkyboxVB);

    D3D11_BUFFER_DESC ibDescSky = { (UINT)(sphereIndices.size() * sizeof(USHORT)), D3D11_USAGE_IMMUTABLE, D3D11_BIND_INDEX_BUFFER, 0, 0, 0 };
    D3D11_SUBRESOURCE_DATA ibDataSky = { sphereIndices.data(), 0, 0 };
    m_pDevice->CreateBuffer(&ibDescSky, &ibDataSky, &m_pSkyboxIB);


    D3D11_BUFFER_DESC sceneDesc = { sizeof(SceneBuffer), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
    m_pDevice->CreateBuffer(&sceneDesc, nullptr, &m_pSceneBuffer);

    D3D11_BUFFER_DESC instDesc = { sizeof(InstanceData) * 100, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
    m_pDevice->CreateBuffer(&instDesc, nullptr, &m_pInstanceBuffer);

    D3D11_BUFFER_DESC visDesc = { sizeof(VisibilityBuffer), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
    m_pDevice->CreateBuffer(&visDesc, nullptr, &m_pVisibilityBuffer);


    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    ID3DBlob* pVSBlob = nullptr; ID3DBlob* pPSBlob = nullptr; ID3DBlob* pErrorBlob = nullptr;

    D3DCompile(ShadersSource, strlen(ShadersSource), nullptr, nullptr, nullptr, "vs_cube_inst", "vs_5_0", flags, 0, &pVSBlob, &pErrorBlob);
    m_pDevice->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), nullptr, &m_pCubeVS);
    D3D11_INPUT_ELEMENT_DESC layoutCube[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    m_pDevice->CreateInputLayout(layoutCube, 4, pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), &m_pCubeLayout);
    SAFE_RELEASE(pVSBlob);

    D3DCompile(ShadersSource, strlen(ShadersSource), nullptr, nullptr, nullptr, "ps_cube_inst", "ps_5_0", flags, 0, &pPSBlob, &pErrorBlob);
    m_pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &m_pCubePS);
    SAFE_RELEASE(pPSBlob);

    D3DCompile(ShadersSource, strlen(ShadersSource), nullptr, nullptr, nullptr, "vs_skybox", "vs_5_0", flags, 0, &pVSBlob, &pErrorBlob);
    m_pDevice->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), nullptr, &m_pSkyboxVS);
    D3D11_INPUT_ELEMENT_DESC layoutSky[] = { {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0} };
    m_pDevice->CreateInputLayout(layoutSky, 1, pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), &m_pSkyboxLayout);
    SAFE_RELEASE(pVSBlob);

    D3DCompile(ShadersSource, strlen(ShadersSource), nullptr, nullptr, nullptr, "ps_skybox", "ps_5_0", flags, 0, &pPSBlob, &pErrorBlob);
    m_pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &m_pSkyboxPS);
    SAFE_RELEASE(pPSBlob);

    D3DCompile(ShadersSource, strlen(ShadersSource), nullptr, nullptr, nullptr, "vs_postprocess", "vs_5_0", flags, 0, &pVSBlob, &pErrorBlob);
    m_pDevice->CreateVertexShader(pVSBlob->GetBufferPointer(), pVSBlob->GetBufferSize(), nullptr, &m_pPostVS);
    SAFE_RELEASE(pVSBlob);

    D3DCompile(ShadersSource, strlen(ShadersSource), nullptr, nullptr, nullptr, "ps_postprocess", "ps_5_0", flags, 0, &pPSBlob, &pErrorBlob);
    m_pDevice->CreatePixelShader(pPSBlob->GetBufferPointer(), pPSBlob->GetBufferSize(), nullptr, &m_pPostPS);
    SAFE_RELEASE(pPSBlob);

    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_ANISOTROPIC; sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP; sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    sampDesc.MaxAnisotropy = 16; sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = -FLT_MAX; sampDesc.MaxLOD = FLT_MAX;
    m_pDevice->CreateSamplerState(&sampDesc, &m_pSampler);

    D3D11_RASTERIZER_DESC rastDesc = {}; rastDesc.FillMode = D3D11_FILL_SOLID; rastDesc.CullMode = D3D11_CULL_NONE;
    m_pDevice->CreateRasterizerState(&rastDesc, &m_pRasterizerStateSkybox);

    TextureDesc tex1, tex2;
    std::wstring path1 = GetAssetPath(L"vect.dds");
    std::wstring path2 = GetAssetPath(L"Brick.dds");


    if (LoadDDS(path1.c_str(), tex1, false) && LoadDDS(path2.c_str(), tex2, false)) {


        std::vector<TextureDesc> texArray = { tex1, tex2 };
        CreateTextureArraySRV(m_pDevice, texArray, false, &m_pTextureArrayView);

        delete[] static_cast<char*>(tex1.pData);
        delete[] static_cast<char*>(tex2.pData);
    }

    TextureDesc norm1, norm2;
    std::wstring normPath1 = GetAssetPath(L"normal.dds");
    std::wstring normPath2 = GetAssetPath(L"BrickNM.dds");

    if (LoadDDS(normPath1.c_str(), norm1, false) && LoadDDS(normPath2.c_str(), norm2, false)) {

        std::vector<TextureDesc> normalArray = { norm1, norm2 };
        CreateTextureArraySRV(m_pDevice, normalArray, false, &m_pNormalMapTextureView);

        delete[] static_cast<char*>(norm1.pData);
        delete[] static_cast<char*>(norm2.pData);
    }

    TextureDesc skyboxDesc;
    std::wstring skyboxPath = GetAssetPath(L"skybox.dds");
    if (LoadDDS(skyboxPath.c_str(), skyboxDesc, true)) {
        std::vector<TextureDesc> skyArray = { skyboxDesc };
        CreateTextureArraySRV(m_pDevice, skyArray, true, &m_pSkyboxView);
        delete[] static_cast<char*>(skyboxDesc.pData);
    }


    for (int i = 0; i < 10; ++i) {
        for (int j = 0; j < 10; ++j) {
            float x = (i - 5) * 2.5f;
            float z = (j - 5) * 2.5f;

            InstanceData inst;
            inst.model = XMMatrixTranslation(x, 0.0f, z);
            inst.params = XMFLOAT4(32.0f, (float)((i + j) % 2), 0.0f, 0.0f);
            g_Instances.push_back(inst);

            AABB box;
            box.minExtents = XMFLOAT3(x - 0.5f, -0.5f, z - 0.5f);
            box.maxExtents = XMFLOAT3(x + 0.5f, 0.5f, z + 0.5f);
            g_InstanceAABBs.push_back(box);
        }
    }
    m_pDeviceContext->UpdateSubresource(m_pInstanceBuffer, 0, nullptr, g_Instances.data(), 0, 0);

    return hr;
}

HRESULT InitDirectX(HWND hWnd) {
    HRESULT result;
    IDXGIFactory* pFactory = nullptr; CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&pFactory);
    IDXGIAdapter* pSelectedAdapter = nullptr; IDXGIAdapter* pAdapter = nullptr; UINT adapterIdx = 0;
    while (SUCCEEDED(pFactory->EnumAdapters(adapterIdx, &pAdapter))) {
        DXGI_ADAPTER_DESC desc; pAdapter->GetDesc(&desc);
        if (wcscmp(desc.Description, L"Microsoft Basic Render Driver") != 0) { pSelectedAdapter = pAdapter; break; }
        pAdapter->Release(); adapterIdx++;
    }

    D3D_FEATURE_LEVEL level; D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 }; UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    result = D3D11CreateDevice(pSelectedAdapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, flags, levels, 1, D3D11_SDK_VERSION, &m_pDevice, &level, &m_pDeviceContext);
    SAFE_RELEASE(pSelectedAdapter);

    DXGI_SWAP_CHAIN_DESC swapChainDesc = { 0 };
    swapChainDesc.BufferCount = 2; swapChainDesc.BufferDesc.Width = m_width; swapChainDesc.BufferDesc.Height = m_height;
    swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.OutputWindow = hWnd; swapChainDesc.SampleDesc.Count = 1; swapChainDesc.Windowed = true;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    result = pFactory->CreateSwapChain(m_pDevice, &swapChainDesc, &m_pSwapChain);
    SAFE_RELEASE(pFactory);

    result = CreateRenderTargets();
    startTime = GetTickCount64(); lastTime = startTime;
    return InitScene();
}

void Render() {
    ULONGLONG currentTime = GetTickCount64();
    float elapsedSec = (currentTime - startTime) / 1000.0f;
    float deltaTime = (currentTime - lastTime) / 1000.0f;
    lastTime = currentTime;

    m_pDeviceContext->ClearState();

    float speed = 5.0f * deltaTime; float rotSpeed = 2.0f * deltaTime;
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
    XMMATRIX proj = XMMatrixPerspectiveFovLH(XM_PI / 3.0f, (float)m_width / (float)m_height, 100.0f, 0.1f);
    XMMATRIX vp = XMMatrixMultiply(view, proj);


    D3D11_MAPPED_SUBRESOURCE subresource;
    if (SUCCEEDED(m_pDeviceContext->Map(m_pSceneBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &subresource))) {
        SceneBuffer* pSceneBuffer = reinterpret_cast<SceneBuffer*>(subresource.pData);
        pSceneBuffer->vp = vp; pSceneBuffer->cameraPos = camPosition;
        pSceneBuffer->ambientColor = XMVectorSet(0.1f, 0.1f, 0.1f, 1.0f); pSceneBuffer->lightCount.x = 1;
        pSceneBuffer->lights[0].pos = XMFLOAT4(sinf(elapsedSec) * 5.0f, 2.0f, cosf(elapsedSec) * 5.0f, 1.0f);
        pSceneBuffer->lights[0].color = XMFLOAT4(1.0f, 0.9f, 0.7f, 1.0f);
        m_pDeviceContext->Unmap(m_pSceneBuffer, 0);
    }

    Plane frustumPlanes[6]; ExtractPlanes(vp, frustumPlanes);
    VisibilityBuffer visBuffer; UINT visibleCount = 0;

    for (size_t i = 0; i < g_Instances.size(); ++i) {
        if (IsBoxInside(frustumPlanes, g_InstanceAABBs[i])) {
            visBuffer.visibleIds[visibleCount].x = (UINT)i;
            visibleCount++;
        }
    }
    m_pDeviceContext->UpdateSubresource(m_pVisibilityBuffer, 0, nullptr, &visBuffer, 0, 0);

    D3D11_VIEWPORT viewport = { 0.0f, 0.0f, (FLOAT)m_width, (FLOAT)m_height, 0.0f, 1.0f };
    m_pDeviceContext->RSSetViewports(1, &viewport);
    ID3D11SamplerState* samplers[] = { m_pSampler };
    m_pDeviceContext->PSSetSamplers(0, 1, samplers);


    ID3D11RenderTargetView* sceneViews[] = { m_pColorTargetRTV };
    m_pDeviceContext->OMSetRenderTargets(1, sceneViews, m_pDepthStencilView);
    static const FLOAT BackColor[4] = { 0.05f, 0.05f, 0.05f, 1.0f };
    m_pDeviceContext->ClearRenderTargetView(m_pColorTargetRTV, BackColor);
    m_pDeviceContext->ClearDepthStencilView(m_pDepthStencilView, D3D11_CLEAR_DEPTH, 0.0f, 0);

    ID3D11Buffer* constBuffers[] = { m_pSceneBuffer, m_pInstanceBuffer, m_pVisibilityBuffer };
    m_pDeviceContext->VSSetConstantBuffers(0, 3, constBuffers);
    m_pDeviceContext->PSSetConstantBuffers(0, 3, constBuffers);


    m_pDeviceContext->OMSetDepthStencilState(m_pDepthStateOpaque, 0);
    m_pDeviceContext->VSSetShader(m_pCubeVS, nullptr, 0);
    m_pDeviceContext->PSSetShader(m_pCubePS, nullptr, 0);
    UINT strideCube = sizeof(TextureVertex); UINT offsetCube = 0;
    m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pCubeVB, &strideCube, &offsetCube);
    m_pDeviceContext->IASetIndexBuffer(m_pCubeIB, DXGI_FORMAT_R16_UINT, 0);
    m_pDeviceContext->IASetInputLayout(m_pCubeLayout);
    m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D11ShaderResourceView* cubeRes[] = { m_pTextureArrayView, m_pNormalMapTextureView };
    m_pDeviceContext->PSSetShaderResources(0, 2, cubeRes);
    if (visibleCount > 0) m_pDeviceContext->DrawIndexedInstanced(36, visibleCount, 0, 0, 0);

    m_pDeviceContext->OMSetDepthStencilState(m_pDepthStateSkybox, 0);
    m_pDeviceContext->RSSetState(m_pRasterizerStateSkybox);
    ID3D11ShaderResourceView* skyboxRes[] = { m_pSkyboxView };
    m_pDeviceContext->PSSetShaderResources(2, 1, skyboxRes);
    m_pDeviceContext->VSSetShader(m_pSkyboxVS, nullptr, 0);
    m_pDeviceContext->PSSetShader(m_pSkyboxPS, nullptr, 0);
    m_pDeviceContext->IASetIndexBuffer(m_pSkyboxIB, DXGI_FORMAT_R16_UINT, 0);
    UINT strideSky = sizeof(SkyboxVertex); UINT offsetSky = 0;
    m_pDeviceContext->IASetVertexBuffers(0, 1, &m_pSkyboxVB, &strideSky, &offsetSky);
    m_pDeviceContext->IASetInputLayout(m_pSkyboxLayout);
    m_pDeviceContext->DrawIndexed(m_skyboxIndexCount, 0, 0);


    ID3D11ShaderResourceView* unbindSRV[] = { nullptr, nullptr, nullptr };
    m_pDeviceContext->PSSetShaderResources(0, 3, unbindSRV);

    ID3D11RenderTargetView* postViews[] = { m_pBackBufferRTV };
    m_pDeviceContext->OMSetRenderTargets(1, postViews, nullptr);

    m_pDeviceContext->RSSetState(nullptr);
    m_pDeviceContext->VSSetShader(m_pPostVS, nullptr, 0);
    m_pDeviceContext->PSSetShader(m_pPostPS, nullptr, 0);

    ID3D11ShaderResourceView* postRes[] = { m_pColorTargetSRV };
    m_pDeviceContext->PSSetShaderResources(0, 1, postRes);

    m_pDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_pDeviceContext->IASetInputLayout(nullptr);
    m_pDeviceContext->Draw(3, 0);

    m_pDeviceContext->PSSetShaderResources(0, 1, unbindSRV);
    m_pSwapChain->Present(1, 0);
}

void Cleanup() {
    if (m_pDeviceContext) m_pDeviceContext->ClearState();
    SAFE_RELEASE(m_pPostVS); SAFE_RELEASE(m_pPostPS);
    SAFE_RELEASE(m_pColorTargetSRV); SAFE_RELEASE(m_pColorTargetRTV); SAFE_RELEASE(m_pColorTargetTexture);
    SAFE_RELEASE(m_pInstanceBuffer); SAFE_RELEASE(m_pVisibilityBuffer);
    SAFE_RELEASE(m_pDepthStateOpaque); SAFE_RELEASE(m_pDepthStateSkybox);
    SAFE_RELEASE(m_pRasterizerStateSkybox); SAFE_RELEASE(m_pTextureArrayView); SAFE_RELEASE(m_pNormalMapTextureView);
    SAFE_RELEASE(m_pSkyboxView); SAFE_RELEASE(m_pSampler); SAFE_RELEASE(m_pSkyboxLayout);
    SAFE_RELEASE(m_pSkyboxPS); SAFE_RELEASE(m_pSkyboxVS); SAFE_RELEASE(m_pSkyboxIB); SAFE_RELEASE(m_pSkyboxVB);
    SAFE_RELEASE(m_pCubeLayout); SAFE_RELEASE(m_pCubePS); SAFE_RELEASE(m_pCubeVS); SAFE_RELEASE(m_pCubeIB); SAFE_RELEASE(m_pCubeVB);
    SAFE_RELEASE(m_pSceneBuffer); SAFE_RELEASE(m_pDepthStencilView); SAFE_RELEASE(m_pDepthStencilBuffer);
    SAFE_RELEASE(m_pBackBufferRTV); SAFE_RELEASE(m_pSwapChain); SAFE_RELEASE(m_pDeviceContext); SAFE_RELEASE(m_pDevice);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_SIZE:
        if (m_pSwapChain && m_pDevice && wParam != SIZE_MINIMIZED) {
            SAFE_RELEASE(m_pBackBufferRTV); SAFE_RELEASE(m_pDepthStencilView); SAFE_RELEASE(m_pDepthStencilBuffer);
            m_width = LOWORD(lParam); m_height = HIWORD(lParam);
            if (SUCCEEDED(m_pSwapChain->ResizeBuffers(0, m_width, m_height, DXGI_FORMAT_UNKNOWN, 0))) CreateRenderTargets();
        } return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"DX11Lesson", nullptr };
    RegisterClassEx(&wc);
    RECT rc = { 0, 0, (LONG)m_width, (LONG)m_height };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, TRUE);
    HWND hWnd = CreateWindow(L"DX11Lesson", L"DirectX 11 Instancing & PostProcess", WS_OVERLAPPEDWINDOW, 100, 100, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, wc.hInstance, nullptr);
    if (FAILED(InitDirectX(hWnd))) { Cleanup(); return 0; }
    ShowWindow(hWnd, nCmdShow); UpdateWindow(hWnd);

    MSG msg = { 0 }; bool exit = false;
    while (!exit) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) exit = true;
            TranslateMessage(&msg); DispatchMessage(&msg);
        }
        else Render();
    }
    Cleanup(); return (int)msg.wParam;
}