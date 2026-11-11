// TaiCH 游戏助手 — 界面设置状态与配置序列化
// 本文件是 TaiCH 游戏助手的一部分，以 GNU GPL v3.0 发布，详见根目录 LICENSE。
struct UISettings {
    int theme=0;
    bool blur=false;
    bool blockInput=true;
};
static UISettings g_ui;
static int g_selectedSample=0,g_requestedSample=0;
static std::string g_savedConfig;
static bool g_configSaved=true;
static std::string SerializeConfig() {
    char data[3072];
    snprintf(data,sizeof(data),
        "enabled=%d\nbrightness=%.3f\ncontrast=%.3f\nsaturation=%.3f\ntemperature=%.3f\ntint=%.3f\ngamma=%.3f\nsharpen=%.3f\n"
        "cross_enabled=%d\ncross_centered=%d\ncross_outline=%d\ncross_style=%d\ncross_dot=%d\n"
        "cross_size=%.3f\ncross_thickness=%.3f\ncross_gap=%.3f\ncross_x=%.6f\ncross_y=%.6f\n"
        "cross_r=%.4f\ncross_g=%.4f\ncross_b=%.4f\ncross_a=%.4f\n"
        "ui_theme=%d\nui_blur=%d\nui_block_input=%d\nui_sample=%d\nui_page=%d\nui_panel_x=%.1f\nui_panel_y=%.1f\n",
        g_p.enabled,g_p.brightness,g_p.contrast,g_p.saturation,g_p.temperature,g_p.tint,g_p.gamma,g_p.sharpen,
        g_crosshair.enabled,g_crosshair.centered,g_crosshair.outline,g_crosshair.style,g_crosshair.centerDot,
        g_crosshair.size,g_crosshair.thickness,g_crosshair.gap,g_crosshair.x,g_crosshair.y,
        g_crosshair.color[0],g_crosshair.color[1],g_crosshair.color[2],g_crosshair.color[3],
        g_ui.theme,g_ui.blur,g_ui.blockInput,g_requestedSample,g_activePage,g_panelPos.x,g_panelPos.y);
    return data;
}
