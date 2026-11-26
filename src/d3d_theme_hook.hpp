#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <d3d11shader.h>
#include <dxgi1_2.h>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../third_party/minhook/include/MinHook.h"
#include "theme_css.hpp"
#include "wallpaper_d3d.hpp"

namespace phoenix_d3d {

// Verified against Windows SDK 10.0.26100.0 (um/d3d11.h, um/d3d11_1.h, um/d3d10.h, shared/dxgi.h, shared/dxgi1_2.h):
// ID3D11Device:         [3] CreateBuffer, [12] CreateVertexShader, [40] GetImmediateContext
// ID3D11DeviceContext:  [11] VSSetShader, [14] Map, [15] Unmap,
//                       [25] VSSetShaderResources, [50] ClearRenderTargetView
// ID3D11DeviceContext1: [132] ClearView
// ID3D10Multithread:    [5] SetMultithreadProtected
// IDXGIFactory2:        [15] CreateSwapChainForHwnd, [24] CreateSwapChainForComposition
// IDXGISwapChain:       [8] Present
constexpr size_t kIdxDevCreateBuffer = 3;
constexpr size_t kIdxDevCreateVertexShader = 12;
constexpr size_t kIdxDevGetImmediateContext = 40;
constexpr size_t kIdxCtxVSSetShader = 11;
constexpr size_t kIdxCtxMap = 14;
constexpr size_t kIdxCtxUnmap = 15;
constexpr size_t kIdxCtxVSSetShaderResources = 25;
constexpr size_t kIdxCtxClearRenderTargetView = 50;
constexpr size_t kIdxCtx1ClearView = 132;
constexpr size_t kIdxMtSetMultithreadProtected = 5;
constexpr size_t kIdxFactoryCreateSwapChainForHwnd = 15;
constexpr size_t kIdxFactoryCreateSwapChainForComposition = 24;
constexpr size_t kIdxSwapChainPresent = 8;

constexpr size_t kMaxDetourSlots = 16;

using LogFn = void (*)(const std::string&);

struct QuadLayout {
    UINT stride = 160;
    UINT offBounds = 8;
    UINT offContentMask = 24;
    UINT offBgTag = 40;
    UINT offBgSolid = 48;
    UINT offBgColor0 = 68;
    UINT offBgColor1 = 88;
    UINT offBorderColor = 112;
    UINT offCornerRadii = 128;
    bool reflected = false;
};

struct MonoLayout {
    UINT stride = 112;
    UINT offColor = 72;
    bool reflected = false;
};

enum class BufferKind : uint8_t {
    Unknown = 0,
    Quads = 1,
    MonoSprites = 2,
};

struct ActiveMapKey {
    ID3D11DeviceContext* ctx = nullptr;
    ID3D11Resource* res = nullptr;
    UINT subresource = 0;

    bool operator==(const ActiveMapKey& o) const {
        return ctx == o.ctx && res == o.res && subresource == o.subresource;
    }
};

struct ActiveMapKeyHash {
    size_t operator()(const ActiveMapKey& k) const {
        size_t h1 = std::hash<void*>{}(k.ctx);
        size_t h2 = std::hash<void*>{}(k.res);
        size_t h3 = std::hash<UINT>{}(k.subresource);
        return h1 ^ (h2 * 2654435761ULL) ^ (h3 * 40503ULL);
    }
};

struct ActiveMap {
    D3D11_MAPPED_SUBRESOURCE realMapped{};
    std::vector<uint8_t> staging;
    BufferKind kind = BufferKind::Unknown;
    UINT byteWidth = 0;
};

using PFN_D3D11CreateDevice = HRESULT(WINAPI*)(
    IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT, const D3D_FEATURE_LEVEL*, UINT, UINT,
    ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

using PFN_CreateBuffer = HRESULT(STDMETHODCALLTYPE*)(
    ID3D11Device*, const D3D11_BUFFER_DESC*, const D3D11_SUBRESOURCE_DATA*, ID3D11Buffer**);
using PFN_CreateVertexShader = HRESULT(STDMETHODCALLTYPE*)(
    ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*, ID3D11VertexShader**);
using PFN_GetImmediateContext = void(STDMETHODCALLTYPE*)(
    ID3D11Device*, ID3D11DeviceContext**);

using PFN_VSSetShader = void(STDMETHODCALLTYPE*)(
    ID3D11DeviceContext*, ID3D11VertexShader*, ID3D11ClassInstance* const*, UINT);
using PFN_Map = HRESULT(STDMETHODCALLTYPE*)(
    ID3D11DeviceContext*, ID3D11Resource*, UINT, D3D11_MAP, UINT, D3D11_MAPPED_SUBRESOURCE*);
using PFN_Unmap = void(STDMETHODCALLTYPE*)(
    ID3D11DeviceContext*, ID3D11Resource*, UINT);
using PFN_VSSetShaderResources = void(STDMETHODCALLTYPE*)(
    ID3D11DeviceContext*, UINT, UINT, ID3D11ShaderResourceView* const*);
using PFN_ClearRenderTargetView = void(STDMETHODCALLTYPE*)(
    ID3D11DeviceContext*, ID3D11RenderTargetView*, const FLOAT[4]);
using PFN_ClearView = void(STDMETHODCALLTYPE*)(
    ID3D11DeviceContext1*, ID3D11View*, const FLOAT[4], const D3D11_RECT*, UINT);
using PFN_SetMultithreadProtected = BOOL(STDMETHODCALLTYPE*)(
    IUnknown*, BOOL);
using PFN_CreateDXGIFactory1 = HRESULT(WINAPI*)(REFIID, void**);
using PFN_CreateDXGIFactory2 = HRESULT(WINAPI*)(UINT, REFIID, void**);
using PFN_CreateSwapChainForHwnd = HRESULT(STDMETHODCALLTYPE*)(
    IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
using PFN_CreateSwapChainForComposition = HRESULT(STDMETHODCALLTYPE*)(
    IDXGIFactory2*, IUnknown*, const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*, IDXGISwapChain1**);
using PFN_SwapChainPresent = HRESULT(STDMETHODCALLTYPE*)(
    IDXGISwapChain*, UINT, UINT);

using PFN_D3DReflect = HRESULT(WINAPI*)(LPCVOID, SIZE_T, REFIID, void**);

struct DevOrigVtbl {
    PFN_CreateBuffer createBuffer = nullptr;
    PFN_CreateVertexShader createVertexShader = nullptr;
    PFN_GetImmediateContext getImmediateContext = nullptr;
};

struct FactoryOrigVtbl {
    PFN_CreateSwapChainForHwnd createForHwnd = nullptr;
    PFN_CreateSwapChainForComposition createForComp = nullptr;
};

namespace detail {

// Unified RAII re-entrancy guard across ALL D3D11 hooks (prevents ClearView->ClearRTV double draw
// and ensures RenderWallpaperToRtv internal D3D calls never re-enter our detours).
inline thread_local int g_hookDepth = 0;

struct HookDepthGuard {
    HookDepthGuard() { ++g_hookDepth; }
    ~HookDepthGuard() { --g_hookDepth; }
};

inline SRWLOCK g_d3dLock = SRWLOCK_INIT;
inline SRWLOCK g_themeLock = SRWLOCK_INIT;
inline LogFn g_log = nullptr;
inline bool g_mhInitialized = false;

inline PFN_D3D11CreateDevice g_origD3D11CreateDevice = nullptr;
inline PFN_CreateDXGIFactory1 g_origCreateDXGIFactory1 = nullptr;
inline PFN_CreateDXGIFactory2 g_origCreateDXGIFactory2 = nullptr;

inline std::unordered_map<void**, DevOrigVtbl> g_devOrigs;
inline std::unordered_map<void**, PFN_SetMultithreadProtected> g_mtOrigs;
inline std::unordered_map<void**, FactoryOrigVtbl> g_factoryOrigs;
inline std::unordered_map<void**, PFN_SwapChainPresent> g_presentOrigs;

// MinHook detour slot tables for d3d11.dll CContext methods (immune to CContext vtable rewrites!)
inline std::unordered_set<void*> g_hookedMapFns;
inline std::unordered_set<void*> g_hookedUnmapFns;
inline std::unordered_set<void*> g_hookedVsFns;
inline std::unordered_set<void*> g_hookedSrvFns;
inline std::unordered_set<void*> g_hookedClearFns;
inline std::unordered_set<void*> g_hookedClearViewFns;
inline std::unordered_set<void*> g_failedHookFns;

inline std::atomic<PFN_Map> g_mapOrigs[kMaxDetourSlots] = {};
inline std::atomic<PFN_Unmap> g_unmapOrigs[kMaxDetourSlots] = {};
inline std::atomic<PFN_VSSetShader> g_vsOrigs[kMaxDetourSlots] = {};
inline std::atomic<PFN_VSSetShaderResources> g_srvOrigs[kMaxDetourSlots] = {};
inline std::atomic<PFN_ClearRenderTargetView> g_clearOrigs[kMaxDetourSlots] = {};
inline std::atomic<PFN_ClearView> g_clearViewOrigs[kMaxDetourSlots] = {};
inline size_t g_mapCount = 0;
inline size_t g_unmapCount = 0;
inline size_t g_vsCount = 0;
inline size_t g_srvCount = 0;
inline size_t g_clearCount = 0;
inline size_t g_clearViewCount = 0;
inline volatile LONG g_loggedClearView = 0;

// Protected by g_d3dLock
inline QuadLayout g_quadLayout;
inline MonoLayout g_monoLayout;

inline std::unordered_map<ID3D11VertexShader*, BufferKind> g_vsKinds;
inline std::unordered_map<ID3D11DeviceContext*, BufferKind> g_boundVsKind;
inline std::unordered_map<ID3D11Resource*, UINT> g_monoBufferByteWidths;
inline std::unordered_map<ActiveMapKey, ActiveMap, ActiveMapKeyHash> g_activeMaps;

inline phoenix_theme::Theme g_theme;
inline float g_effectiveWallpaperOpacity = 0.35f;
inline volatile LONG g_diagQuadLogs = 0;

// Per-swapchain backbuffer tracking: evicts stale backbuffer pointers on ResizeBuffers so
// recycled addresses can never be misclassified as a swapchain backbuffer.
inline std::unordered_map<IDXGISwapChain*, ID3D11Resource*> g_swapChainCurrentBB;
inline std::unordered_set<ID3D11Resource*> g_swapChainBackBuffers;
inline UINT g_viewportW = 0;
inline UINT g_viewportH = 0;

inline void Log(const std::string& msg) {
    if (g_log) g_log(msg);
}

inline void HookContextMethods(ID3D11DeviceContext* ctx);

inline void EnsureMinHookLocked() {
    if (!g_mhInitialized) {
        MH_STATUS st = MH_Initialize();
        if (st == MH_OK || st == MH_ERROR_ALREADY_INITIALIZED) {
            g_mhInitialized = true;
        }
    }
}

inline DevOrigVtbl GetDevOrig(ID3D11Device* self) {
    void** vtbl = *reinterpret_cast<void***>(self);
    AcquireSRWLockShared(&g_d3dLock);
    auto it = g_devOrigs.find(vtbl);
    DevOrigVtbl out = (it != g_devOrigs.end()) ? it->second : DevOrigVtbl{};
    ReleaseSRWLockShared(&g_d3dLock);
    return out;
}

inline QuadLayout GetQuadLayoutSnapshot() {
    AcquireSRWLockShared(&g_d3dLock);
    QuadLayout ql = g_quadLayout;
    ReleaseSRWLockShared(&g_d3dLock);
    return ql;
}

inline MonoLayout GetMonoLayoutSnapshot() {
    AcquireSRWLockShared(&g_d3dLock);
    MonoLayout ml = g_monoLayout;
    ReleaseSRWLockShared(&g_d3dLock);
    return ml;
}

inline PFN_D3DReflect ResolveD3DReflect() {
    static const PFN_D3DReflect fn = []() -> PFN_D3DReflect {
        HMODULE mod = GetModuleHandleW(L"d3dcompiler_47.dll");
        if (!mod) mod = LoadLibraryW(L"d3dcompiler_47.dll");
        if (!mod) return nullptr;
        return reinterpret_cast<PFN_D3DReflect>(GetProcAddress(mod, "D3DReflect"));
    }();
    return fn;
}

inline bool FindMemberOffset(ID3D11ShaderReflectionType* type, const char* name, UINT& outOffset, ID3D11ShaderReflectionType** outSub = nullptr) {
    if (!type || !name) return false;
    D3D11_SHADER_TYPE_DESC td{};
    if (FAILED(type->GetDesc(&td))) return false;
    for (UINT i = 0; i < td.Members; ++i) {
        const char* mname = type->GetMemberTypeName(i);
        if (mname && strcmp(mname, name) == 0) {
            auto* sub = type->GetMemberTypeByIndex(i);
            D3D11_SHADER_TYPE_DESC mtd{};
            if (sub && SUCCEEDED(sub->GetDesc(&mtd))) {
                outOffset = mtd.Offset;
                if (outSub) *outSub = sub;
                return true;
            }
        }
    }
    return false;
}

inline bool InBounds(UINT stride, UINT off, UINT sz) {
    return off <= stride && (stride - off) >= sz;
}

inline BufferKind InspectVertexShader(const void* bytecode, SIZE_T bytecodeLen) {
    auto reflectFn = ResolveD3DReflect();
    if (!reflectFn || !bytecode || bytecodeLen < 32) return BufferKind::Unknown;

    ID3D11ShaderReflection* refl = nullptr;
    static const GUID kIidShaderReflection = {
        0x8d536ca1, 0x0cca, 0x4956, {0xa8, 0x37, 0x78, 0x69, 0x63, 0x75, 0x55, 0x84}};
    if (FAILED(reflectFn(bytecode, bytecodeLen, kIidShaderReflection, reinterpret_cast<void**>(&refl))) || !refl) {
        return BufferKind::Unknown;
    }

    BufferKind detected = BufferKind::Unknown;
    D3D11_SHADER_DESC sdesc{};
    if (SUCCEEDED(refl->GetDesc(&sdesc))) {
        for (UINT i = 0; i < sdesc.BoundResources; ++i) {
            D3D11_SHADER_INPUT_BIND_DESC bdesc{};
            if (FAILED(refl->GetResourceBindingDesc(i, &bdesc)) || !bdesc.Name) continue;
            if (bdesc.Type != D3D_SIT_STRUCTURED) continue;

            if (strcmp(bdesc.Name, "quads") == 0) {
                detected = BufferKind::Quads;
                auto* cb = refl->GetConstantBufferByName("quads");
                if (cb) {
                    auto* var = cb->GetVariableByIndex(0);
                    D3D11_SHADER_VARIABLE_DESC vdesc{};
                    if (var && SUCCEEDED(var->GetDesc(&vdesc)) && vdesc.Size >= 128) {
                        QuadLayout ql = GetQuadLayoutSnapshot();
                        ql.stride = vdesc.Size;
                        auto* elemType = var->GetType();
                        UINT tmpOff = 0;
                        if (FindMemberOffset(elemType, "bounds", tmpOff)) ql.offBounds = tmpOff;
                        if (FindMemberOffset(elemType, "content_mask", tmpOff)) ql.offContentMask = tmpOff;
                        UINT bgOff = 0;
                        ID3D11ShaderReflectionType* bgType = nullptr;
                        if (FindMemberOffset(elemType, "background", bgOff, &bgType)) {
                            UINT subOff = 0;
                            if (FindMemberOffset(bgType, "tag", subOff)) ql.offBgTag = bgOff + subOff;
                            if (FindMemberOffset(bgType, "solid", subOff)) ql.offBgSolid = bgOff + subOff;
                            if (FindMemberOffset(bgType, "colors", subOff)) {
                                ql.offBgColor0 = bgOff + subOff;
                                ql.offBgColor1 = ql.offBgColor0 + 20;
                            }
                        }
                        if (FindMemberOffset(elemType, "border_color", tmpOff)) ql.offBorderColor = tmpOff;
                        if (FindMemberOffset(elemType, "corner_radii", tmpOff)) ql.offCornerRadii = tmpOff;

                        bool valid = InBounds(ql.stride, ql.offBounds, 16) &&
                                     InBounds(ql.stride, ql.offBgTag, 4) &&
                                     InBounds(ql.stride, ql.offBgSolid, 16) &&
                                     InBounds(ql.stride, ql.offBorderColor, 16) &&
                                     InBounds(ql.stride, ql.offCornerRadii, 16);
                        if (valid) {
                            bool firstReflect = false;
                            AcquireSRWLockExclusive(&g_d3dLock);
                            firstReflect = !g_quadLayout.reflected;
                            ql.reflected = true;
                            g_quadLayout = ql;
                            ReleaseSRWLockExclusive(&g_d3dLock);
                            if (firstReflect) {
                                Log("D3D11 reflected GPUI quads: stride=" + std::to_string(ql.stride) +
                                    " bounds=" + std::to_string(ql.offBounds) +
                                    " bgSolid=" + std::to_string(ql.offBgSolid));
                            }
                        }
                    }
                }
                break;
            } else if (strcmp(bdesc.Name, "mono_sprites") == 0) {
                detected = BufferKind::MonoSprites;
                auto* cb = refl->GetConstantBufferByName("mono_sprites");
                if (cb) {
                    auto* var = cb->GetVariableByIndex(0);
                    D3D11_SHADER_VARIABLE_DESC vdesc{};
                    if (var && SUCCEEDED(var->GetDesc(&vdesc)) && vdesc.Size >= 88) {
                        MonoLayout ml = GetMonoLayoutSnapshot();
                        ml.stride = vdesc.Size;
                        UINT tmpOff = 0;
                        if (FindMemberOffset(var->GetType(), "color", tmpOff)) ml.offColor = tmpOff;
                        if (InBounds(ml.stride, ml.offColor, 16)) {
                            AcquireSRWLockExclusive(&g_d3dLock);
                            ml.reflected = true;
                            g_monoLayout = ml;
                            ReleaseSRWLockExclusive(&g_d3dLock);
                        }
                    }
                }
                break;
            }
        }
    }

    refl->Release();
    return detected;
}

inline phoenix_theme::Theme GetActiveThemeSnapshot(float* outWallpaperOpacity = nullptr) {
    AcquireSRWLockShared(&g_themeLock);
    phoenix_theme::Theme copy = g_theme;
    if (outWallpaperOpacity) *outWallpaperOpacity = g_effectiveWallpaperOpacity;
    ReleaseSRWLockShared(&g_themeLock);
    return copy;
}

inline void TransformBufferData(BufferKind kind, uint8_t* data, UINT byteSize,
                                const phoenix_theme::Theme& theme, bool hasWallpaper) {
    if ((!theme.active && !hasWallpaper) || !data || byteSize == 0) return;

    if (kind == BufferKind::Quads) {
        const QuadLayout ql = GetQuadLayoutSnapshot();
        if (ql.stride < 128 ||
            !InBounds(ql.stride, ql.offBounds, 16) ||
            !InBounds(ql.stride, ql.offBgTag, 4) ||
            !InBounds(ql.stride, ql.offBgSolid, 16) ||
            !InBounds(ql.stride, ql.offBorderColor, 16) ||
            !InBounds(ql.stride, ql.offCornerRadii, 16)) {
            return;
        }
        const bool hasContentMask = InBounds(ql.stride, ql.offContentMask, 16);
        const bool hasGradientColors = InBounds(ql.stride, ql.offBgColor0, 16) &&
                                       InBounds(ql.stride, ql.offBgColor1, 16);
        const UINT count = byteSize / ql.stride;

        // Use exact swapchain viewport dimensions (immune to 10,000px tall scroll containers!)
        AcquireSRWLockShared(&g_d3dLock);
        float vpW = static_cast<float>(g_viewportW);
        float vpH = static_cast<float>(g_viewportH);
        ReleaseSRWLockShared(&g_d3dLock);
        if (vpW < 400.0f || vpH < 300.0f) {
            for (UINT i = 0; i < count; ++i) {
                float b[4] = {};
                memcpy(b, data + static_cast<size_t>(i) * ql.stride + ql.offBounds, sizeof(b));
                if (b[2] <= 4096.0f && b[3] <= 2560.0f) {
                    if (b[2] > vpW) vpW = b[2];
                    if (b[3] > vpH) vpH = b[3];
                }
            }
        }

        UINT touched = 0;
        UINT changedColors = 0;
        bool seenFullWindowRoot = false;
        bool wantLog = (InterlockedCompareExchange(&g_diagQuadLogs, 0, 0) > 0);
        std::string sampleDiag;

        for (UINT i = 0; i < count; ++i) {
            uint8_t* q = data + static_cast<size_t>(i) * ql.stride;

            float bounds[4] = {}; // origin.x, origin.y, size.x, size.y
            memcpy(bounds, q + ql.offBounds, sizeof(bounds));
            const float x = bounds[0], y = bounds[1], w = bounds[2], h = bounds[3];

            const bool isLargePanel = (w >= 96.0f && h >= 48.0f) ||
                                      (w >= 44.0f && h >= 200.0f) ||
                                      (w >= 200.0f && h >= 28.0f);

            phoenix_theme::QuadRole role = phoenix_theme::QuadRole::Default;
            bool isModalScrim = false;
            if (vpW >= 400.0f && vpH >= 300.0f) {
                if (w >= vpW * 0.85f && h >= vpH * 0.85f) {
                    // Only early window-root backgrounds (i < 8) are FullWindowRoot.
                    // Later full-screen quads (i >= 8) are modal/lightbox/settings dimming scrims —
                    // preserve their dark scrim alpha so opened images & settings stay visually focused!
                    if (i < 8) {
                        role = phoenix_theme::QuadRole::FullWindowRoot;
                    } else {
                        isModalScrim = true;
                    }
                } else if (x <= 12.0f && w >= 44.0f && w <= 96.0f && h >= vpH * 0.50f) {
                    role = phoenix_theme::QuadRole::ServerRail;
                } else if (w >= 140.0f && w <= 380.0f && h >= vpH * 0.50f &&
                           (x <= 120.0f || (x + w) >= vpW - 40.0f)) {
                    role = phoenix_theme::QuadRole::Sidebar;
                } else if (y <= 56.0f && w >= vpW * 0.45f && h >= 24.0f && h <= 64.0f) {
                    role = phoenix_theme::QuadRole::HeaderBar;
                } else if (w >= vpW * 0.45f && h >= vpH * 0.45f) {
                    role = phoenix_theme::QuadRole::MainPane;
                }
            }
            const bool isFullWindowRoot = (role == phoenix_theme::QuadRole::FullWindowRoot);

            // Seamless server rail: when Discord's vertical server rail ends above the floating
            // bottom-left user dock, extend its background rect to the bottom of the viewport
            // so no bright un-tinted gap pokes out around the rounded user dock corners.
            if (hasWallpaper && role == phoenix_theme::QuadRole::ServerRail &&
                vpH >= 300.0f && (y + h) < vpH - 4.0f && (y + h) >= vpH * 0.65f) {
                bounds[3] = vpH - y;
                memcpy(q + ql.offBounds, bounds, sizeof(bounds));
                if (hasContentMask) {
                    float mask[4] = {};
                    memcpy(mask, q + ql.offContentMask, sizeof(mask));
                    if (mask[2] > 0.0f && mask[3] > 0.0f && (mask[1] + mask[3]) < vpH - 4.0f &&
                        (mask[1] + mask[3]) >= (y + h) - 8.0f) {
                        mask[3] = vpH - mask[1];
                        memcpy(q + ql.offContentMask, mask, sizeof(mask));
                    }
                }
            }

            uint32_t tag = 0;
            memcpy(&tag, q + ql.offBgTag, sizeof(tag));

            if (tag == 0) {
                phoenix_theme::Hsla solid{};
                memcpy(&solid, q + ql.offBgSolid, sizeof(solid));
                if (solid.a > 0.005f && solid.a <= 1.001f) {
                    // Also treat semi-transparent full-screen overlays (e.g., lightbox/settings backdrop scrims)
                    // as modal scrims regardless of slot index so their dimming stays intact.
                    const bool effectiveFullRoot = isFullWindowRoot && (solid.a >= 0.96f);
                    const bool effectiveModalScrim = isModalScrim || (isFullWindowRoot && solid.a < 0.96f);
                    if (effectiveModalScrim) {
                        touched++;
                        continue;
                    }
                    bool isPrimaryFullRoot = false;
                    if (effectiveFullRoot && !seenFullWindowRoot) {
                        seenFullWindowRoot = true;
                        isPrimaryFullRoot = true;
                    }
                    phoenix_theme::Hsla before = solid;
                    if (theme.active) {
                        phoenix_theme::RemapQuadColor(theme, solid, isLargePanel, false, hasWallpaper, effectiveFullRoot, role);
                        if (hasWallpaper && isPrimaryFullRoot) {
                            solid.a = phoenix_theme::Clamp01(theme.panel_opacity * 0.38f);
                        }
                    } else if (hasWallpaper) {
                        if (effectiveFullRoot) {
                            solid.a = isPrimaryFullRoot ? phoenix_theme::Clamp01(theme.panel_opacity * 0.38f) : 0.0f;
                        } else if (role == phoenix_theme::QuadRole::MainPane) {
                            solid.a = (std::min)(solid.a, theme.panel_opacity * 0.25f);
                        } else if (role == phoenix_theme::QuadRole::ServerRail || role == phoenix_theme::QuadRole::Sidebar || role == phoenix_theme::QuadRole::HeaderBar) {
                            solid.a = (std::min)(solid.a, theme.panel_opacity * 0.42f);
                        } else if (isLargePanel && solid.a >= 0.35f) {
                            solid.a = (std::min)(solid.a, theme.panel_opacity);
                        }
                    }
                    if (before.h != solid.h || before.s != solid.s || before.l != solid.l || before.a != solid.a) {
                        changedColors++;
                    }
                    memcpy(q + ql.offBgSolid, &solid, sizeof(solid));
                    if (wantLog && touched < 4) {
                        char buf[160] = {};
                        snprintf(buf, sizeof(buf), " [q%u r%d %dx%d (%.2f,%.2f,%.2f,%.2f)->(%.2f,%.2f,%.2f,%.2f)]",
                                 i, static_cast<int>(role), static_cast<int>(w), static_cast<int>(h),
                                 before.h, before.s, before.l, before.a,
                                 solid.h, solid.s, solid.l, solid.a);
                        sampleDiag += buf;
                    }
                    touched++;
                }
            } else if (tag == 1 && hasGradientColors) {
                if (isModalScrim) {
                    touched++;
                    continue;
                }
                const bool isHugeGradient = (w >= 200.0f && h >= 100.0f) || (role != phoenix_theme::QuadRole::Default);
                phoenix_theme::Hsla c0{}, c1{};
                memcpy(&c0, q + ql.offBgColor0, sizeof(c0));
                memcpy(&c1, q + ql.offBgColor1, sizeof(c1));
                bool isPrimaryFullRoot = false;
                if (isFullWindowRoot && !seenFullWindowRoot && (c0.a > 0.005f || c1.a > 0.005f)) {
                    seenFullWindowRoot = true;
                    isPrimaryFullRoot = true;
                }
                if (c0.a > 0.005f && c0.a <= 1.001f) {
                    phoenix_theme::Hsla before = c0;
                    if (theme.active) {
                        phoenix_theme::RemapQuadColor(theme, c0, isLargePanel, isHugeGradient, hasWallpaper, isFullWindowRoot, role);
                        if (hasWallpaper && isPrimaryFullRoot) c0.a = phoenix_theme::Clamp01(theme.panel_opacity * 0.38f);
                    } else if (hasWallpaper && isFullWindowRoot) {
                        c0.a = isPrimaryFullRoot ? phoenix_theme::Clamp01(theme.panel_opacity * 0.38f) : 0.0f;
                    }
                    if (before.h != c0.h || before.s != c0.s || before.l != c0.l || before.a != c0.a) changedColors++;
                    memcpy(q + ql.offBgColor0, &c0, sizeof(c0));
                    touched++;
                }
                if (c1.a > 0.005f && c1.a <= 1.001f) {
                    phoenix_theme::Hsla before = c1;
                    if (theme.active) {
                        phoenix_theme::RemapQuadColor(theme, c1, isLargePanel, isHugeGradient, hasWallpaper, isFullWindowRoot, role);
                        if (hasWallpaper && isPrimaryFullRoot) c1.a = phoenix_theme::Clamp01(theme.panel_opacity * 0.38f);
                    } else if (hasWallpaper && isFullWindowRoot) {
                        c1.a = isPrimaryFullRoot ? phoenix_theme::Clamp01(theme.panel_opacity * 0.38f) : 0.0f;
                    }
                    if (before.h != c1.h || before.s != c1.s || before.l != c1.l || before.a != c1.a) changedColors++;
                    memcpy(q + ql.offBgColor1, &c1, sizeof(c1));
                    touched++;
                }
            }

            if (theme.active) {
                phoenix_theme::Hsla border{};
                memcpy(&border, q + ql.offBorderColor, sizeof(border));
                if (border.a > 0.005f && border.a <= 1.001f) {
                    phoenix_theme::RemapBorderColor(theme, border);
                    memcpy(q + ql.offBorderColor, &border, sizeof(border));
                }

                if (theme.has_radius_scale) {
                    float radii[4] = {};
                    memcpy(radii, q + ql.offCornerRadii, sizeof(radii));
                    bool changed = false;
                    for (int r = 0; r < 4; ++r) {
                        if (radii[r] > 0.5f && radii[r] < 24.0f) {
                            radii[r] *= theme.radius_scale;
                            changed = true;
                        }
                    }
                    if (changed) {
                        memcpy(q + ql.offCornerRadii, radii, sizeof(radii));
                    }
                }
            }
        }
        if (wantLog && touched > 0 && InterlockedDecrement(&g_diagQuadLogs) >= 0) {
            Log("D3D11 themed quads (slots=" + std::to_string(count) +
                ", touched=" + std::to_string(touched) +
                ", changed=" + std::to_string(changedColors) +
                ", wp=" + (hasWallpaper ? "1" : "0") + ")" + sampleDiag);
        }
    } else if (kind == BufferKind::MonoSprites && theme.active) {
        const MonoLayout ml = GetMonoLayoutSnapshot();
        if (ml.stride < 88 || !InBounds(ml.stride, ml.offColor, 16)) return;
        const UINT count = byteSize / ml.stride;
        for (UINT i = 0; i < count; ++i) {
            uint8_t* m = data + static_cast<size_t>(i) * ml.stride;
            phoenix_theme::Hsla col{};
            memcpy(&col, m + ml.offColor, sizeof(col));
            if (col.a > 0.005f && col.a <= 1.001f) {
                phoenix_theme::RemapMonoColor(theme, col);
                memcpy(m + ml.offColor, &col, sizeof(col));
            }
        }
    }
}

inline bool ClassifyBufferLive(ID3D11Resource* res, BufferKind& outKind, UINT& outByteWidth) {
    outKind = BufferKind::Unknown;
    outByteWidth = 0;
    if (!res) return false;

    D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    res->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_BUFFER) return false;

    ID3D11Buffer* buf = nullptr;
    if (FAILED(res->QueryInterface(__uuidof(ID3D11Buffer), reinterpret_cast<void**>(&buf))) || !buf) {
        return false;
    }
    D3D11_BUFFER_DESC d{};
    buf->GetDesc(&d);
    buf->Release();

    if (!(d.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED)) return false;
    if (d.ByteWidth == 0 || d.ByteWidth > 64 * 1024 * 1024) return false;

    const QuadLayout ql = GetQuadLayoutSnapshot();
    const MonoLayout ml = GetMonoLayoutSnapshot();

    if (d.StructureByteStride == ql.stride) {
        outKind = BufferKind::Quads;
        outByteWidth = d.ByteWidth;
        return true;
    }

    if (d.StructureByteStride == ml.stride) {
        AcquireSRWLockShared(&g_d3dLock);
        auto it = g_monoBufferByteWidths.find(res);
        bool isMono = (it != g_monoBufferByteWidths.end() && it->second == d.ByteWidth);
        ReleaseSRWLockShared(&g_d3dLock);
        if (isMono) {
            outKind = BufferKind::MonoSprites;
            outByteWidth = d.ByteWidth;
            return true;
        }
    }

    return false;
}

// ============================================================================
// Re-entrancy-safe MinHook detours for d3d11.dll CContext methods
// ============================================================================

inline HRESULT HandleMapCommon(
    PFN_Map origFn,
    ID3D11DeviceContext* self,
    ID3D11Resource* pResource,
    UINT subresource,
    D3D11_MAP mapType,
    UINT mapFlags,
    D3D11_MAPPED_SUBRESOURCE* pMappedResource) {
    if (!origFn) return E_FAIL;
    if (g_hookDepth > 0) {
        return origFn(self, pResource, subresource, mapType, mapFlags, pMappedResource);
    }
    HookDepthGuard guard;
    HRESULT hr = origFn(self, pResource, subresource, mapType, mapFlags, pMappedResource);
    if (SUCCEEDED(hr) && pResource && pMappedResource && mapType == D3D11_MAP_WRITE_DISCARD) {
        phoenix_theme::Theme theme = GetActiveThemeSnapshot();
        bool hasWp = phoenix_wallpaper::HasLoadedWallpaper();
        if (theme.active || hasWp) {
            BufferKind kind = BufferKind::Unknown;
            UINT byteWidth = 0;
            if (ClassifyBufferLive(pResource, kind, byteWidth)) {
                try {
                    ActiveMap am{};
                    am.realMapped = *pMappedResource;
                    am.kind = kind;
                    am.byteWidth = byteWidth;
                    am.staging.assign(byteWidth, 0);

                    ActiveMapKey key{self, pResource, subresource};
                    AcquireSRWLockExclusive(&g_d3dLock);
                    auto& slot = g_activeMaps[key];
                    slot = std::move(am);
                    pMappedResource->pData = slot.staging.data();
                    ReleaseSRWLockExclusive(&g_d3dLock);
                } catch (...) {
                    // Never allow C++ exceptions to cross COM boundary; fall back to un-staged mapping.
                }
            }
        }
    }
    return hr;
}

inline void HandleUnmapCommon(
    PFN_Unmap origFn,
    ID3D11DeviceContext* self,
    ID3D11Resource* pResource,
    UINT subresource) {
    if (!origFn) return;
    if (g_hookDepth > 0) {
        origFn(self, pResource, subresource);
        return;
    }
    HookDepthGuard guard;
    if (pResource) {
        ActiveMap am{};
        bool found = false;
        ActiveMapKey key{self, pResource, subresource};
        AcquireSRWLockExclusive(&g_d3dLock);
        auto it = g_activeMaps.find(key);
        if (it != g_activeMaps.end()) {
            am = std::move(it->second);
            g_activeMaps.erase(it);
            found = true;
        }
        ReleaseSRWLockExclusive(&g_d3dLock);

        if (found && am.realMapped.pData && !am.staging.empty()) {
            phoenix_theme::Theme theme = GetActiveThemeSnapshot();
            bool hasWp = phoenix_wallpaper::HasLoadedWallpaper();
            TransformBufferData(am.kind, am.staging.data(), am.byteWidth, theme, hasWp);
            memcpy(am.realMapped.pData, am.staging.data(), am.byteWidth);
        }
    }
    origFn(self, pResource, subresource);
}

inline void HandleVSSetShaderCommon(
    PFN_VSSetShader origFn,
    ID3D11DeviceContext* self,
    ID3D11VertexShader* pVertexShader,
    ID3D11ClassInstance* const* ppClassInstances,
    UINT numClassInstances) {
    if (!origFn) return;
    if (g_hookDepth > 0) {
        origFn(self, pVertexShader, ppClassInstances, numClassInstances);
        return;
    }
    HookDepthGuard guard;
    origFn(self, pVertexShader, ppClassInstances, numClassInstances);
    BufferKind kind = BufferKind::Unknown;
    if (pVertexShader) {
        AcquireSRWLockShared(&g_d3dLock);
        auto it = g_vsKinds.find(pVertexShader);
        if (it != g_vsKinds.end()) kind = it->second;
        ReleaseSRWLockShared(&g_d3dLock);
    }
    AcquireSRWLockExclusive(&g_d3dLock);
    g_boundVsKind[self] = kind;
    ReleaseSRWLockExclusive(&g_d3dLock);
}

inline void HandleVSSetSRVsCommon(
    PFN_VSSetShaderResources origFn,
    ID3D11DeviceContext* self,
    UINT startSlot,
    UINT numViews,
    ID3D11ShaderResourceView* const* ppShaderResourceViews) {
    if (!origFn) return;
    if (g_hookDepth > 0) {
        origFn(self, startSlot, numViews, ppShaderResourceViews);
        return;
    }
    HookDepthGuard guard;
    origFn(self, startSlot, numViews, ppShaderResourceViews);
    if (ppShaderResourceViews && numViews > 0 && startSlot <= 1 && (startSlot + numViews) > 1) {
        ID3D11ShaderResourceView* srv = ppShaderResourceViews[1 - startSlot];
        if (srv) {
            BufferKind bound = BufferKind::Unknown;
            AcquireSRWLockShared(&g_d3dLock);
            auto it = g_boundVsKind.find(self);
            if (it != g_boundVsKind.end()) bound = it->second;
            ReleaseSRWLockShared(&g_d3dLock);

            if (bound == BufferKind::MonoSprites) {
                const MonoLayout ml = GetMonoLayoutSnapshot();
                ID3D11Resource* res = nullptr;
                srv->GetResource(&res);
                if (res) {
                    D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
                    res->GetType(&dim);
                    if (dim == D3D11_RESOURCE_DIMENSION_BUFFER) {
                        ID3D11Buffer* buf = nullptr;
                        if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Buffer), reinterpret_cast<void**>(&buf))) && buf) {
                            D3D11_BUFFER_DESC d{};
                            buf->GetDesc(&d);
                            buf->Release();
                            if ((d.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) &&
                                d.StructureByteStride == ml.stride) {
                                AcquireSRWLockExclusive(&g_d3dLock);
                                g_monoBufferByteWidths[res] = d.ByteWidth;
                                ReleaseSRWLockExclusive(&g_d3dLock);
                            }
                        }
                    }
                    res->Release();
                }
            }
        }
    }
}

inline void HandleClearRtvCommon(
    PFN_ClearRenderTargetView origFn,
    ID3D11DeviceContext* self,
    ID3D11RenderTargetView* pRtv,
    const FLOAT colorRGBA[4]) {
    if (!origFn) return;
    if (g_hookDepth > 0) {
        origFn(self, pRtv, colorRGBA);
        return;
    }
    HookDepthGuard guard;
    origFn(self, pRtv, colorRGBA);
    if (pRtv && phoenix_wallpaper::HasLoadedWallpaper()) {
        // ONLY draw the wallpaper onto a real IDXGISwapChain backbuffer — NEVER onto
        // offscreen scroll/layer/blur/path render targets!
        ID3D11Resource* res = nullptr;
        pRtv->GetResource(&res);
        bool isSwapChainBackBuf = false;
        if (res) {
            AcquireSRWLockShared(&g_d3dLock);
            isSwapChainBackBuf = (g_swapChainBackBuffers.count(res) != 0);
            ReleaseSRWLockShared(&g_d3dLock);
            res->Release();
        }
        if (isSwapChainBackBuf) {
            float wpOpacity = 0.35f;
            phoenix_theme::Theme theme = GetActiveThemeSnapshot(&wpOpacity);
            phoenix_wallpaper::RenderWallpaperToRtv(self, pRtv, theme, wpOpacity);
        }
    }
}

inline void HandleClearViewCommon(
    PFN_ClearView origFn,
    ID3D11DeviceContext1* self,
    ID3D11View* pView,
    const FLOAT colorRGBA[4],
    const D3D11_RECT* pRects,
    UINT numRects) {
    if (!origFn) return;
    if (g_hookDepth > 0) {
        origFn(self, pView, colorRGBA, pRects, numRects);
        return;
    }
    HookDepthGuard guard;
    origFn(self, pView, colorRGBA, pRects, numRects);
    if (pView && phoenix_wallpaper::HasLoadedWallpaper()) {
        ID3D11RenderTargetView* rtv = nullptr;
        if (SUCCEEDED(pView->QueryInterface(__uuidof(ID3D11RenderTargetView), reinterpret_cast<void**>(&rtv))) && rtv) {
            ID3D11Resource* res = nullptr;
            rtv->GetResource(&res);
            bool isSwapChainBackBuf = false;
            if (res) {
                AcquireSRWLockShared(&g_d3dLock);
                isSwapChainBackBuf = (g_swapChainBackBuffers.count(res) != 0);
                ReleaseSRWLockShared(&g_d3dLock);
                res->Release();
            }
            if (isSwapChainBackBuf) {
                if (InterlockedCompareExchange(&g_loggedClearView, 1, 0) == 0) {
                    Log("ClearView dirty-rect wallpaper redraw active (numRects=" + std::to_string(numRects) + ")");
                }
                float wpOpacity = 0.35f;
                phoenix_theme::Theme theme = GetActiveThemeSnapshot(&wpOpacity);
                phoenix_wallpaper::RenderWallpaperToRtv(self, rtv, theme, wpOpacity, pRects, numRects);
            }
            rtv->Release();
        }
    }
}

template <size_t Slot>
HRESULT STDMETHODCALLTYPE MapDetour(
    ID3D11DeviceContext* self, ID3D11Resource* res, UINT sub, D3D11_MAP mt, UINT flags, D3D11_MAPPED_SUBRESOURCE* mapped) {
    return HandleMapCommon(g_mapOrigs[Slot].load(std::memory_order_acquire), self, res, sub, mt, flags, mapped);
}
template <size_t Slot>
void STDMETHODCALLTYPE UnmapDetour(ID3D11DeviceContext* self, ID3D11Resource* res, UINT sub) {
    HandleUnmapCommon(g_unmapOrigs[Slot].load(std::memory_order_acquire), self, res, sub);
}
template <size_t Slot>
void STDMETHODCALLTYPE VsDetour(
    ID3D11DeviceContext* self, ID3D11VertexShader* vs, ID3D11ClassInstance* const* ci, UINT nci) {
    HandleVSSetShaderCommon(g_vsOrigs[Slot].load(std::memory_order_acquire), self, vs, ci, nci);
}
template <size_t Slot>
void STDMETHODCALLTYPE SrvDetour(
    ID3D11DeviceContext* self, UINT startSlot, UINT numViews, ID3D11ShaderResourceView* const* srvs) {
    HandleVSSetSRVsCommon(g_srvOrigs[Slot].load(std::memory_order_acquire), self, startSlot, numViews, srvs);
}
template <size_t Slot>
void STDMETHODCALLTYPE ClearRtvDetour(
    ID3D11DeviceContext* self, ID3D11RenderTargetView* rtv, const FLOAT colorRGBA[4]) {
    HandleClearRtvCommon(g_clearOrigs[Slot].load(std::memory_order_acquire), self, rtv, colorRGBA);
}
template <size_t Slot>
void STDMETHODCALLTYPE ClearViewDetour(
    ID3D11DeviceContext1* self, ID3D11View* view, const FLOAT colorRGBA[4], const D3D11_RECT* rects, UINT numRects) {
    HandleClearViewCommon(g_clearViewOrigs[Slot].load(std::memory_order_acquire), self, view, colorRGBA, rects, numRects);
}

inline PFN_Map GetMapDetourFn(size_t i) {
    static constexpr PFN_Map kTable[kMaxDetourSlots] = {
        &MapDetour<0>, &MapDetour<1>, &MapDetour<2>, &MapDetour<3>,
        &MapDetour<4>, &MapDetour<5>, &MapDetour<6>, &MapDetour<7>,
        &MapDetour<8>, &MapDetour<9>, &MapDetour<10>, &MapDetour<11>,
        &MapDetour<12>, &MapDetour<13>, &MapDetour<14>, &MapDetour<15>,
    };
    return (i < kMaxDetourSlots) ? kTable[i] : nullptr;
}
inline PFN_Unmap GetUnmapDetourFn(size_t i) {
    static constexpr PFN_Unmap kTable[kMaxDetourSlots] = {
        &UnmapDetour<0>, &UnmapDetour<1>, &UnmapDetour<2>, &UnmapDetour<3>,
        &UnmapDetour<4>, &UnmapDetour<5>, &UnmapDetour<6>, &UnmapDetour<7>,
        &UnmapDetour<8>, &UnmapDetour<9>, &UnmapDetour<10>, &UnmapDetour<11>,
        &UnmapDetour<12>, &UnmapDetour<13>, &UnmapDetour<14>, &UnmapDetour<15>,
    };
    return (i < kMaxDetourSlots) ? kTable[i] : nullptr;
}
inline PFN_VSSetShader GetVsDetourFn(size_t i) {
    static constexpr PFN_VSSetShader kTable[kMaxDetourSlots] = {
        &VsDetour<0>, &VsDetour<1>, &VsDetour<2>, &VsDetour<3>,
        &VsDetour<4>, &VsDetour<5>, &VsDetour<6>, &VsDetour<7>,
        &VsDetour<8>, &VsDetour<9>, &VsDetour<10>, &VsDetour<11>,
        &VsDetour<12>, &VsDetour<13>, &VsDetour<14>, &VsDetour<15>,
    };
    return (i < kMaxDetourSlots) ? kTable[i] : nullptr;
}
inline PFN_VSSetShaderResources GetSrvDetourFn(size_t i) {
    static constexpr PFN_VSSetShaderResources kTable[kMaxDetourSlots] = {
        &SrvDetour<0>, &SrvDetour<1>, &SrvDetour<2>, &SrvDetour<3>,
        &SrvDetour<4>, &SrvDetour<5>, &SrvDetour<6>, &SrvDetour<7>,
        &SrvDetour<8>, &SrvDetour<9>, &SrvDetour<10>, &SrvDetour<11>,
        &SrvDetour<12>, &SrvDetour<13>, &SrvDetour<14>, &SrvDetour<15>,
    };
    return (i < kMaxDetourSlots) ? kTable[i] : nullptr;
}
inline PFN_ClearRenderTargetView GetClearDetourFn(size_t i) {
    static constexpr PFN_ClearRenderTargetView kTable[kMaxDetourSlots] = {
        &ClearRtvDetour<0>, &ClearRtvDetour<1>, &ClearRtvDetour<2>, &ClearRtvDetour<3>,
        &ClearRtvDetour<4>, &ClearRtvDetour<5>, &ClearRtvDetour<6>, &ClearRtvDetour<7>,
        &ClearRtvDetour<8>, &ClearRtvDetour<9>, &ClearRtvDetour<10>, &ClearRtvDetour<11>,
        &ClearRtvDetour<12>, &ClearRtvDetour<13>, &ClearRtvDetour<14>, &ClearRtvDetour<15>,
    };
    return (i < kMaxDetourSlots) ? kTable[i] : nullptr;
}
inline PFN_ClearView GetClearViewDetourFn(size_t i) {
    static constexpr PFN_ClearView kTable[kMaxDetourSlots] = {
        &ClearViewDetour<0>, &ClearViewDetour<1>, &ClearViewDetour<2>, &ClearViewDetour<3>,
        &ClearViewDetour<4>, &ClearViewDetour<5>, &ClearViewDetour<6>, &ClearViewDetour<7>,
        &ClearViewDetour<8>, &ClearViewDetour<9>, &ClearViewDetour<10>, &ClearViewDetour<11>,
        &ClearViewDetour<12>, &ClearViewDetour<13>, &ClearViewDetour<14>, &ClearViewDetour<15>,
    };
    return (i < kMaxDetourSlots) ? kTable[i] : nullptr;
}

template <typename FnType>
inline bool InstallDetourSlotLocked(
    void* targetFn,
    std::unordered_set<void*>& hookedSet,
    std::atomic<FnType>* origSlots,
    size_t& count,
    FnType (*getDetour)(size_t),
    const char* logPrefix = nullptr) {
    if (!targetFn || hookedSet.count(targetFn) || g_failedHookFns.count(targetFn) || count >= kMaxDetourSlots) {
        return false;
    }
    size_t slot = count;
    void* orig = nullptr;
    if (MH_CreateHook(targetFn, reinterpret_cast<void*>(getDetour(slot)), &orig) != MH_OK) {
        g_failedHookFns.insert(targetFn);
        return false;
    }
    // Publish trampoline pointer with release semantics BEFORE enabling hook so no thread can see nullptr!
    origSlots[slot].store(reinterpret_cast<FnType>(orig), std::memory_order_release);
    if (MH_EnableHook(targetFn) != MH_OK) {
        MH_RemoveHook(targetFn);
        origSlots[slot].store(nullptr, std::memory_order_release);
        g_failedHookFns.insert(targetFn);
        return false;
    }
    count++;
    hookedSet.insert(targetFn);
    if (logPrefix) {
        char buf[96] = {};
        snprintf(buf, sizeof(buf), "%s #%zu @ %p", logPrefix, count, targetFn);
        Log(buf);
    }
    return true;
}

inline bool PatchSlot(void** vtable, size_t idx, void* hookFn, void*& prevOut) {
    prevOut = vtable[idx];
    if (prevOut == hookFn) return true;
    DWORD oldProt = 0;
    if (!VirtualProtect(&vtable[idx], sizeof(void*), PAGE_READWRITE, &oldProt)) {
        return false;
    }
    vtable[idx] = hookFn;
    DWORD tmp = 0;
    VirtualProtect(&vtable[idx], sizeof(void*), oldProt, &tmp);
    return true;
}

inline void HookContextMethods(ID3D11DeviceContext* ctx) {
    if (!ctx) return;
    void** vtbl = *reinterpret_cast<void***>(ctx);
    void* fnMap = vtbl[kIdxCtxMap];
    void* fnUnmap = vtbl[kIdxCtxUnmap];
    void* fnVs = vtbl[kIdxCtxVSSetShader];
    void* fnSrv = vtbl[kIdxCtxVSSetShaderResources];
    void* fnClear = vtbl[kIdxCtxClearRenderTargetView];
    void* fnClearView = nullptr;
    ID3D11DeviceContext1* ctx1 = nullptr;
    if (SUCCEEDED(ctx->QueryInterface(__uuidof(ID3D11DeviceContext1), reinterpret_cast<void**>(&ctx1))) && ctx1) {
        void** vtbl1 = *reinterpret_cast<void***>(ctx1);
        fnClearView = vtbl1[kIdxCtx1ClearView];
        ctx1->Release();
    }

    auto isDone = [](void* fn, const std::unordered_set<void*>& set) {
        return !fn || set.count(fn) != 0 || g_failedHookFns.count(fn) != 0;
    };

    AcquireSRWLockShared(&g_d3dLock);
    bool allHandled = isDone(fnMap, g_hookedMapFns) &&
                      isDone(fnUnmap, g_hookedUnmapFns) &&
                      isDone(fnVs, g_hookedVsFns) &&
                      isDone(fnSrv, g_hookedSrvFns) &&
                      isDone(fnClear, g_hookedClearFns) &&
                      isDone(fnClearView, g_hookedClearViewFns);
    ReleaseSRWLockShared(&g_d3dLock);
    if (allHandled) return;

    AcquireSRWLockExclusive(&g_d3dLock);
    EnsureMinHookLocked();
    if (g_mhInitialized) {
        InstallDetourSlotLocked(fnMap, g_hookedMapFns, g_mapOrigs, g_mapCount, &GetMapDetourFn, "MinHook detoured d3d11 Map");
        InstallDetourSlotLocked(fnUnmap, g_hookedUnmapFns, g_unmapOrigs, g_unmapCount, &GetUnmapDetourFn);
        InstallDetourSlotLocked(fnVs, g_hookedVsFns, g_vsOrigs, g_vsCount, &GetVsDetourFn);
        InstallDetourSlotLocked(fnSrv, g_hookedSrvFns, g_srvOrigs, g_srvCount, &GetSrvDetourFn);
        InstallDetourSlotLocked(fnClear, g_hookedClearFns, g_clearOrigs, g_clearCount, &GetClearDetourFn);
        InstallDetourSlotLocked(fnClearView, g_hookedClearViewFns, g_clearViewOrigs, g_clearViewCount, &GetClearViewDetourFn);
    }
    ReleaseSRWLockExclusive(&g_d3dLock);
}

inline HRESULT STDMETHODCALLTYPE Hooked_CreateBuffer(
    ID3D11Device* self, const D3D11_BUFFER_DESC* desc, const D3D11_SUBRESOURCE_DATA* initData, ID3D11Buffer** ppBuffer) {
    DevOrigVtbl orig = GetDevOrig(self);
    if (!orig.createBuffer) return E_FAIL;
    HRESULT hr = orig.createBuffer(self, desc, initData, ppBuffer);
    if (g_hookDepth > 0) return hr;
    if (SUCCEEDED(hr) && desc && ppBuffer && *ppBuffer) {
        AcquireSRWLockExclusive(&g_d3dLock);
        g_monoBufferByteWidths.erase(*ppBuffer);
        ReleaseSRWLockExclusive(&g_d3dLock);
        if ((desc->MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) && orig.getImmediateContext) {
            ID3D11DeviceContext* ctx = nullptr;
            orig.getImmediateContext(self, &ctx);
            if (ctx) { HookContextMethods(ctx); ctx->Release(); }
        }
    }
    return hr;
}

inline HRESULT STDMETHODCALLTYPE Hooked_CreateVertexShader(
    ID3D11Device* self, const void* pShaderBytecode, SIZE_T bytecodeLength,
    ID3D11ClassLinkage* pClassLinkage, ID3D11VertexShader** ppVertexShader) {
    DevOrigVtbl orig = GetDevOrig(self);
    if (!orig.createVertexShader) return E_FAIL;
    HRESULT hr = orig.createVertexShader(self, pShaderBytecode, bytecodeLength, pClassLinkage, ppVertexShader);
    if (g_hookDepth > 0) return hr;
    if (SUCCEEDED(hr) && ppVertexShader && *ppVertexShader) {
        BufferKind kind = InspectVertexShader(pShaderBytecode, bytecodeLength);
        AcquireSRWLockExclusive(&g_d3dLock);
        if (kind != BufferKind::Unknown) {
            g_vsKinds[*ppVertexShader] = kind;
        } else {
            g_vsKinds.erase(*ppVertexShader);
        }
        ReleaseSRWLockExclusive(&g_d3dLock);
        if (kind != BufferKind::Unknown && orig.getImmediateContext) {
            ID3D11DeviceContext* ctx = nullptr;
            orig.getImmediateContext(self, &ctx);
            if (ctx) { HookContextMethods(ctx); ctx->Release(); }
        }
    }
    return hr;
}

inline void STDMETHODCALLTYPE Hooked_GetImmediateContext(ID3D11Device* self, ID3D11DeviceContext** ppImmediateContext) {
    DevOrigVtbl orig = GetDevOrig(self);
    if (!orig.getImmediateContext) return;
    orig.getImmediateContext(self, ppImmediateContext);
    if (g_hookDepth == 0 && ppImmediateContext && *ppImmediateContext) HookContextMethods(*ppImmediateContext);
}

inline BOOL STDMETHODCALLTYPE Hooked_SetMultithreadProtected(IUnknown* self, BOOL bMTProtect) {
    void** vtbl = *reinterpret_cast<void***>(self);
    PFN_SetMultithreadProtected orig = nullptr;
    AcquireSRWLockShared(&g_d3dLock);
    auto it = g_mtOrigs.find(vtbl);
    if (it != g_mtOrigs.end()) orig = it->second;
    ReleaseSRWLockShared(&g_d3dLock);
    BOOL prev = orig ? orig(self, bMTProtect) : FALSE;

    if (g_hookDepth > 0) return prev;
    ID3D11DeviceContext* ctx = nullptr;
    if (SUCCEEDED(self->QueryInterface(__uuidof(ID3D11DeviceContext), reinterpret_cast<void**>(&ctx))) && ctx) {
        HookContextMethods(ctx);
        ctx->Release();
    } else {
        ID3D11Device* dev = nullptr;
        if (SUCCEEDED(self->QueryInterface(__uuidof(ID3D11Device), reinterpret_cast<void**>(&dev))) && dev) {
            DevOrigVtbl devOrig = GetDevOrig(dev);
            if (devOrig.getImmediateContext) {
                devOrig.getImmediateContext(dev, &ctx);
                if (ctx) { HookContextMethods(ctx); ctx->Release(); }
            }
            dev->Release();
        }
    }
    return prev;
}

inline void HookMultithreadInterface(IUnknown* obj) {
    if (!obj) return;
    static const GUID kIidD3D10Multithread = {
        0x9B7E4E00, 0x342C, 0x4106, {0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0}};
    IUnknown* mt = nullptr;
    if (SUCCEEDED(obj->QueryInterface(kIidD3D10Multithread, reinterpret_cast<void**>(&mt))) && mt) {
        void** vtbl = *reinterpret_cast<void***>(mt);
        AcquireSRWLockExclusive(&g_d3dLock);
        if (g_mtOrigs.find(vtbl) == g_mtOrigs.end()) {
            void* prev = nullptr;
            if (PatchSlot(vtbl, kIdxMtSetMultithreadProtected, reinterpret_cast<void*>(&Hooked_SetMultithreadProtected), prev)) {
                g_mtOrigs[vtbl] = reinterpret_cast<PFN_SetMultithreadProtected>(prev);
            }
        }
        ReleaseSRWLockExclusive(&g_d3dLock);
        mt->Release();
    }
}

// Every vtable hook here is installed once per vtable. Another tool (Sunbird's
// desktop themes) may patch the same slot after us; re-hooking would save its
// hook as our "original" while it holds ours, and the two would recurse forever.
inline void HookDeviceVtable(ID3D11Device* dev) {
    if (!dev) return;
    void** vtbl = *reinterpret_cast<void***>(dev);
    AcquireSRWLockExclusive(&g_d3dLock);
    if (g_devOrigs.find(vtbl) == g_devOrigs.end()) {
        DevOrigVtbl orig{};
        void* p1 = nullptr;
        void* p2 = nullptr;
        void* p3 = nullptr;
        if (PatchSlot(vtbl, kIdxDevCreateBuffer, reinterpret_cast<void*>(&Hooked_CreateBuffer), p1) &&
            PatchSlot(vtbl, kIdxDevCreateVertexShader, reinterpret_cast<void*>(&Hooked_CreateVertexShader), p2) &&
            PatchSlot(vtbl, kIdxDevGetImmediateContext, reinterpret_cast<void*>(&Hooked_GetImmediateContext), p3)) {
            orig.createBuffer = reinterpret_cast<PFN_CreateBuffer>(p1);
            orig.createVertexShader = reinterpret_cast<PFN_CreateVertexShader>(p2);
            orig.getImmediateContext = reinterpret_cast<PFN_GetImmediateContext>(p3);
            g_devOrigs[vtbl] = orig;
            Log("Hooked ID3D11Device vtable");
        }
    }
    ReleaseSRWLockExclusive(&g_d3dLock);
    HookMultithreadInterface(dev);
}

inline void TrackSwapChainBackBuffer(IDXGISwapChain* sc) {
    if (!sc) return;
    ID3D11Texture2D* bb = nullptr;
    if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&bb))) && bb) {
        D3D11_TEXTURE2D_DESC td{};
        bb->GetDesc(&td);
        if (td.Width >= 400 && td.Height >= 300) {
            AcquireSRWLockExclusive(&g_d3dLock);
            auto prevIt = g_swapChainCurrentBB.find(sc);
            if (prevIt != g_swapChainCurrentBB.end() && prevIt->second != bb) {
                g_swapChainBackBuffers.erase(prevIt->second);
            }
            g_swapChainCurrentBB[sc] = bb;
            g_swapChainBackBuffers.insert(bb);
            // Update active viewport directly from the presenting main swapchain so shrink/un-maximize stays accurate
            if (td.Width >= 600 && td.Height >= 400) {
                g_viewportW = td.Width;
                g_viewportH = td.Height;
            }
            ReleaseSRWLockExclusive(&g_d3dLock);
        }
        bb->Release();
    }
}

inline HRESULT STDMETHODCALLTYPE Hooked_SwapChainPresent(IDXGISwapChain* self, UINT syncInterval, UINT flags) {
    void** vtbl = *reinterpret_cast<void***>(self);
    PFN_SwapChainPresent orig = nullptr;
    AcquireSRWLockShared(&g_d3dLock);
    auto it = g_presentOrigs.find(vtbl);
    if (it != g_presentOrigs.end()) orig = it->second;
    ReleaseSRWLockShared(&g_d3dLock);

    TrackSwapChainBackBuffer(self);

    ID3D11Device* dev = nullptr;
    if (SUCCEEDED(self->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&dev))) && dev) {
        ID3D11DeviceContext* ctx = nullptr;
        DevOrigVtbl devOrig = GetDevOrig(dev);
        if (devOrig.getImmediateContext) devOrig.getImmediateContext(dev, &ctx);
        else dev->GetImmediateContext(&ctx);
        if (ctx) { HookContextMethods(ctx); ctx->Release(); }
        dev->Release();
    }
    return orig ? orig(self, syncInterval, flags) : S_OK;
}

inline void HookSwapChainVtable(IDXGISwapChain* sc) {
    if (!sc) return;
    TrackSwapChainBackBuffer(sc);
    void** vtbl = *reinterpret_cast<void***>(sc);
    AcquireSRWLockExclusive(&g_d3dLock);
    if (g_presentOrigs.find(vtbl) == g_presentOrigs.end()) {
        void* prev = nullptr;
        if (PatchSlot(vtbl, kIdxSwapChainPresent, reinterpret_cast<void*>(&Hooked_SwapChainPresent), prev)) {
            g_presentOrigs[vtbl] = reinterpret_cast<PFN_SwapChainPresent>(prev);
        }
    }
    ReleaseSRWLockExclusive(&g_d3dLock);
}

inline HRESULT STDMETHODCALLTYPE Hooked_CreateSwapChainForHwnd(
    IDXGIFactory2* self, IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc,
    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc, IDXGIOutput* pRestrictToOutput,
    IDXGISwapChain1** ppSwapChain) {
    void** vtbl = *reinterpret_cast<void***>(self);
    FactoryOrigVtbl orig{};
    AcquireSRWLockShared(&g_d3dLock);
    auto it = g_factoryOrigs.find(vtbl);
    if (it != g_factoryOrigs.end()) orig = it->second;
    ReleaseSRWLockShared(&g_d3dLock);
    if (!orig.createForHwnd) return E_FAIL;

    HRESULT hr = orig.createForHwnd(self, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain) HookSwapChainVtable(*ppSwapChain);
    return hr;
}

inline HRESULT STDMETHODCALLTYPE Hooked_CreateSwapChainForComposition(
    IDXGIFactory2* self, IUnknown* pDevice, const DXGI_SWAP_CHAIN_DESC1* pDesc,
    IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain) {
    void** vtbl = *reinterpret_cast<void***>(self);
    FactoryOrigVtbl orig{};
    AcquireSRWLockShared(&g_d3dLock);
    auto it = g_factoryOrigs.find(vtbl);
    if (it != g_factoryOrigs.end()) orig = it->second;
    ReleaseSRWLockShared(&g_d3dLock);
    if (!orig.createForComp) return E_FAIL;

    HRESULT hr = orig.createForComp(self, pDevice, pDesc, pRestrictToOutput, ppSwapChain);
    if (SUCCEEDED(hr) && ppSwapChain && *ppSwapChain) HookSwapChainVtable(*ppSwapChain);
    return hr;
}

inline void HookDxgiFactory(IUnknown* unk) {
    if (!unk) return;
    IDXGIFactory2* f2 = nullptr;
    if (FAILED(unk->QueryInterface(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&f2))) || !f2) return;
    void** vtbl = *reinterpret_cast<void***>(f2);
    AcquireSRWLockExclusive(&g_d3dLock);
    if (g_factoryOrigs.find(vtbl) == g_factoryOrigs.end()) {
        FactoryOrigVtbl orig{};
        void* p1 = nullptr;
        void* p2 = nullptr;
        if (PatchSlot(vtbl, kIdxFactoryCreateSwapChainForHwnd, reinterpret_cast<void*>(&Hooked_CreateSwapChainForHwnd), p1) &&
            PatchSlot(vtbl, kIdxFactoryCreateSwapChainForComposition, reinterpret_cast<void*>(&Hooked_CreateSwapChainForComposition), p2)) {
            orig.createForHwnd = reinterpret_cast<PFN_CreateSwapChainForHwnd>(p1);
            orig.createForComp = reinterpret_cast<PFN_CreateSwapChainForComposition>(p2);
            g_factoryOrigs[vtbl] = orig;
        }
    }
    ReleaseSRWLockExclusive(&g_d3dLock);
    f2->Release();
}

inline HRESULT WINAPI Hooked_CreateDXGIFactory1(REFIID riid, void** ppFactory) {
    if (!g_origCreateDXGIFactory1) return E_FAIL;
    HRESULT hr = g_origCreateDXGIFactory1(riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory && *ppFactory) HookDxgiFactory(reinterpret_cast<IUnknown*>(*ppFactory));
    return hr;
}
inline HRESULT WINAPI Hooked_CreateDXGIFactory2(UINT flags, REFIID riid, void** ppFactory) {
    if (!g_origCreateDXGIFactory2) return E_FAIL;
    HRESULT hr = g_origCreateDXGIFactory2(flags, riid, ppFactory);
    if (SUCCEEDED(hr) && ppFactory && *ppFactory) HookDxgiFactory(reinterpret_cast<IUnknown*>(*ppFactory));
    return hr;
}

inline HRESULT WINAPI Hooked_D3D11CreateDevice(
    IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE driverType, HMODULE software, UINT flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels, UINT featureLevels, UINT sdkVersion,
    ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFeatureLevel, ID3D11DeviceContext** ppImmediateContext) {
    if (!g_origD3D11CreateDevice) return E_FAIL;
    HRESULT hr = g_origD3D11CreateDevice(
        pAdapter, driverType, software, flags, pFeatureLevels, featureLevels,
        sdkVersion, ppDevice, pFeatureLevel, ppImmediateContext);
    if (SUCCEEDED(hr) && ppDevice && *ppDevice) {
        HookDeviceVtable(*ppDevice);
        ID3D11DeviceContext* ctx = (ppImmediateContext && *ppImmediateContext) ? *ppImmediateContext : nullptr;
        ID3D11DeviceContext* ownedCtx = nullptr;
        if (!ctx) {
            DevOrigVtbl orig = GetDevOrig(*ppDevice);
            if (orig.getImmediateContext) orig.getImmediateContext(*ppDevice, &ownedCtx);
            ctx = ownedCtx;
        }
        if (ctx) {
            HookContextMethods(ctx);
            HookMultithreadInterface(ctx);
        }
        if (ownedCtx) ownedCtx->Release();
    }
    return hr;
}

} // namespace detail

inline void SetLogCallback(LogFn fn) { detail::g_log = fn; }

inline void SetActiveTheme(const phoenix_theme::Theme& theme, float wallpaperOpacity) {
    AcquireSRWLockExclusive(&detail::g_themeLock);
    detail::g_theme = theme;
    detail::g_effectiveWallpaperOpacity = wallpaperOpacity;
    ReleaseSRWLockExclusive(&detail::g_themeLock);
    if (theme.active || phoenix_wallpaper::HasLoadedWallpaper()) {
        InterlockedExchange(&detail::g_diagQuadLogs, 2);
    }
}

inline void* GetCreateDeviceHookPtr() { return reinterpret_cast<void*>(&detail::Hooked_D3D11CreateDevice); }
inline void** GetCreateDeviceOrigSlot() { return reinterpret_cast<void**>(&detail::g_origD3D11CreateDevice); }
inline void* GetCreateDxgiFactory1HookPtr() { return reinterpret_cast<void*>(&detail::Hooked_CreateDXGIFactory1); }
inline void** GetCreateDxgiFactory1OrigSlot() { return reinterpret_cast<void**>(&detail::g_origCreateDXGIFactory1); }
inline void* GetCreateDxgiFactory2HookPtr() { return reinterpret_cast<void*>(&detail::Hooked_CreateDXGIFactory2); }
inline void** GetCreateDxgiFactory2OrigSlot() { return reinterpret_cast<void**>(&detail::g_origCreateDXGIFactory2); }

} // namespace phoenix_d3d
