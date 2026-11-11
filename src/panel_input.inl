// Taisl 游戏助手 — 面板交互模式：鼠标穿透 / 拦截输入
// 本文件是 Taisl 游戏助手的一部分，以 GNU GPL v3.0 发布，详见根目录 LICENSE。
// Visible UI is an ordinary focused desktop window; hidden UI is an overlay.
// Keep the original foreground target so Ctrl+Insert can return to the game.
static HWND g_panelReturnWindow = nullptr;

static void UpdatePanelInput()
{
    bool interactive = g_panelVisible && g_ui.blockInput;
    if (g_panelVisible && !g_ui.blockInput) {
        POINT cursor;
        if (GetCursorPos(&cursor) && ScreenToClient(g_hwnd, &cursor)) {
            RECT rect = g_panelRect;
            InflateRect(&rect, 4, 4);
            interactive = g_panelRect.right > g_panelRect.left && PtInRect(&rect, cursor);
        }
        if (ImGui::GetCurrentContext() && ImGui::IsAnyItemActive()) interactive = true;
    }
    SetWindowClickThrough(g_hwnd, !interactive);
}

static void BeginPanelInput()
{
    if (!g_ui.blockInput) {
        UpdatePanelInput();
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
        SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return;
    }
    const HWND foreground = GetForegroundWindow();
    if (foreground && foreground != g_hwnd) g_panelReturnWindow = foreground;
    SetWindowClickThrough(g_hwnd, false);
    ShowWindow(g_hwnd, SW_SHOW);
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetForegroundWindow(g_hwnd);
    if (GetForegroundWindow() == g_hwnd) {
        SetFocus(g_hwnd);
        ClipCursor(nullptr);
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
    }
}

static void EndPanelInput()
{
    const bool restore = GetForegroundWindow() == g_hwnd;
    if (GetCapture() == g_hwnd) ReleaseCapture();
    SetWindowClickThrough(g_hwnd, true);
    // Alt+Tab or switching apps is respected: don't steal focus back on hide.
    if (restore && IsWindow(g_panelReturnWindow) &&
        IsWindowVisible(g_panelReturnWindow) && !IsIconic(g_panelReturnWindow)) {
        SetForegroundWindow(g_panelReturnWindow);
    }
    g_panelReturnWindow = nullptr;
}

static void SetPanelVisible(bool visible)
{
    if (g_panelVisible == visible) return;
    g_panelVisible = visible;
    if (visible) BeginPanelInput();
    else EndPanelInput();
}
static void SetPanelBlockInput(bool block)
{
    if (g_ui.blockInput == block) return;
    if (g_panelVisible) EndPanelInput();
    g_ui.blockInput = block;
    if (g_panelVisible) BeginPanelInput();
}