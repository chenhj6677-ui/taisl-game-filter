// Light editor UI. Coordinates use a 1200 x 820 design canvas.
static ImFont* g_fontBody = nullptr;
static ImFont* g_fontTitle = nullptr;
static ImFont* g_fontSmall = nullptr;
#include "ui_theme.inl"

struct PreviewTarget {
    ID3D11Texture2D* texture = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    void Release() {
        if (srv) srv->Release(); if (rtv) rtv->Release(); if (texture) texture->Release();
        srv = nullptr; rtv = nullptr; texture = nullptr;
    }
};
static ID3D11ShaderResourceView* g_sampleSRV = nullptr;
static UINT g_sampleW = 0, g_sampleH = 0;
static PreviewTarget g_preview, g_thumbnails[5];
static bool g_previewAttempted = false, g_previewDirty = true;

static const wchar_t* kSampleFiles[] = { L"assets\\preview_outdoor.png", L"assets\\preview_indoor.png" };
static const char* kSampleLabels[] = { "室外 FPS", "室内" };
static constexpr int kSampleCount = IM_ARRAYSIZE(kSampleFiles);
static float g_compareSplit = 0.30f;
static ULONG_PTR g_gdiplusToken = 0;

static bool CreatePreviewTarget(PreviewTarget& target, UINT w, UINT h) {
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = w; desc.Height = h; desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(g_dev->CreateTexture2D(&desc, nullptr, &target.texture)) ||
        FAILED(g_dev->CreateRenderTargetView(target.texture, nullptr, &target.rtv)) ||
        FAILED(g_dev->CreateShaderResourceView(target.texture, nullptr, &target.srv))) {
        target.Release(); return false;
    }
    return true;
}
static bool LoadImageSRV(const wchar_t* relativePath, ID3D11ShaderResourceView*& result, UINT& width, UINT& height) {
    Gdiplus::GdiplusStartupInput startup;
    if (!g_gdiplusToken && Gdiplus::GdiplusStartup(&g_gdiplusToken, &startup, nullptr) != Gdiplus::Ok) return false;
    wchar_t path[MAX_PATH]; GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* end = wcsrchr(path, L'\\'); if (!end) return false;
    *(end + 1) = 0; lstrcatW(path, relativePath);
    Gdiplus::Bitmap bitmap(path);
    if (bitmap.GetLastStatus() != Gdiplus::Ok) return false;
    width = bitmap.GetWidth(); height = bitmap.GetHeight();
    Gdiplus::Rect rect(0, 0, width, height);
    Gdiplus::BitmapData data;
    if (bitmap.LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok) return false;
    // Copy row by row: GDI+ may return a negative stride.
    std::vector<unsigned char> pixels((size_t)width * height * 4);
    for (UINT y = 0; y < height; ++y)
        memcpy(pixels.data() + (size_t)y * width * 4, (unsigned char*)data.Scan0 + (ptrdiff_t)y * data.Stride, width * 4);
    bitmap.UnlockBits(&data);
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width; desc.Height = height; desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial = { pixels.data(), width * 4, 0 };
    ID3D11Texture2D* texture = nullptr;
    if (FAILED(g_dev->CreateTexture2D(&desc, &initial, &texture))) return false;

    HRESULT hr = g_dev->CreateShaderResourceView(texture, nullptr, &result);
    texture->Release();
    return SUCCEEDED(hr);
}
static ID3D11ShaderResourceView* g_crosshairSampleSRV = nullptr;
static UINT g_crosshairSampleW=0, g_crosshairSampleH=0;
static bool g_crosshairSampleAttempted=false;
static void EnsureCrosshairSample() {
    if(!g_crosshairSampleAttempted) {
        g_crosshairSampleAttempted=true;
        LoadImageSRV(kSampleFiles[0],g_crosshairSampleSRV,g_crosshairSampleW,g_crosshairSampleH);
    }
}
static bool LoadPreviewImage(int index) {
    if (index < 0 || index >= kSampleCount) return false;
    UINT width=0,height=0;
    ID3D11ShaderResourceView* nextSRV=nullptr;
    bool loaded=LoadImageSRV(kSampleFiles[index],nextSRV,width,height);
    HRESULT hr=loaded ? S_OK:E_FAIL;
    PreviewTarget nextPreview, nextThumbnails[5];
    bool ok = SUCCEEDED(hr) && CreatePreviewTarget(nextPreview, width, height);
    for (auto& target : nextThumbnails) if (ok) ok = CreatePreviewTarget(target, 256, 144);
    if (!ok) {
        if (nextSRV) nextSRV->Release(); nextPreview.Release();
        for (auto& target : nextThumbnails) target.Release();
        return false;
    }
    ID3D11ShaderResourceView* empty = nullptr;
    g_ctx->PSSetShaderResources(0, 1, &empty);
    g_ctx->OMSetRenderTargets(0, nullptr, nullptr);
    if (g_sampleSRV) g_sampleSRV->Release();
    g_preview.Release(); for (auto& target : g_thumbnails) target.Release();
    g_sampleSRV = nextSRV; g_preview = nextPreview;
    for (int i=0; i<5; ++i) g_thumbnails[i] = nextThumbnails[i];
    g_sampleW = width; g_sampleH = height; g_selectedSample = index; g_previewDirty = true;
    return true;
}
static void UpdatePreview() {
    if (!g_previewAttempted || g_requestedSample != g_selectedSample) {
        g_previewAttempted = true;
        if (LoadPreviewImage(g_requestedSample)) {
            for (int i = 0; i < kPresetCount; ++i) {
                GPUParams gp; MakeGPUParams(kPresets[i], true, 1.0f/g_sampleW, 1.0f/g_sampleH, gp);
                DrawFilterPass(g_thumbnails[i].rtv, 256, 144, g_sampleSRV, gp);
            }
        } else { g_requestedSample = g_selectedSample; }
    }
    static FilterParams previous = {};
    if (g_sampleSRV && g_preview.rtv && (g_previewDirty || memcmp(&previous, &g_p, sizeof(g_p)) != 0)) {
        GPUParams gp; MakeGPUParams(g_p, g_p.enabled, 1.0f/g_sampleW, 1.0f/g_sampleH, gp);
        DrawFilterPass(g_preview.rtv, g_sampleW, g_sampleH, g_sampleSRV, gp);
        previous = g_p; g_previewDirty = false;
    }
    ID3D11ShaderResourceView* empty = nullptr;
    g_ctx->PSSetShaderResources(0, 1, &empty);
    g_ctx->OMSetRenderTargets(0, nullptr, nullptr);
}
#include "ui_blur.inl"
static void ReleasePreview() {
    ReleaseUIBlur();
    if(g_crosshairSampleSRV){g_crosshairSampleSRV->Release();g_crosshairSampleSRV=nullptr;}
    g_preview.Release(); for (auto& target : g_thumbnails) target.Release();
    if (g_sampleSRV) { g_sampleSRV->Release(); g_sampleSRV = nullptr; }
    if (g_gdiplusToken) Gdiplus::GdiplusShutdown(g_gdiplusToken);
}
static void SetupStyle() { ApplyUITheme(); }
// Interruptible, time-based ease-out. Retarget from the current position so
// rapid clicks never snap the indicator back to an earlier animation frame.
struct UIAnimatedValue {
    float value, start, target, elapsed;
    explicit UIAnimatedValue(float initial) : value(initial), start(initial), target(initial), elapsed(1.0f) {}
    float Step(float next, float dt, float duration = 0.22f) {
        if (next != target) { start = value; target = next; elapsed = 0.0f; }
        elapsed = min(duration, elapsed + max(0.0f, dt));
        float t = elapsed / duration;
        float ease = 1.0f - (1.0f-t)*(1.0f-t)*(1.0f-t);
        value = start + (target-start)*ease;
        if (elapsed >= duration) value = target;
        return value;
    }
};
static UIAnimatedValue g_sampleIndicator(0.0f);
static UIAnimatedValue g_panelAnimation(1.0f);
#include "segmented_ui.inl"
#include "crosshair_ui.inl"
#include "settings_ui.inl"
static void DrawPanel() {
    ApplyUITheme();
    UpdatePreview();
    ImGuiIO& io = ImGui::GetIO();
    const float scale = min(1.0f, min((io.DisplaySize.x-32)/1200.0f, (io.DisplaySize.y-32)/820.0f));
    const float baseScale = max(0.25f, scale);
    const float visibility = g_panelAnimation.value;
    const float s = baseScale * (0.965f + 0.035f * visibility);
    const ImVec2 size(1200*s, 820*s);
    const ImVec2 baseSize(1200*baseScale, 820*baseScale);
    if (g_panelPos.x < 0) g_panelPos = ImVec2((io.DisplaySize.x-baseSize.x)*0.5f, (io.DisplaySize.y-baseSize.y)*0.5f);
    g_panelPos.x = max(0.0f, min(g_panelPos.x, io.DisplaySize.x-baseSize.x));
    g_panelPos.y = max(0.0f, min(g_panelPos.y, io.DisplaySize.y-baseSize.y));
    const ImVec2 animatedPos(g_panelPos.x+(baseSize.x-size.x)*0.5f,
        g_panelPos.y+(baseSize.y-size.y)*0.5f+12.0f*baseScale*(1.0f-visibility));
    const bool frosted=UpdateUIBlur(animatedPos,size);
    ImGui::SetNextWindowBgAlpha(frosted ? 0.0f:1.0f);
    ImGui::SetNextWindowPos(animatedPos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 26*s);
    ImGui::Begin("TaiCH游戏助手", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ((!g_panelVisible || visibility < 0.35f) ? ImGuiWindowFlags_NoInputs : 0));
    const ImVec2 origin = ImGui::GetWindowPos();
    g_panelRect = { (LONG)origin.x, (LONG)origin.y, (LONG)(origin.x+size.x), (LONG)(origin.y+size.y) };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto P = [&](float x, float y) { return ImVec2(origin.x+x*s, origin.y+y*s); };
    if(frosted) {
        dl->AddImageRounded((ImTextureID)(intptr_t)g_uiBlurB.srv,origin,ImVec2(origin.x+size.x,origin.y+size.y),ImVec2(0,0),ImVec2(1,1),IM_COL32_WHITE,26*s);
        ImU32 tint=g_ui.theme==1 ? IM_COL32(16,20,28,214):IM_COL32(248,249,244,214);
        dl->AddRectFilled(origin,ImVec2(origin.x+size.x,origin.y+size.y),tint,26*s);
    }
    const ImU32 ink = UIColor(62,67,74,255), muted = UIColor(145,149,154,255);
    const ImU32 accent = UIColor(83,112,211,255), border = UIColor(225,227,223,255);
    auto text = [&](float x, float y, const char* value, ImFont* font, float height, ImU32 color) {
        dl->AddText(font, height*s, P(x,y), color, value);
    };
    auto centered = [&](float x, float y, float width, const char* value, float height, ImU32 color) {
        float tw = g_fontBody->CalcTextSizeA(height*s, FLT_MAX, 0, value).x/s;
        text(x+(width-tw)*0.5f,y,value,g_fontBody,height,color);
    };
    auto hit = [&](const char* id, float x, float y, float w, float h) {
        ImGui::SetCursorScreenPos(P(x,y)); return ImGui::InvisibleButton(id,ImVec2(w*s,h*s));
    };
    auto box = [&](float x, float y, float w, float h, ImU32 fill, float radius=14.0f) {
        dl->AddRectFilled(P(x,y),P(x+w,y+h),fill,radius*s);
        dl->AddRect(P(x,y),P(x+w,y+h),border,radius*s,0,1*s);
    };
    auto section = [&](float x, float y, const char* title) {
        dl->AddRectFilled(P(x,y),P(x+3,y+22),accent,1.5f*s);
        text(x+15,y+1,title,g_fontBody,16,ink);
    };
    // Header drag area is separate from the power switch.
    hit("##header",0,0,420,94);
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
        g_panelPos.x += io.MouseDelta.x; g_panelPos.y += io.MouseDelta.y;
    }
    dl->AddRectFilled(P(34,27),P(76,69),accent,13*s);
    dl->AddCircleFilled(P(55,48),8*s,IM_COL32(255,255,247,255));
    dl->AddCircle(P(55,48),9*s,UIColor(64,88,172,255),0,2*s);
    text(93,27,"TaiCH游戏助手",g_fontTitle,22,ink);
    text(94,59,"画面与准星  /  专注每一刻",g_fontSmall,12,muted);
    static UIAnimatedValue pageAnimation(0);
    const char* pages[]={"画面调节","准星设置","设置"};
    g_activePage=DrawAnimatedSegments("##pages",P(430,33),ImVec2(390,36),pages,3,g_activePage,pageAnimation,s);
    if(g_activePage==0) {
    bool& moduleEnabled=g_p.enabled;
    text(984,43,moduleEnabled ? "已启用" : "已暂停",g_fontSmall,13,muted);
    bool toggle = hit("##power",1046,34,64,34);
    bool powerHover = ImGui::IsItemHovered();
    if (powerHover) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (toggle) { moduleEnabled = !moduleEnabled; SaveConfig(); }
    static UIAnimatedValue power(moduleEnabled ? 1.0f : 0.0f);
    const float powerT = power.Step(moduleEnabled ? 1.0f : 0.0f, io.DeltaTime, 0.20f);
    ImU32 powerColor = ImGui::ColorConvertFloat4ToU32(ImLerp(
        ImVec4(0.74f,0.76f,0.79f,1), ImVec4(0.325f,0.439f,0.827f,1), powerT));
    dl->AddRectFilled(P(1046,34),P(1110,68),powerColor,17*s);
    float thumbX = 1063.0f + 30.0f*powerT;
    dl->AddCircleFilled(P(thumbX,52),12.5f*s,UIColor(35,45,75,24));
    dl->AddCircleFilled(P(thumbX,51),12*s,IM_COL32(255,255,253,255));
    }
    bool hide = hit("##hide",1134,31,40,40);
    if (ImGui::IsItemHovered()) {
        dl->AddRectFilled(P(1134,31),P(1174,71),UIColor(232,236,244,255),10*s);
        ImGui::SetTooltip("隐藏面板 (Ctrl+Insert)");
    }
    dl->AddLine(P(1147,51),P(1161,51),muted,2*s);
    if (hide) SetPanelVisible(false);
    dl->AddLine(P(0,96),P(1200,96),border,1*s);

    if(g_activePage==0) {
    box(24,120,710,602,UIColor(251,251,246,255),20);
    section(40,140,"画面对比");
    text(209,145,"拖动分割线查看效果",g_fontSmall,11,muted);
    // iOS-style segmented control: one shared track and one moving selection.
    // Only image loading waits for the next frame; visual feedback starts now.
    const float segmentX=538, segmentY=132, segmentW=180, segmentH=38;
    const float cellW=segmentW/kSampleCount, inset=3;
    dl->AddRectFilled(P(segmentX,segmentY),P(segmentX+segmentW,segmentY+segmentH),UIColor(235,237,240,255),13*s);
    static UIAnimatedValue segmentHover[] = { UIAnimatedValue(0), UIAnimatedValue(0) };
    bool segmentDown=false;
    for (int i=0; i<kSampleCount; ++i) {
        const float x=segmentX+i*cellW;
        ImGui::PushID(100+i);
        bool clicked=hit("##sample",x,segmentY,cellW,segmentH);
        bool hovered=ImGui::IsItemHovered();
        segmentDown |= ImGui::IsItemActive();
        ImGui::PopID();
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        float hover=segmentHover[i].Step(hovered ? 1.0f:0.0f,io.DeltaTime,0.12f);
        dl->AddRectFilled(P(x+inset,segmentY+inset),P(x+cellW-inset,segmentY+segmentH-inset),
            UIColor(255,255,255,(int)(hover*75)),10*s);
        if (clicked) g_requestedSample=i;
    }
    const float selection=g_sampleIndicator.Step((float)g_requestedSample,io.DeltaTime);
    const float indicatorX=segmentX+selection*cellW+inset;
    const float pressInset=segmentDown ? 0.5f:0.0f;
    // Layered soft shadow rather than a heavy outline keeps the selected tile raised.
    dl->AddRectFilled(P(indicatorX-1,segmentY+inset+1),P(indicatorX+cellW-2*inset+1,segmentY+segmentH-inset+3),UIColor(33,43,67,9),11*s);
    dl->AddRectFilled(P(indicatorX,segmentY+inset+1),P(indicatorX+cellW-2*inset,segmentY+segmentH-inset+1),UIColor(33,43,67,15),10*s);
    dl->AddRectFilled(P(indicatorX+pressInset,segmentY+inset+pressInset),
        P(indicatorX+cellW-2*inset-pressInset,segmentY+segmentH-inset-pressInset),UITileWhite(),10*s);
    dl->AddRect(P(indicatorX+pressInset,segmentY+inset+pressInset),
        P(indicatorX+cellW-2*inset-pressInset,segmentY+segmentH-inset-pressInset),UIColor(255,255,255,220),10*s,0,0.7f*s);
    for (int i=0; i<kSampleCount; ++i) {
        float emphasis=1.0f-min(1.0f,fabsf(selection-(float)i));
        ImU32 color=ImGui::ColorConvertFloat4ToU32(ImLerp(
            ImVec4(0.52f,0.55f,0.59f,1),UISelectedText(),emphasis));
        centered(segmentX+i*cellW,segmentY+12,cellW,kSampleLabels[i],13,color);
    }
    const float ix=40, iy=182, iw=678, ih=350;
    box(ix-3,iy-3,iw+6,ih+6,UIColor(230,232,227,255),16);
    if (g_sampleSRV && g_preview.srv) {
        // Center-crop both halves identically; never stretch the landscape.
        const float imageAspect = (float)g_sampleW/g_sampleH;
        const float visible = min(1.0f, (iw/ih)/imageAspect);
        const float visibleY = min(1.0f, imageAspect/(iw/ih));
        const float v0=(1-visibleY)*0.5f, v1=1-v0;
        const float u0=(1-visible)*0.5f, u1=1-u0;
        dl->AddImageRounded((ImTextureID)(intptr_t)g_preview.srv,P(ix,iy),P(ix+iw,iy+ih),ImVec2(u0,v0),ImVec2(u1,v1),IM_COL32_WHITE,12*s);
        dl->PushClipRect(P(ix,iy),P(ix+iw*g_compareSplit,iy+ih),true);
        dl->AddImageRounded((ImTextureID)(intptr_t)g_sampleSRV,P(ix,iy),P(ix+iw,iy+ih),ImVec2(u0,v0),ImVec2(u1,v1),IM_COL32_WHITE,12*s);
        dl->PopClipRect();
    } else {
        centered(ix,iy+155,iw,"预览图片未找到，请检查 assets 目录",14,muted);
    }
    hit("##comparison",ix,iy,iw,ih);
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive()) g_compareSplit = max(0.02f,min(0.98f,(io.MousePos.x-origin.x-ix*s)/(iw*s)));
    const float split=ix+iw*g_compareSplit;
    dl->AddLine(P(split,iy),P(split,iy+ih),UIColor(255,255,255,225),1.5f*s);
    dl->AddCircleFilled(P(split,iy+ih/2),14*s,UIColor(52,61,71,180));
    dl->AddCircle(P(split,iy+ih/2),14*s,UIColor(255,255,255,240),0,1.5f*s);
    dl->AddLine(P(split-3,iy+ih/2-4),P(split-6,iy+ih/2),IM_COL32_WHITE,1.5f*s);
    dl->AddLine(P(split-6,iy+ih/2),P(split-3,iy+ih/2+4),IM_COL32_WHITE,1.5f*s);
    dl->AddLine(P(split+3,iy+ih/2-4),P(split+6,iy+ih/2),IM_COL32_WHITE,1.5f*s);
    dl->AddLine(P(split+6,iy+ih/2),P(split+3,iy+ih/2+4),IM_COL32_WHITE,1.5f*s);
    dl->AddRectFilled(P(ix+10,iy+10),P(ix+56,iy+34),UIColor(41,51,61,160),7*s);
    text(ix+20,iy+15,"原图",g_fontSmall,12,IM_COL32_WHITE);
    dl->AddRectFilled(P(ix+iw-94,iy+10),P(ix+iw-10,iy+34),UIColor(41,51,61,160),7*s);
    text(ix+iw-82,iy+15,g_p.enabled ? "滤镜效果" : "滤镜暂停",g_fontSmall,12,IM_COL32_WHITE);

    section(40,557,"滤镜预设");
    text(586,561,"一键切换风格",g_fontSmall,12,muted);
    const char* hints[] = {"暗部清晰","通透高饱和","暖调氛围","暗部提亮","细腻低对比"};
    for (int i=0;i<5;++i) {
        const float x=40+i*138.0f, y=594;
        const FilterParams& p=kPresets[i];
        bool selected = g_p.brightness==p.brightness && g_p.contrast==p.contrast && g_p.saturation==p.saturation &&
            g_p.temperature==p.temperature && g_p.tint==p.tint && g_p.gamma==p.gamma && g_p.sharpen==p.sharpen;
        ImGui::PushID(i); bool clicked=hit("##preset",x,y,126,110); bool hovered=ImGui::IsItemHovered(); ImGui::PopID();
        box(x,y,126,110,selected ? UIColor(239,243,254,255) : hovered ? UIColor(241,243,242,255) : UIColor(250,250,246,255),12);
        if (selected) dl->AddRect(P(x,y),P(x+126,y+110),accent,12*s,0,1.8f*s);
        if (g_thumbnails[i].srv) dl->AddImageRounded((ImTextureID)(intptr_t)g_thumbnails[i].srv,P(x+7,y+7),P(x+119,y+69),ImVec2(0,0),ImVec2(1,1),IM_COL32_WHITE,7*s);
        centered(x,y+76,126,kPresetNames[i],13,selected ? accent:ink);
        centered(x,y+94,126,hints[i],10,muted);
        if (clicked) ApplyPreset(i);
    }
    section(754,140,"精细调节");
    text(1022,144,"实时预览 · 开放你的视界",g_fontSmall,11,muted);
    float* values[] = { &g_p.brightness,&g_p.contrast,&g_p.saturation,&g_p.temperature,&g_p.tint,&g_p.gamma,&g_p.sharpen };
    const char* labels[] = {"亮度","对比度","饱和度","色温","色调","伽马","清晰度"};
    static bool dirty=false;
    for (int i=0;i<7;++i) {
        float x=750, y=182+i*73.0f;
        box(x,y,426,57,UIColor(251,251,247,255),13);
        text(x+17,y+20,labels[i],g_fontBody,14,ink);
        const float tx=x+106, tw=236, cy=y+29;
        ImGui::PushID(i); hit("##parameter",tx-8,y+8,tw+16,42);
        bool hovered=ImGui::IsItemHovered(), active=ImGui::IsItemActive();
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const float lo=i==6 ? 0.0f:-100.0f;
        if (active) {
            float value=lo+max(0.0f,min(1.0f,(io.MousePos.x-origin.x-tx*s)/(tw*s)))*(100-lo);
            value=roundf(value); if (*values[i]!=value) { *values[i]=value; dirty=true; }
        }
        if (hovered && io.MouseWheel!=0) { *values[i]=max(lo,min(100.0f,*values[i]+io.MouseWheel)); dirty=true; }
        ImGui::PopID();
        const float knob=tx+(*values[i]-lo)/(100-lo)*tw;
        dl->AddLine(P(tx,cy),P(tx+tw,cy),UIColor(216,220,225,255),5*s);
        const float neutral=i==6 ? tx:tx+tw/2;
        if (*values[i]!=0) dl->AddLine(P(neutral,cy),P(knob,cy),UIColor(132,153,221,255),5*s);
        dl->AddCircleFilled(P(knob,cy),10*s,UIColor(251,251,247,255));
        dl->AddCircle(P(knob,cy),10*s,active ? accent:UIColor(129,146,196,255),0,(hovered?2.5f:2.0f)*s);
        char value[16]; snprintf(value,sizeof(value),"%.0f",*values[i]);
        centered(x+365,y+21,47,value,13,muted);
    }
    if (dirty && !ImGui::IsMouseDown(0)) { SaveConfig(); dirty=false; }
    text(760,704,"拖动调节 · 滚轮微调 · 参数自动保存",g_fontSmall,11,muted);
    } else if(g_activePage==1) { DrawCrosshairPage(origin,s); } else { DrawSettingsPage(origin,s); }
    dl->AddLine(P(24,742),P(1176,742),border,1*s);
    char status[128];
    snprintf(status,sizeof(status),"%s  /  %.0f FPS",g_activePage==2 ? "设置自动保存" : g_activePage==1 ? (g_crosshair.enabled ? "准星已启用":"准星已关闭") : (g_captureOk ? "桌面实时滤镜":"正在连接桌面"),io.Framerate);
    dl->AddCircleFilled(P(41,781),3*s,g_captureOk ? UIColor(91,160,127,255):UIColor(210,155,75,255));
    text(53,774,status,g_fontSmall,12,muted);
    text(268,775,"Ctrl+Insert 显示 / 隐藏     Ctrl+F5 滤镜     Ctrl+F3 预设",g_fontSmall,11,muted);
    if (!g_hotkeyOk || !g_antiCaptureOk)
        text(40,729,!g_hotkeyOk ? "部分快捷键被占用":"窗口防捕获不可用，可能出现重影",g_fontSmall,10,UIColor(184,131,65,255));
    auto button = [&](const char* id, float x, const char* label) {
        bool clicked=hit(id,x,761,120,41); bool hovered=ImGui::IsItemHovered();
        box(x,761,120,41,hovered ? UIColor(235,239,249,255):UIColor(248,249,245,255),11);
        centered(x,774,120,label,14,ink); return clicked;
    };
    if (button("##reset",914,"恢复默认")) {
        if(g_activePage==0) ResetFilter(); else if(g_activePage==1) { g_crosshair=CrosshairParams(); SaveConfig(); } else { SetPanelBlockInput(true);g_ui=UISettings();ApplyUITheme();SaveConfig(); }
    }
    if (button("##exit",1050,"退出")) g_quit=true;
    // Apply opacity to every panel vertex, including custom images and blur.
    // The foreground crosshair is a separate draw list and remains untouched.
    if (visibility < 1.0f) {
        for (ImDrawVert& vertex : dl->VtxBuffer) {
            const ImU32 alpha = (vertex.col >> IM_COL32_A_SHIFT) & 255;
            vertex.col = (vertex.col & ~(255u << IM_COL32_A_SHIFT)) |
                ((ImU32)(alpha * visibility + 0.5f) << IM_COL32_A_SHIFT);
        }
    }
    ImGui::End(); ImGui::PopStyleVar();
}














