// TaiCH 游戏助手 — 准星参数与几何绘制（桌面叠加与编辑器预览共用同一套绘制代码）
// 本文件是 TaiCH 游戏助手的一部分，以 GNU GPL v3.0 发布，详见根目录 LICENSE。
struct CrosshairParams {
    bool enabled = false;
    bool centered = true;
    bool outline = true;
    bool centerDot = true;
    int style = 0;
    float size = 12.0f;
    float thickness = 2.0f;
    float gap = 4.0f;
    float x = 0.5f, y = 0.5f;
    float color[4] = {0.25f, 1.0f, 0.50f, 1.0f};
};
static CrosshairParams g_crosshair;
static int g_activePage = 0;
static float CrosshairClamp(float value, float lo, float hi, float fallback) {
    return std::isfinite(value) ? max(lo, min(hi, value)) : fallback;
}
static void ValidateCrosshair() {
    g_crosshair.style = max(0, min(4, g_crosshair.style));
    g_crosshair.size = CrosshairClamp(g_crosshair.size,2,40,12);
    g_crosshair.thickness = CrosshairClamp(g_crosshair.thickness,1,8,2);
    g_crosshair.gap = CrosshairClamp(g_crosshair.gap,0,20,4);
    g_crosshair.x = CrosshairClamp(g_crosshair.x,0,1,0.5f);
    g_crosshair.y = CrosshairClamp(g_crosshair.y,0,1,0.5f);
    for (int i=0;i<4;++i) g_crosshair.color[i]=CrosshairClamp(g_crosshair.color[i],0,1,1);
}
static ImVec2 CrosshairPosition(ImVec2 display) {
    return ImVec2(display.x*(g_crosshair.centered ? 0.5f:g_crosshair.x),
                  display.y*(g_crosshair.centered ? 0.5f:g_crosshair.y));
}
// Shared geometry for the desktop overlay and the editor preview.
static void DrawCrosshairShape(ImDrawList* dl, ImVec2 center, const CrosshairParams& p, float scale=1.0f) {
    ImU32 color=ImGui::ColorConvertFloat4ToU32(ImVec4(p.color[0],p.color[1],p.color[2],p.color[3]));
    ImU32 black=IM_COL32(12,16,22,(int)(p.color[3]*220));
    float length=p.size*scale, width=p.thickness*scale, gap=p.gap*scale;
    // Draw all outlines first, then all colored strokes. Arm-by-arm outlines
    // otherwise paint black over the center when gap is zero.
    auto geometry=[&](bool outlinePass) {
        ImU32 stroke=outlinePass ? black:color;
        float thick=width+(outlinePass ? 2*scale:0);
        auto line=[&](float x1,float y1,float x2,float y2) {
            dl->AddLine(ImVec2(center.x+x1,center.y+y1),ImVec2(center.x+x2,center.y+y2),stroke,thick);
        };
        if(p.style==1) {
            float radius=max(width,length*0.18f)+(outlinePass ? scale:0);
            dl->AddCircleFilled(center,radius,stroke,32);
        } else {
            if(p.style==2 || p.style==3) dl->AddCircle(center,length,stroke,64,thick);
            if(p.style==0 || p.style==3 || p.style==4) {
                if(gap<=0.001f) {
                    line(-length,0,length,0);
                    line(0,p.style==4 ? 0:-length,0,length);
                } else {
                    line(-gap-length,0,-gap,0);line(gap,0,gap+length,0);
                    if(p.style!=4)line(0,-gap-length,0,-gap);
                    line(0,gap,0,gap+length);
                }
            }
            if(p.centerDot) dl->AddCircleFilled(center,max(scale,width*.6f)+(outlinePass ? scale:0),stroke,24);
        }
    };
    if(p.outline) geometry(true);
    geometry(false);
}
static void DrawCrosshairOverlay() {
    if(g_crosshair.enabled)
        DrawCrosshairShape(ImGui::GetForegroundDrawList(),CrosshairPosition(ImGui::GetIO().DisplaySize),g_crosshair);
}

