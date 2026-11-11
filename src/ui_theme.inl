// Taisl 游戏助手 — 亮 / 暗主题的配色映射
// 本文件是 Taisl 游戏助手的一部分，以 GNU GPL v3.0 发布，详见根目录 LICENSE。
// Theme conversion is used only by UI drawing. Image tints, crosshair colors
// and desktop filter constants never go through this function.
static ImU32 UIColor(int r,int g,int b,int a=255) {
    bool neutral=max(r,max(g,b))-min(r,min(g,b))<30;
    if(g_ui.theme==1) {
        if(a<100 || (a<240 && r<80 && g<90 && b<100)) return IM_COL32(r,g,b,a);
        if(r>220 && g>220 && b>235 && b-r>6) return IM_COL32(43,53,77,a);
        if(neutral) {
            int level=(r+g+b)/3;
            if(level>238){r=31;g=34;b=41;}
            else if(level>205){r=62;g=67;b=80;}
            else if(level>170){r=95;g=103;b=118;}
            else if(level>105){r=156;g=165;b=181;}
            else{r=225;g=231;b=242;}
        } else if(b>r+25 && b>g+15 && r<180){r=132;g=157;b=245;}
    }
    // Frosted UI surfaces reveal the already-blurred background, never raw game pixels.
    if(g_ui.blur && a==255 && neutral && ((r+g+b)/3>235 || (g_ui.theme==1 && r==31 && g==34)))a=228;
    return IM_COL32(r,g,b,a);
}
static ImU32 UITileWhite(){return g_ui.theme==1 ? IM_COL32(69,76,94,255):IM_COL32_WHITE;}
static ImVec4 UISelectedText(){return g_ui.theme==1 ? ImVec4(.70f,.78f,1,1):ImVec4(.27f,.37f,.68f,1);}
static void ApplyUITheme() {
    if(g_ui.theme==1)ImGui::StyleColorsDark();else ImGui::StyleColorsLight();
    ImGuiStyle& style=ImGui::GetStyle();
    style.WindowRounding=26;style.WindowBorderSize=0;style.WindowPadding=ImVec2(0,0);
    style.FrameRounding=10;style.PopupRounding=12;style.AntiAliasedLines=true;
    style.Colors[ImGuiCol_WindowBg]=g_ui.theme==1 ? ImVec4(.075f,.086f,.11f,1):ImVec4(.973f,.973f,.953f,1);
    style.Colors[ImGuiCol_Text]=g_ui.theme==1 ? ImVec4(.88f,.91f,.95f,1):ImVec4(.24f,.26f,.28f,1);
    style.Colors[ImGuiCol_PopupBg]=g_ui.theme==1 ? ImVec4(.12f,.135f,.17f,1):ImVec4(.98f,.98f,.97f,1);
}

