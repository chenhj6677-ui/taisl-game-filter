// Taisl 游戏助手 (Taisl Game Filter)
// 程序入口：窗口与 Direct3D 初始化、桌面抓帧、内嵌 HLSL 滤镜、配置读写、主循环
// Copyright (C) 2026 ikun-2dot5
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
// =============================================================
// Taisl游戏助手 (Game Filter)
//   ImGui + Direct3D 11 + DXGI 桌面复制
//   抓取当前显示器画面 -> 像素着色器调色 -> 全屏无边框窗口叠加输出
// =============================================================

#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

using std::min;
using std::max;

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

// -------------------------------------------------------------
// 滤镜参数
// -------------------------------------------------------------
struct FilterParams
{
    float brightness  = 0.0f;   // 亮度 -100 ~ 100
    float contrast    = 0.0f;   // 对比度 -100 ~ 100
    float saturation  = 0.0f;   // 饱和度 -100 ~ 100
    float temperature = 0.0f;   // 色温 -100(冷) ~ 100(暖)
    float tint        = 0.0f;   // 色调 -100(品红) ~ 100(绿)
    float gamma       = 0.0f;   // 伽马 -100 ~ 100
    float sharpen     = 0.0f;   // 清晰度 0 ~ 100
    bool  enabled     = true;   // 滤镜总开关
};

static const FilterParams kPresets[] = {
    {  6,  16,  18,  -6,   0,  12,  35, true },   // 明基风格：暗部提亮、适度饱和及锐化
    {  3,  14,  38,  -3,   0,   4,  22, true },   // 鲜艳：提升饱和度，适度对比、暗部提亮与锐化
    { -3,  10,  -5,  18,   0,  -6,  10, true },   // 电影：暖调低饱和
    { 12,   8,   6,  25,   0,  10,   5, true },   // 夜战：提亮 + 降蓝光
    {  5, -12,  -8,   5,   0,   0,   0, true },   // 柔和：低对比低饱和
};
static const char* kPresetNames[] = { "明基", "鲜艳", "电影", "夜战", "柔和" };
static const int   kPresetCount   = 5;

// -------------------------------------------------------------
// 全局状态
// -------------------------------------------------------------
static HWND                     g_hwnd = nullptr;
static RECT                     g_monRect = {};
static bool                     g_quit = false;
static bool                     g_panelVisible = true;
static bool                     g_captureOk = true;
static bool                     g_antiCaptureOk = false;
static bool                     g_hotkeyOk = true;      // 全局热键是否注册成功
static bool                     g_noExclude = false;   // --no-exclude: 不启用窗口防捕获（排查用）
static RECT                     g_panelRect = {};       // 面板区域（客户区像素）

static ID3D11Device*            g_dev = nullptr;
static ID3D11DeviceContext*     g_ctx = nullptr;
static IDXGISwapChain1*         g_swap = nullptr;
static ID3D11RenderTargetView*  g_rtv = nullptr;
static UINT                     g_bbW = 0, g_bbH = 0;

static IDXGIAdapter*            g_adapter = nullptr;
static IDXGIOutput*             g_output = nullptr;

static IDXGIOutputDuplication*  g_dup = nullptr;
static ID3D11Texture2D*         g_capTex = nullptr;
static ID3D11ShaderResourceView* g_capSRV = nullptr;
static UINT                     g_capW = 0, g_capH = 0;
static DWORD                    g_lastDupRetry = 0;

static ID3D11VertexShader*      g_vs = nullptr;
static ID3D11PixelShader*       g_ps = nullptr;
static ID3D11Buffer*            g_cb = nullptr;
static ID3D11SamplerState*      g_smp = nullptr;
static char                     g_shaderErr[512] = {};

static FilterParams             g_p = FilterParams();

static ImVec2 g_panelPos(-1.0f, -1.0f);
#include "crosshair.inl"
#include "settings_state.inl"

// 与着色器 cbuffer 一一对应（12 个 float = 48 字节，16 字节对齐）
struct GPUParams
{
    float brightness, contrast, saturation, temperature, tint, gamma, sharpen, texelX, texelY;
    float pad[3];
};

// -------------------------------------------------------------
// 着色器源码（内嵌，避免外部文件依赖）
// -------------------------------------------------------------
static const char* kShaderSrc = R"HLSL(
cbuffer Params : register(b0)
{
    float g_brightness;
    float g_contrast;
    float g_saturation;
    float g_temperature;
    float g_tint;
    float g_gamma;
    float g_sharpen;
    float g_texelX;
    float g_texelY;
    float3 g_pad;
};

Texture2D    g_tex  : register(t0);
SamplerState g_samp : register(s0);

struct VSOut
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

// 全屏三角形，不需要顶点缓冲
VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    o.uv  = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

float4 PSMain(VSOut i) : SV_TARGET
{
    float2 uv = i.uv;
    float3 c  = g_tex.SampleLevel(g_samp, uv, 0).rgb;

    // 清晰度：3x3 反锐化掩模
    if (g_sharpen > 0.001)
    {
        float2 t = float2(g_texelX, g_texelY);
        float3 blur = 0;
        blur += g_tex.SampleLevel(g_samp, uv + float2(-t.x, -t.y), 0).rgb;
        blur += g_tex.SampleLevel(g_samp, uv + float2( 0.0, -t.y), 0).rgb;
        blur += g_tex.SampleLevel(g_samp, uv + float2( t.x, -t.y), 0).rgb;
        blur += g_tex.SampleLevel(g_samp, uv + float2(-t.x,  0.0), 0).rgb;
        blur += g_tex.SampleLevel(g_samp, uv + float2( t.x,  0.0), 0).rgb;
        blur += g_tex.SampleLevel(g_samp, uv + float2(-t.x,  t.y), 0).rgb;
        blur += g_tex.SampleLevel(g_samp, uv + float2( 0.0,  t.y), 0).rgb;
        blur += g_tex.SampleLevel(g_samp, uv + float2( t.x,  t.y), 0).rgb;
        blur *= 0.125;
        c += (c - blur) * g_sharpen;
    }

    c += g_brightness;
    c  = (c - 0.5) * g_contrast + 0.5;

    float lum = dot(c, float3(0.2126, 0.7152, 0.0722));
    c = lerp(lum.xxx, c, g_saturation);

    c.r += g_temperature * 0.16;
    c.b -= g_temperature * 0.16;

    c.g += g_tint * 0.12;
    c.r -= g_tint * 0.05;
    c.b -= g_tint * 0.05;

    c = saturate(c);
    c = pow(c, g_gamma);

    return float4(c, 1.0);
}
)HLSL";

// -------------------------------------------------------------
// 工具函数
// -------------------------------------------------------------
static void SetWindowClickThrough(HWND hwnd, bool clickThrough)
{
    const LONG_PTR oldEx = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    LONG_PTR ex = oldEx;
    if (clickThrough)
        ex |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
    else
    {
        ex &= ~WS_EX_TRANSPARENT;
        if (g_ui.blockInput) ex &= ~WS_EX_NOACTIVATE;
        else ex |= WS_EX_NOACTIVATE;
    }
    if (ex == oldEx) return;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

#include "panel_input.inl"

static void ConfigPath(wchar_t* buf, int n)
{
    GetModuleFileNameW(nullptr, buf, n);
    wchar_t* p = wcsrchr(buf, L'\\');
    if (p) *(p + 1) = 0;
    lstrcatW(buf, L"filter.cfg");
}

static void SaveConfig() {
    wchar_t path[MAX_PATH];ConfigPath(path,MAX_PATH);
    std::wstring temporary=std::wstring(path)+L".tmp";
    const std::string contents=SerializeConfig();
    FILE* f=_wfopen(temporary.c_str(),L"wb");
    if(!f){g_configSaved=false;return;}
    bool ok=fwrite(contents.data(),1,contents.size(),f)==contents.size();
    if(fflush(f)!=0)ok=false;
    if(fclose(f)!=0)ok=false;
    if(ok)ok=MoveFileExW(temporary.c_str(),path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(ok){g_savedConfig=contents;g_configSaved=true;}
    else{DeleteFileW(temporary.c_str());g_configSaved=false;}
}
// Catch every setting, including preview selection and panel dragging. Mouse
// release commits a drag once rather than writing the config on every frame.
static void AutoSaveConfig() {
    if(!ImGui::IsMouseDown(0) && SerializeConfig()!=g_savedConfig) SaveConfig();
}

static void LoadConfig()
{
    wchar_t path[MAX_PATH];
    ConfigPath(path, MAX_PATH);
    FILE* f = _wfopen(path, L"r");
    if (!f) return;
    char key[64];
    float val;
    int en;
    while (fscanf(f, "%63[^=]=%f\n", key, &val) == 2)
    {
        if (!strcmp(key, "enabled"))                { en = (int)val; g_p.enabled = en != 0; }
        else if (!strcmp(key, "brightness"))         g_p.brightness = val;
        else if (!strcmp(key, "contrast"))           g_p.contrast = val;
        else if (!strcmp(key, "saturation"))         g_p.saturation = val;
        else if (!strcmp(key, "temperature"))        g_p.temperature = val;
        else if (!strcmp(key, "tint"))               g_p.tint = val;
        else if (!strcmp(key, "gamma"))              g_p.gamma = val;
        else if (!strcmp(key, "sharpen"))            g_p.sharpen = val;
        else if (!strcmp(key,"cross_enabled")) g_crosshair.enabled=val!=0;
        else if (!strcmp(key,"cross_centered")) g_crosshair.centered=val!=0;
        else if (!strcmp(key,"cross_dot")) g_crosshair.centerDot=val!=0;
        else if (!strcmp(key,"cross_outline")) g_crosshair.outline=val!=0;
        else if (!strcmp(key,"cross_style") && std::isfinite(val)) g_crosshair.style=(int)max(0.0f,min(4.0f,val));
        else if (!strcmp(key,"cross_size")) g_crosshair.size=val;
        else if (!strcmp(key,"cross_thickness")) g_crosshair.thickness=val;
        else if (!strcmp(key,"cross_gap")) g_crosshair.gap=val;
        else if (!strcmp(key,"cross_x")) g_crosshair.x=val;
        else if (!strcmp(key,"cross_y")) g_crosshair.y=val;
        else if (!strcmp(key,"cross_r")) g_crosshair.color[0]=val;
        else if (!strcmp(key,"cross_g")) g_crosshair.color[1]=val;
        else if (!strcmp(key,"cross_b")) g_crosshair.color[2]=val;
        else if (!strcmp(key,"cross_a")) g_crosshair.color[3]=val;
        else if (!strcmp(key,"ui_theme") && std::isfinite(val)) g_ui.theme=(int)max(0.0f,min(1.0f,val));
        else if (!strcmp(key,"ui_blur")) g_ui.blur=val!=0;
        else if (!strcmp(key,"ui_block_input")) g_ui.blockInput=val!=0;
        else if (!strcmp(key,"ui_sample") && std::isfinite(val)) g_requestedSample=(int)max(0.0f,min(1.0f,val));
        else if (!strcmp(key,"ui_page") && std::isfinite(val)) g_activePage=(int)max(0.0f,min(2.0f,val));
        else if (!strcmp(key,"ui_panel_x") && std::isfinite(val)) g_panelPos.x=max(-1.0f,val);
        else if (!strcmp(key,"ui_panel_y") && std::isfinite(val)) g_panelPos.y=max(-1.0f,val);
    }
    g_p.brightness=CrosshairClamp(g_p.brightness,-100,100,0);
    g_p.contrast=CrosshairClamp(g_p.contrast,-100,100,0);
    g_p.saturation=CrosshairClamp(g_p.saturation,-100,100,0);
    g_p.temperature=CrosshairClamp(g_p.temperature,-100,100,0);
    g_p.tint=CrosshairClamp(g_p.tint,-100,100,0);
    g_p.gamma=CrosshairClamp(g_p.gamma,-100,100,0);
    g_p.sharpen=CrosshairClamp(g_p.sharpen,0,100,0);
    ValidateCrosshair();
    g_savedConfig=SerializeConfig();
    fclose(f);
}

static void ApplyPreset(int idx)
{
    if (idx < 0 || idx >= kPresetCount) return;
    bool en = g_p.enabled;
    g_p = kPresets[idx];
    g_p.enabled = en;
    SaveConfig();
}

static void ResetFilter()
{
    const bool enabled = g_p.enabled;
    g_p = FilterParams();
    g_p.enabled = enabled;
    SaveConfig();
}

// -------------------------------------------------------------
// 窗口 / D3D 初始化
// -------------------------------------------------------------
static bool PickAdapterOutput(IDXGIFactory2* factory)
{
    for (UINT a = 0; ; ++a)
    {
        IDXGIAdapter1* ad = nullptr;
        if (factory->EnumAdapters1(a, &ad) == DXGI_ERROR_NOT_FOUND) break;

        for (UINT o = 0; ; ++o)
        {
            IDXGIOutput* out = nullptr;
            if (ad->EnumOutputs(o, &out) == DXGI_ERROR_NOT_FOUND) break;

            DXGI_OUTPUT_DESC d;
            if (SUCCEEDED(out->GetDesc(&d)) &&
                d.DesktopCoordinates.left == g_monRect.left &&
                d.DesktopCoordinates.top  == g_monRect.top)
            {
                g_adapter = ad;
                g_output  = out;
                return true;
            }
            out->Release();
        }
        ad->Release();
    }
    return false;
}

static bool CreateDeviceAndSwapChain(IDXGIFactory2* factory)
{
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDevice(g_adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, _countof(levels),
                                   D3D11_SDK_VERSION, &g_dev, &got, &g_ctx);
    if (FAILED(hr)) return false;

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width            = g_monRect.right - g_monRect.left;
    sd.Height           = g_monRect.bottom - g_monRect.top;
    sd.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount      = 2;
    sd.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.AlphaMode        = DXGI_ALPHA_MODE_IGNORE;
    sd.Scaling          = DXGI_SCALING_STRETCH;

    hr = factory->CreateSwapChainForHwnd(g_dev, g_hwnd, &sd, nullptr, nullptr, &g_swap);
    if (FAILED(hr)) return false;

    // 设备侧只允许排队 1 帧，避免多帧积压把画面拖成残影
    IDXGIDevice1* dxgiDev = nullptr;
    if (SUCCEEDED(g_dev->QueryInterface(__uuidof(IDXGIDevice1), (void**)&dxgiDev)))
    {
        dxgiDev->SetMaximumFrameLatency(1);
        dxgiDev->Release();
    }

    ID3D11Texture2D* bb = nullptr;
    if (FAILED(g_swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&bb))) return false;
    g_dev->CreateRenderTargetView(bb, nullptr, &g_rtv);
    D3D11_TEXTURE2D_DESC bd;
    bb->GetDesc(&bd);
    g_bbW = bd.Width;
    g_bbH = bd.Height;
    bb->Release();
    return g_rtv != nullptr;
}

static void ReleaseDup()
{
    if (g_dup)    { g_dup->Release();    g_dup = nullptr; }
}

static bool CreateDup()
{
    if (!g_output || !g_dev) return false;
    IDXGIOutput1* out1 = nullptr;
    if (FAILED(g_output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&out1))) return false;
    HRESULT hr = out1->DuplicateOutput(g_dev, &g_dup);
    out1->Release();
    return SUCCEEDED(hr);
}

static bool InitFilterPipeline()
{
    ID3DBlob* blob = nullptr;
    HRESULT hr;

    hr = D3DCompile(kShaderSrc, strlen(kShaderSrc), "filter.hlsl", nullptr, nullptr,
                    "VSMain", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, nullptr);
    if (FAILED(hr)) { strcpy(g_shaderErr, "顶点着色器编译失败"); return false; }
    g_dev->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_vs);
    blob->Release();

    hr = D3DCompile(kShaderSrc, strlen(kShaderSrc), "filter.hlsl", nullptr, nullptr,
                    "PSMain", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, nullptr);
    if (FAILED(hr)) { strcpy(g_shaderErr, "像素着色器编译失败"); return false; }
    g_dev->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &g_ps);
    blob->Release();

    D3D11_BUFFER_DESC cbd = {};
    cbd.ByteWidth      = sizeof(GPUParams);
    cbd.Usage          = D3D11_USAGE_DEFAULT;
    cbd.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(g_dev->CreateBuffer(&cbd, nullptr, &g_cb))) return false;

    D3D11_SAMPLER_DESC smd = {};
    smd.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    smd.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
    smd.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
    smd.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
    smd.ComparisonFunc = D3D11_COMPARISON_NEVER;
    smd.MaxLOD         = D3D11_FLOAT32_MAX;
    if (FAILED(g_dev->CreateSamplerState(&smd, &g_smp))) return false;

    return g_vs && g_ps && g_cb && g_smp;
}

// -------------------------------------------------------------
// 抓取一帧桌面
// -------------------------------------------------------------
static void CaptureFrame()
{
    if (!g_dup) return;

    DXGI_OUTDUPL_FRAME_INFO fi = {};
    IDXGIResource* res = nullptr;
    // 阻塞等待新帧到达（最多 16ms）：桌面静止时不空转，有更新时立刻返回，天然按刷新率走
    HRESULT hr = g_dup->AcquireNextFrame(16, &fi, &res);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) { g_captureOk = true; return; }  // 画面没变化，沿用上一帧
    if (FAILED(hr)) { ReleaseDup(); g_captureOk = false; return; }      // 需要重建

    ID3D11Texture2D* src = nullptr;
    if (SUCCEEDED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&src)))
    {
        D3D11_TEXTURE2D_DESC sd;
        src->GetDesc(&sd);
        if (!g_capTex || sd.Width != g_capW || sd.Height != g_capH)
        {
            if (g_capSRV) { g_capSRV->Release(); g_capSRV = nullptr; }
            if (g_capTex) { g_capTex->Release(); g_capTex = nullptr; }
            D3D11_TEXTURE2D_DESC d = sd;
            d.Usage          = D3D11_USAGE_DEFAULT;
            d.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
            d.CPUAccessFlags = 0;
            d.MiscFlags      = 0;
            if (SUCCEEDED(g_dev->CreateTexture2D(&d, nullptr, &g_capTex)))
            {
                g_dev->CreateShaderResourceView(g_capTex, nullptr, &g_capSRV);
                g_capW = sd.Width;
                g_capH = sd.Height;
            }
        }
        if (g_capTex) g_ctx->CopyResource(g_capTex, src);
        src->Release();
    }
    res->Release();
    g_dup->ReleaseFrame();
    g_captureOk = g_capTex != nullptr;
}

// -------------------------------------------------------------
// 渲染
// -------------------------------------------------------------
// 滤镜参数 -> GPU 常量
static void MakeGPUParams(const FilterParams& p, bool enabled, float texelX, float texelY, GPUParams& gp)
{
    memset(&gp, 0, sizeof(gp));
    if (enabled)
    {
        gp.brightness  = p.brightness / 100.0f * 0.4f;
        gp.contrast    = 1.0f + p.contrast / 100.0f;
        gp.saturation  = 1.0f + p.saturation / 100.0f;
        gp.temperature = p.temperature / 100.0f;
        gp.tint        = p.tint / 100.0f;
        gp.gamma       = 1.0f / max(0.01f, 1.0f + p.gamma / 100.0f);
        gp.sharpen     = p.sharpen / 100.0f * 2.5f;
    }
    else
    {
        gp.contrast   = 1.0f;
        gp.saturation = 1.0f;
        gp.gamma      = 1.0f;
    }
    gp.texelX = texelX;
    gp.texelY = texelY;
}

static void DrawFilterPass(ID3D11RenderTargetView* rtv, UINT w, UINT h,
                           ID3D11ShaderResourceView* srv, const GPUParams& gp)
{
    D3D11_VIEWPORT vp = { 0.0f, 0.0f, (float)w, (float)h, 0.0f, 1.0f };
    g_ctx->OMSetRenderTargets(1, &rtv, nullptr);
    g_ctx->RSSetViewports(1, &vp);
    g_ctx->RSSetState(nullptr);
    g_ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
    g_ctx->OMSetDepthStencilState(nullptr, 0);
    g_ctx->UpdateSubresource(g_cb, 0, nullptr, &gp, 0, 0);
    g_ctx->IASetInputLayout(nullptr);
    g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->VSSetShader(g_vs, nullptr, 0);
    g_ctx->PSSetShader(g_ps, nullptr, 0);
    g_ctx->PSSetConstantBuffers(0, 1, &g_cb);
    g_ctx->PSSetShaderResources(0, 1, &srv);
    g_ctx->PSSetSamplers(0, 1, &g_smp);
    g_ctx->Draw(3, 0);
}

// -------------------------------------------------------------
// 主渲染
// -------------------------------------------------------------
static void RenderFrame()
{
    if (!g_rtv) return;


    const float clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
    g_ctx->ClearRenderTargetView(g_rtv, clear);

    if (g_capSRV && g_vs && g_ps)
    {
        GPUParams gp;
        MakeGPUParams(g_p, g_p.enabled, 1.0f / (float)(g_capW ? g_capW : 1),
                      1.0f / (float)(g_capH ? g_capH : 1), gp);
        DrawFilterPass(g_rtv, g_bbW, g_bbH, g_capSRV, gp);

        ID3D11ShaderResourceView* nullSRV = nullptr;
        g_ctx->PSSetShaderResources(0, 1, &nullSRV);
    }

    g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    // 不等垂直同步直接提交，DWM 合成时取最新一帧，减少一帧以上的延迟
    g_swap->Present(0, 0);
}

// -------------------------------------------------------------
// 控制面板：原生 ImGui 控件
// -------------------------------------------------------------
#include "filter_ui.inl"

static void HandleHotkey(int id)
{
    switch (id)
    {
    case 1: SetPanelVisible(!g_panelVisible); break;                     // Ctrl+Insert
    case 2: g_p.enabled = !g_p.enabled; SaveConfig(); break;             // Ctrl+F5
    case 3: {
        int next = 0;
        for (int i = 0; i < kPresetCount; ++i) {
            const FilterParams& p = kPresets[i];
            if (g_p.brightness == p.brightness && g_p.contrast == p.contrast &&
                g_p.saturation == p.saturation && g_p.temperature == p.temperature &&
                g_p.tint == p.tint && g_p.gamma == p.gamma && g_p.sharpen == p.sharpen) {
                next = (i + 1) % kPresetCount;
                break;
            }
        }
        ApplyPreset(next);
        break;
    }
    case 4: ResetFilter(); break;                                       // F4 重置
    case 9: g_quit = true; break;                                        // Ctrl+Alt+Q 退出
    }
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_HOTKEY:     HandleHotkey((int)wParam); return 0;
    case WM_MOUSEACTIVATE:
        return g_panelVisible && g_ui.blockInput ? MA_ACTIVATE : MA_NOACTIVATE;
    case WM_ACTIVATE:
        if (g_panelVisible && g_ui.blockInput && LOWORD(wParam) != WA_INACTIVE) ClipCursor(nullptr);
        break;
    case WM_CLOSE:
        SetPanelVisible(false);
        g_quit = true;
        return 0;
    case WM_SYSCOMMAND: if ((wParam & 0xFFF0) == SC_KEYMENU) return 0; break;
    case WM_DISPLAYCHANGE:
        ReleaseDup();
        return 0;
    case WM_DESTROY:    PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// -------------------------------------------------------------
// 入口
// -------------------------------------------------------------
static bool InitWindow(HINSTANCE hInst)
{
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"TaislAssistantWindow";
    if (!RegisterClassExW(&wc)) return false;

    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfoW(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), &mi);
    g_monRect = mi.rcMonitor;

    // 必须带 WS_EX_LAYERED：只有分层窗口的 WS_EX_TRANSPARENT 才能把鼠标透传给其它进程的窗口
    g_hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, L"TaislAssistantWindow",
                             L"Taisl游戏助手", WS_POPUP, g_monRect.left, g_monRect.top,
                             g_monRect.right - g_monRect.left, g_monRect.bottom - g_monRect.top,
                             nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return false;
    SetLayeredWindowAttributes(g_hwnd, 0, 255, LWA_ALPHA);

    // 让本窗口不被桌面复制捕获，避免自我嵌套重影（Win10 2004+）
    if (g_noExclude)
        g_antiCaptureOk = true;
    else
        g_antiCaptureOk = SetWindowDisplayAffinity(g_hwnd, WDA_EXCLUDEFROMCAPTURE) != FALSE;

    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    const BOOL hk1 = RegisterHotKey(g_hwnd, 1, MOD_CONTROL | MOD_NOREPEAT, VK_INSERT);
    const BOOL hk2 = RegisterHotKey(g_hwnd, 2, MOD_CONTROL, VK_F5);
    const BOOL hk3 = RegisterHotKey(g_hwnd, 3, MOD_CONTROL, VK_F3);
    const BOOL hk4 = RegisterHotKey(g_hwnd, 4, MOD_CONTROL, VK_F4);
    g_hotkeyOk = hk1 && hk2 && hk3 && hk4;
    RegisterHotKey(g_hwnd, 9, MOD_CONTROL | MOD_ALT, 'Q');
    return true;
}

static bool InitImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;

    // 只烘焙界面实际用到的字，避免完整字库导致贴图过大、启动变慢
    static const char* kUsedChars =
        "实时滤镜桌面复制画面对比拖动分割线预设施加参数微调原图明基鲜艳通透高饱和电影夜战柔和"
        "亮度饱和度色温调伽马清晰重置退出板已应提示部快捷注册失败可能被其它程序占"
        "警告系统不支持窗口防捕获影抓不到请把游戏设为无边框化关闭帧率启用"
        "阻止输入穿透拦截交互方式选项开启关闭调节游戏恢复原来自动保存设置简洁白黑高斯模糊界面背景外观自动保存读取恢复默认配置状态已仅作用不影响画面滤镜准星启动还原退出后保留本地选择主题开关参数"
        "中心原点闭合助手准星设置画面调节实时预览颜色样式大小粗细间距透明屏幕居中自由位置拖动定位十字圆点环形描边启用当前坐标百分比自定义点击色块修改关闭水平垂直像素恢复准星"
        "室外内隐藏面板目录检查可移动种风格样张屏幕让每一帧更出暂停拖线查看效果键切换精细节开放你视界真实自然清高暖氛围暗提亮腻低正在连接示隐藏开关滚轮参数自动保存恢复默认片未找到"
        "·：，。「」";

    static ImVector<ImWchar> ranges;
    if (ranges.empty())
    {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
        builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        builder.AddText(kUsedChars);
        builder.BuildRanges(&ranges);
    }

    const char* fonts[] = { "C:\\Windows\\Fonts\\msyh.ttc", "C:\\Windows\\Fonts\\msyh.ttf",
                            "C:\\Windows\\Fonts\\simhei.ttf", "C:\\Windows\\Fonts\\simsun.ttc" };
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH  = true;
    for (const char* f : fonts)
    {
        if (GetFileAttributesA(f) == INVALID_FILE_ATTRIBUTES) continue;
        g_fontBody  = io.Fonts->AddFontFromFileTTF(f, 16.0f, &cfg, ranges.Data);
        g_fontSmall = io.Fonts->AddFontFromFileTTF(f, 13.0f, &cfg, ranges.Data);
        g_fontTitle = io.Fonts->AddFontFromFileTTF(f, 23.0f, &cfg, ranges.Data);
        if (g_fontBody && g_fontSmall && g_fontTitle) break;
    }
    if (!g_fontBody)
    {
        g_fontBody = io.Fonts->AddFontDefault();
        g_fontSmall = g_fontBody;
        g_fontTitle = g_fontBody;
    }
    io.FontDefault = g_fontBody;

    SetupStyle();
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);
    return true;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (wcsstr(GetCommandLineW(), L"--no-exclude")) g_noExclude = true;

    if (!InitWindow(hInst))
    {
        MessageBoxW(nullptr, L"窗口创建失败", L"Taisl游戏助手", MB_ICONERROR);
        return 1;
    }

    IDXGIFactory2* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory2), (void**)&factory)) ||
        !PickAdapterOutput(factory) || !CreateDeviceAndSwapChain(factory))
    {
        MessageBoxW(nullptr, L"Direct3D 11 初始化失败，请确认显卡驱动正常", L"Taisl游戏助手", MB_ICONERROR);
        return 1;
    }
    factory->Release();

    if (!InitFilterPipeline())
    {
        wchar_t msg[600];
        swprintf(msg, 600, L"着色器初始化失败：%hs", g_shaderErr);
        MessageBoxW(nullptr, msg, L"Taisl游戏助手", MB_ICONERROR);
        return 1;
    }

    LoadConfig();
    InitImGui();
    BeginPanelInput();


    g_panelAnimation = UIAnimatedValue(0.0f);
    while (!g_quit)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) { g_quit = true; break; }
        }
        if (g_quit) break;

        // 面板隐藏且滤镜关闭时彻底隐藏窗口，零开销
        const bool needWindow = g_panelVisible || g_panelAnimation.value > 0.0f || g_p.enabled || g_crosshair.enabled;
        const bool shown = IsWindowVisible(g_hwnd) != FALSE;
        if (needWindow && !shown)
        {
            ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
            SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
        else if (!needWindow && shown)
        {
            ShowWindow(g_hwnd, SW_HIDE);
        }

        if (!needWindow)
        {
            Sleep(20);
            continue;
        }

        // 桌面复制失败时每秒重试一次（切换分辨率 / UAC / 独占全屏后常见）
        if (!g_dup && GetTickCount() - g_lastDupRetry > 1000)
        {
            g_lastDupRetry = GetTickCount();
            if (!CreateDup()) g_captureOk = false;
        }
        CaptureFrame();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        g_panelAnimation.Step(g_panelVisible ? 1.0f : 0.0f, min(ImGui::GetIO().DeltaTime, 0.05f), 0.24f);
        if (g_panelAnimation.value > 0.0f) DrawPanel();
        DrawCrosshairOverlay();
        AutoSaveConfig();
        ImGui::Render();

        // The setting selects focused modal input or the original hover-only input mode.
        // Hiding releases input immediately while the closing animation continues.
        UpdatePanelInput();

        RenderFrame();
    }

    EndPanelInput();
    SaveConfig();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    ReleasePreview();
    if (g_capSRV) g_capSRV->Release();
    if (g_capTex) g_capTex->Release();
    ReleaseDup();
    if (g_rtv)  g_rtv->Release();
    if (g_swap) g_swap->Release();
    if (g_output) g_output->Release();
    if (g_adapter) g_adapter->Release();
    if (g_ctx)  g_ctx->Release();
    if (g_dev)  g_dev->Release();
    if (g_hwnd) DestroyWindow(g_hwnd);
    return 0;
}






