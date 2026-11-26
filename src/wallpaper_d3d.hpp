#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "theme_css.hpp"

namespace phoenix_wallpaper {

struct DecodedImage {
    uint64_t version = 0;
    UINT width = 0;
    UINT height = 0;
    std::vector<uint8_t> rgba; // 4 bytes per pixel (R8G8B8A8)
};

struct DeviceWallpaperState {
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11Buffer* cb = nullptr;
    ID3D11SamplerState* sampler = nullptr;
    ID3D11BlendState* blendOpaque = nullptr;
    ID3D11DepthStencilState* depthDisabled = nullptr;
    ID3D11RasterizerState* rastNoCull = nullptr;
    ID3D11RasterizerState* rastScissor = nullptr;

    ID3D11Texture2D* tex = nullptr;
    ID3D11ShaderResourceView* srvUnorm = nullptr;
    ID3D11ShaderResourceView* srvSrgb = nullptr;
    uint64_t uploadedVersion = 0;
    UINT texWidth = 0;
    UINT texHeight = 0;
    bool pipelineReady = false;
    bool pipelineFailed = false;

    void ReleaseTexture() {
        if (srvSrgb) { srvSrgb->Release(); srvSrgb = nullptr; }
        if (srvUnorm) { srvUnorm->Release(); srvUnorm = nullptr; }
        if (tex) { tex->Release(); tex = nullptr; }
        uploadedVersion = 0;
        texWidth = 0;
        texHeight = 0;
    }

    void ReleasePipeline() {
        if (vs) { vs->Release(); vs = nullptr; }
        if (ps) { ps->Release(); ps = nullptr; }
        if (cb) { cb->Release(); cb = nullptr; }
        if (sampler) { sampler->Release(); sampler = nullptr; }
        if (blendOpaque) { blendOpaque->Release(); blendOpaque = nullptr; }
        if (depthDisabled) { depthDisabled->Release(); depthDisabled = nullptr; }
        if (rastNoCull) { rastNoCull->Release(); rastNoCull = nullptr; }
        if (rastScissor) { rastScissor->Release(); rastScissor = nullptr; }
        pipelineReady = false;
    }
};

struct WallpaperCB {
    float texWidth;
    float texHeight;
    float vpWidth;
    float vpHeight;
    float bgR;
    float bgG;
    float bgB;
    float opacity;
};

using PFN_D3DCompile = HRESULT(WINAPI*)(
    LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*,
    LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

namespace detail {

inline SRWLOCK g_wpLock = SRWLOCK_INIT;
inline DecodedImage g_cpuImage;
inline uint64_t g_nextVersion = 1;
inline std::unordered_map<ID3D11Device*, DeviceWallpaperState> g_devStates;

inline float SrgbToLinear(float c) {
    c = phoenix_theme::Clamp01(c);
    return (c <= 0.04045f) ? (c / 12.92f) : powf((c + 0.055f) / 1.055f, 2.4f);
}

inline PFN_D3DCompile ResolveD3DCompile() {
    static const PFN_D3DCompile fn = []() -> PFN_D3DCompile {
        HMODULE mod = GetModuleHandleW(L"d3dcompiler_47.dll");
        if (!mod) mod = LoadLibraryW(L"d3dcompiler_47.dll");
        if (!mod) return nullptr;
        return reinterpret_cast<PFN_D3DCompile>(GetProcAddress(mod, "D3DCompile"));
    }();
    return fn;
}

struct CompiledWallpaperShaders {
    std::vector<uint8_t> vsBytecode;
    std::vector<uint8_t> psBytecode;
    bool ok = false;
};

// Compiles the embedded HLSL shaders exactly once (thread-safe C++11 magic static, OUTSIDE g_wpLock!)
inline const CompiledWallpaperShaders& GetCompiledShaders() {
    static const CompiledWallpaperShaders compiled = []() -> CompiledWallpaperShaders {
        CompiledWallpaperShaders out{};
        auto compileFn = ResolveD3DCompile();
        if (!compileFn) return out;

        static const char* kHlsl = R"(
cbuffer WallpaperCB : register(b0) {
    float4 texAndVp;   // xy = tex size, zw = viewport size
    float4 bgAndAlpha; // rgb = base bg color, a = wallpaper opacity
};
Texture2D g_tex : register(t0);
SamplerState g_samp : register(s0);

struct VSOut {
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv = uv;
    o.pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    return o;
}

float4 PSMain(VSOut inp) : SV_Target {
    float texAspect = texAndVp.x / max(texAndVp.y, 1.0);
    float vpAspect  = texAndVp.z / max(texAndVp.w, 1.0);
    float2 uv = inp.uv;
    if (vpAspect > texAspect) {
        float scale = texAspect / vpAspect;
        uv.y = (uv.y - 0.5) * scale + 0.5;
    } else {
        float scale = vpAspect / texAspect;
        uv.x = (uv.x - 0.5) * scale + 0.5;
    }
    float4 sampleCol = g_tex.Sample(g_samp, uv);
    float3 blended = lerp(bgAndAlpha.rgb, sampleCol.rgb, saturate(bgAndAlpha.a));
    return float4(blended, 1.0);
}
)";

        ID3DBlob* vsBlob = nullptr;
        ID3DBlob* psBlob = nullptr;
        ID3DBlob* errBlob = nullptr;
        HRESULT hr1 = compileFn(kHlsl, strlen(kHlsl), "wallpaper.hlsl", nullptr, nullptr,
                                "VSMain", "vs_4_0", 0, 0, &vsBlob, &errBlob);
        if (errBlob) { errBlob->Release(); errBlob = nullptr; }
        HRESULT hr2 = compileFn(kHlsl, strlen(kHlsl), "wallpaper.hlsl", nullptr, nullptr,
                                "PSMain", "ps_4_0", 0, 0, &psBlob, &errBlob);
        if (errBlob) { errBlob->Release(); errBlob = nullptr; }

        if (SUCCEEDED(hr1) && SUCCEEDED(hr2) && vsBlob && psBlob) {
            const auto* vsBytes = reinterpret_cast<const uint8_t*>(vsBlob->GetBufferPointer());
            const auto* psBytes = reinterpret_cast<const uint8_t*>(psBlob->GetBufferPointer());
            out.vsBytecode.assign(vsBytes, vsBytes + vsBlob->GetBufferSize());
            out.psBytecode.assign(psBytes, psBytes + psBlob->GetBufferSize());
            out.ok = true;
        }
        if (vsBlob) vsBlob->Release();
        if (psBlob) psBlob->Release();
        return out;
    }();
    return compiled;
}

inline bool EnsurePipeline(ID3D11Device* dev, DeviceWallpaperState& st, const CompiledWallpaperShaders& shaders) {
    if (st.pipelineReady) return true;
    if (st.pipelineFailed || !shaders.ok) {
        st.pipelineFailed = true;
        return false;
    }

    HRESULT hr = dev->CreateVertexShader(shaders.vsBytecode.data(), shaders.vsBytecode.size(), nullptr, &st.vs);
    if (SUCCEEDED(hr)) {
        hr = dev->CreatePixelShader(shaders.psBytecode.data(), shaders.psBytecode.size(), nullptr, &st.ps);
    }
    if (SUCCEEDED(hr)) {
        D3D11_BUFFER_DESC cbd{};
        cbd.ByteWidth = sizeof(WallpaperCB);
        cbd.Usage = D3D11_USAGE_DEFAULT;
        cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = dev->CreateBuffer(&cbd, nullptr, &st.cb);
    }
    if (SUCCEEDED(hr)) {
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        hr = dev->CreateSamplerState(&sd, &st.sampler);
    }
    if (SUCCEEDED(hr)) {
        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].BlendEnable = FALSE;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        hr = dev->CreateBlendState(&bd, &st.blendOpaque);
    }
    if (SUCCEEDED(hr)) {
        D3D11_DEPTH_STENCIL_DESC dd{};
        dd.DepthEnable = FALSE;
        dd.StencilEnable = FALSE;
        hr = dev->CreateDepthStencilState(&dd, &st.depthDisabled);
    }
    if (SUCCEEDED(hr)) {
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        rd.ScissorEnable = FALSE;
        hr = dev->CreateRasterizerState(&rd, &st.rastNoCull);
        if (SUCCEEDED(hr)) {
            rd.ScissorEnable = TRUE;
            hr = dev->CreateRasterizerState(&rd, &st.rastScissor);
        }
    }

    if (SUCCEEDED(hr)) {
        st.pipelineReady = true;
        return true;
    }

    // Clean up all partially created COM objects on failure (zero COM leak)
    st.ReleasePipeline();
    st.pipelineFailed = true;
    return false;
}

} // namespace detail

// Decodes any PNG/JPG/BMP/WebP/GIF image into RGBA8 via native Windows Imaging Component (WIC).
inline bool LoadWallpaperFromFile(const std::wstring& path) {
    if (path.empty()) {
        AcquireSRWLockExclusive(&detail::g_wpLock);
        detail::g_cpuImage.version = 0;
        detail::g_cpuImage.width = 0;
        detail::g_cpuImage.height = 0;
        detail::g_cpuImage.rgba.clear();
        for (auto& kv : detail::g_devStates) {
            kv.second.ReleaseTexture();
        }
        ReleaseSRWLockExclusive(&detail::g_wpLock);
        return false;
    }

    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool shouldUninit = SUCCEEDED(hrInit);

    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    bool ok = false;

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr) && factory) {
        hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnLoad, &decoder);
    }
    if (SUCCEEDED(hr) && decoder) {
        hr = decoder->GetFrame(0, &frame);
    }
    if (SUCCEEDED(hr) && frame) {
        hr = factory->CreateFormatConverter(&converter);
    }
    if (SUCCEEDED(hr) && converter) {
        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    }
    if (SUCCEEDED(hr) && converter) {
        UINT w = 0, h = 0;
        if (SUCCEEDED(converter->GetSize(&w, &h)) && w > 0 && h > 0 && w <= 8192 && h <= 8192) {
            UINT stride = w * 4;
            std::vector<uint8_t> pixels(static_cast<size_t>(stride) * h);
            if (SUCCEEDED(converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data()))) {
                AcquireSRWLockExclusive(&detail::g_wpLock);
                detail::g_cpuImage.version = detail::g_nextVersion++;
                detail::g_cpuImage.width = w;
                detail::g_cpuImage.height = h;
                detail::g_cpuImage.rgba = std::move(pixels);
                ReleaseSRWLockExclusive(&detail::g_wpLock);
                ok = true;
            }
        }
    }

    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    if (shouldUninit) CoUninitialize();
    return ok;
}

inline bool HasLoadedWallpaper() {
    AcquireSRWLockShared(&detail::g_wpLock);
    bool has = (detail::g_cpuImage.version != 0 && !detail::g_cpuImage.rgba.empty());
    ReleaseSRWLockShared(&detail::g_wpLock);
    return has;
}

// Draws the loaded wallpaper (CSS background-size: cover) onto the bound render target,
// optionally clipped to dirty scissor rects (for ID3D11DeviceContext1::ClearView partial redraws),
// saving and restoring all touched D3D11 pipeline state so GPUI rendering is unaffected.
inline void RenderWallpaperToRtv(ID3D11DeviceContext* ctx,
                                 ID3D11RenderTargetView* rtv,
                                 const phoenix_theme::Theme& theme,
                                 float opacityOverride,
                                 const D3D11_RECT* scissorRects = nullptr,
                                 UINT numScissorRects = 0) {
    if (!ctx || !rtv) return;

    // Inspect target texture dimensions & format (only draw onto full-window RGBA/BGRA backbuffers)
    ID3D11Resource* res = nullptr;
    rtv->GetResource(&res);
    if (!res) return;
    D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    res->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        res->Release();
        return;
    }
    ID3D11Texture2D* rtTex = nullptr;
    if (FAILED(res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&rtTex))) || !rtTex) {
        res->Release();
        return;
    }
    D3D11_TEXTURE2D_DESC td{};
    rtTex->GetDesc(&td);
    rtTex->Release();
    res->Release();

    if (td.Width < 400 || td.Height < 300) return;

    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtv->GetDesc(&rtvDesc);
    DXGI_FORMAT effFmt = (rtvDesc.Format != DXGI_FORMAT_UNKNOWN) ? rtvDesc.Format : td.Format;
    const bool isSrgbRt = (effFmt == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
                           effFmt == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
    const bool isColorRt = isSrgbRt ||
                           (effFmt == DXGI_FORMAT_B8G8R8A8_UNORM ||
                            effFmt == DXGI_FORMAT_R8G8B8A8_UNORM);
    if (!isColorRt) return;

    // Compile HLSL outside g_wpLock so D3DCompile never stalls HasLoadedWallpaper/LoadWallpaperFromFile
    const auto& shaders = detail::GetCompiledShaders();
    if (!shaders.ok) return;

    ID3D11Device* dev = nullptr;
    ctx->GetDevice(&dev);
    if (!dev) return;

    // Upload CPU RGBA image to GPU texture if needed
    AcquireSRWLockExclusive(&detail::g_wpLock);
    if (detail::g_cpuImage.version == 0 || detail::g_cpuImage.rgba.empty()) {
        ReleaseSRWLockExclusive(&detail::g_wpLock);
        dev->Release();
        return;
    }
    auto& st = detail::g_devStates[dev];
    if (!detail::EnsurePipeline(dev, st, shaders)) {
        ReleaseSRWLockExclusive(&detail::g_wpLock);
        dev->Release();
        return;
    }
    if (st.uploadedVersion != detail::g_cpuImage.version) {
        st.ReleaseTexture();

        D3D11_TEXTURE2D_DESC itd{};
        itd.Width = detail::g_cpuImage.width;
        itd.Height = detail::g_cpuImage.height;
        itd.MipLevels = 1;
        itd.ArraySize = 1;
        itd.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        itd.SampleDesc.Count = 1;
        itd.Usage = D3D11_USAGE_IMMUTABLE;
        itd.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA init{};
        init.pSysMem = detail::g_cpuImage.rgba.data();
        init.SysMemPitch = detail::g_cpuImage.width * 4;

        if (SUCCEEDED(dev->CreateTexture2D(&itd, &init, &st.tex)) && st.tex) {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
            srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvd.Texture2D.MipLevels = 1;
            srvd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            HRESULT hrUnorm = dev->CreateShaderResourceView(st.tex, &srvd, &st.srvUnorm);
            srvd.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            HRESULT hrSrgb = dev->CreateShaderResourceView(st.tex, &srvd, &st.srvSrgb);
            if (SUCCEEDED(hrUnorm) && st.srvUnorm && SUCCEEDED(hrSrgb) && st.srvSrgb) {
                st.uploadedVersion = detail::g_cpuImage.version;
                st.texWidth = detail::g_cpuImage.width;
                st.texHeight = detail::g_cpuImage.height;
            } else {
                st.ReleaseTexture();
            }
        }
    }

    ID3D11VertexShader* wpVs = st.vs;
    ID3D11PixelShader* wpPs = st.ps;
    ID3D11Buffer* wpCb = st.cb;
    ID3D11SamplerState* wpSamp = st.sampler;
    ID3D11BlendState* wpBlend = st.blendOpaque;
    ID3D11DepthStencilState* wpDepth = st.depthDisabled;
    ID3D11RasterizerState* wpRast = st.rastNoCull;
    ID3D11RasterizerState* wpRastScissor = st.rastScissor;
    ID3D11ShaderResourceView* wpSrv = isSrgbRt ? st.srvSrgb : st.srvUnorm;
    UINT imgW = st.texWidth;
    UINT imgH = st.texHeight;
    if (wpSrv) wpSrv->AddRef();
    ReleaseSRWLockExclusive(&detail::g_wpLock);

    if (!wpSrv || imgW == 0 || imgH == 0) {
        if (wpSrv) wpSrv->Release();
        dev->Release();
        return;
    }

    // Save full D3D11 pipeline state (all 8 MRT RTVs, GS/HS/DS, viewports, scissor rects, etc.)
    ID3D11VertexShader* prevVs = nullptr;
    ID3D11PixelShader* prevPs = nullptr;
    ID3D11GeometryShader* prevGs = nullptr;
    ID3D11HullShader* prevHs = nullptr;
    ID3D11DomainShader* prevDs = nullptr;
    ID3D11InputLayout* prevIl = nullptr;
    D3D11_PRIMITIVE_TOPOLOGY prevTopo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11RasterizerState* prevRs = nullptr;
    D3D11_VIEWPORT prevVps[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    UINT prevVpCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_RECT prevScissors[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    UINT prevScissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ID3D11BlendState* prevBlend = nullptr;
    FLOAT prevBlendFactor[4] = {};
    UINT prevSampleMask = 0xFFFFFFFF;
    ID3D11DepthStencilState* prevDepth = nullptr;
    UINT prevStencilRef = 0;
    ID3D11RenderTargetView* prevRtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* prevDsv = nullptr;
    ID3D11ShaderResourceView* prevPsSrv[1] = {};
    ID3D11SamplerState* prevPsSamp[1] = {};
    ID3D11Buffer* prevPsCb[1] = {};

    ctx->VSGetShader(&prevVs, nullptr, nullptr);
    ctx->PSGetShader(&prevPs, nullptr, nullptr);
    ctx->GSGetShader(&prevGs, nullptr, nullptr);
    ctx->HSGetShader(&prevHs, nullptr, nullptr);
    ctx->DSGetShader(&prevDs, nullptr, nullptr);
    ctx->IAGetInputLayout(&prevIl);
    ctx->IAGetPrimitiveTopology(&prevTopo);
    ctx->RSGetState(&prevRs);
    ctx->RSGetViewports(&prevVpCount, prevVps);
    ctx->RSGetScissorRects(&prevScissorCount, prevScissors);
    ctx->OMGetBlendState(&prevBlend, prevBlendFactor, &prevSampleMask);
    ctx->OMGetDepthStencilState(&prevDepth, &prevStencilRef);
    ctx->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, prevRtvs, &prevDsv);
    ctx->PSGetShaderResources(0, 1, prevPsSrv);
    ctx->PSGetSamplers(0, 1, prevPsSamp);
    ctx->PSGetConstantBuffers(0, 1, prevPsCb);

    // Compute tint base color from active theme's bg_tertiary (or near-black)
    float bgR = 0.08f, bgG = 0.09f, bgB = 0.12f;
    if (theme.bg_tertiary.set) {
        phoenix_theme::HslaToRgb(theme.bg_tertiary.color, bgR, bgG, bgB);
    } else if (theme.bg_primary.set) {
        phoenix_theme::HslaToRgb(theme.bg_primary.color, bgR, bgG, bgB);
    }
    if (isSrgbRt) {
        bgR = detail::SrgbToLinear(bgR);
        bgG = detail::SrgbToLinear(bgG);
        bgB = detail::SrgbToLinear(bgB);
    }

    WallpaperCB cbData{};
    cbData.texWidth = static_cast<float>(imgW);
    cbData.texHeight = static_cast<float>(imgH);
    cbData.vpWidth = static_cast<float>(td.Width);
    cbData.vpHeight = static_cast<float>(td.Height);
    cbData.bgR = bgR;
    cbData.bgG = bgG;
    cbData.bgB = bgB;
    cbData.opacity = phoenix_theme::Clamp01(opacityOverride);
    ctx->UpdateSubresource(wpCb, 0, nullptr, &cbData, 0, 0);

    D3D11_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = static_cast<float>(td.Width);
    vp.Height = static_cast<float>(td.Height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    FLOAT zeroFactor[4] = {0, 0, 0, 0};
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->OMSetBlendState(wpBlend, zeroFactor, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(wpDepth, 0);
    ctx->RSSetViewports(1, &vp);
    ctx->IASetInputLayout(nullptr);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->GSSetShader(nullptr, nullptr, 0);
    ctx->HSSetShader(nullptr, nullptr, 0);
    ctx->DSSetShader(nullptr, nullptr, 0);
    ctx->VSSetShader(wpVs, nullptr, 0);
    ctx->PSSetShader(wpPs, nullptr, 0);
    ctx->PSSetShaderResources(0, 1, &wpSrv);
    ctx->PSSetSamplers(0, 1, &wpSamp);
    ctx->PSSetConstantBuffers(0, 1, &wpCb);
    if (scissorRects && numScissorRects > 0 && wpRastScissor) {
        ctx->RSSetState(wpRastScissor);
        for (UINT i = 0; i < numScissorRects; ++i) {
            if (scissorRects[i].right <= scissorRects[i].left ||
                scissorRects[i].bottom <= scissorRects[i].top) {
                continue;
            }
            ctx->RSSetScissorRects(1, &scissorRects[i]);
            ctx->Draw(3, 0);
        }
    } else {
        ctx->RSSetState(wpRast);
        ctx->Draw(3, 0);
    }

    // Restore saved D3D11 pipeline state cleanly (unconditional for viewports/scissors/MRT)
    ctx->PSSetShaderResources(0, 1, prevPsSrv);
    ctx->PSSetSamplers(0, 1, prevPsSamp);
    ctx->PSSetConstantBuffers(0, 1, prevPsCb);
    ctx->VSSetShader(prevVs, nullptr, 0);
    ctx->PSSetShader(prevPs, nullptr, 0);
    ctx->GSSetShader(prevGs, nullptr, 0);
    ctx->HSSetShader(prevHs, nullptr, 0);
    ctx->DSSetShader(prevDs, nullptr, 0);
    ctx->IASetInputLayout(prevIl);
    ctx->IASetPrimitiveTopology(prevTopo);
    ctx->RSSetState(prevRs);
    ctx->RSSetViewports(prevVpCount, prevVpCount > 0 ? prevVps : nullptr);
    ctx->RSSetScissorRects(prevScissorCount, prevScissorCount > 0 ? prevScissors : nullptr);
    ctx->OMSetBlendState(prevBlend, prevBlendFactor, prevSampleMask);
    ctx->OMSetDepthStencilState(prevDepth, prevStencilRef);
    ctx->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, prevRtvs, prevDsv);

    if (prevVs) prevVs->Release();
    if (prevPs) prevPs->Release();
    if (prevGs) prevGs->Release();
    if (prevHs) prevHs->Release();
    if (prevDs) prevDs->Release();
    if (prevIl) prevIl->Release();
    if (prevRs) prevRs->Release();
    if (prevBlend) prevBlend->Release();
    if (prevDepth) prevDepth->Release();
    for (auto* r : prevRtvs) {
        if (r) r->Release();
    }
    if (prevDsv) prevDsv->Release();
    if (prevPsSrv[0]) prevPsSrv[0]->Release();
    if (prevPsSamp[0]) prevPsSamp[0]->Release();
    if (prevPsCb[0]) prevPsCb[0]->Release();
    wpSrv->Release();
    dev->Release();
}

} // namespace phoenix_wallpaper
