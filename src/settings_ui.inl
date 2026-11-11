// TaiCH 游戏助手 — 设置页：主题、毛玻璃、输入模式与配置读写
// 本文件是 TaiCH 游戏助手的一部分，以 GNU GPL v3.0 发布，详见根目录 LICENSE。
static void DrawSettingsPage(ImVec2 origin,float s) {
    auto* dl=ImGui::GetWindowDrawList();
    auto P=[&](float x,float y){return ImVec2(origin.x+x*s,origin.y+y*s);};
    ImU32 ink=UIColor(62,67,74,255),muted=UIColor(145,149,154,255),accent=UIColor(83,112,211,255),border=UIColor(225,227,223,255);
    auto text=[&](float x,float y,const char* label,float fs=14.0f,ImU32 color=0){dl->AddText(g_fontBody,fs*s,P(x,y),color?color:ink,label);};
    auto card=[&](float x,float y,float w,float h){dl->AddRectFilled(P(x,y),P(x+w,y+h),UIColor(251,251,247,255),18*s);dl->AddRect(P(x,y),P(x+w,y+h),border,18*s);};
    auto hit=[&](const char* id,float x,float y,float w,float h){ImGui::SetCursorScreenPos(P(x,y));bool clicked=ImGui::InvisibleButton(id,ImVec2(w*s,h*s));if(ImGui::IsItemHovered())ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);return clicked;};
    text(42,139,"设置",19);text(42,173,"外观与配置",12,muted);
    card(40,211,704,268);text(62,233,"界面主题",16);text(62,265,"简洁白 / 简洁黑",12,muted);
    for(int i=0;i<2;++i){float x=62+i*331.0f,y=304;bool clicked=hit(i==0?"##lighttheme":"##darktheme",x,y,309,151);bool hover=ImGui::IsItemHovered();
        // Miniatures deliberately retain their own colors, independently of the active theme.
        dl->AddRectFilled(P(x,y),P(x+309,y+151),i==0?IM_COL32(246,247,245,255):IM_COL32(22,25,32,255),13*s);
        dl->AddRect(P(x,y),P(x+309,y+151),g_ui.theme==i?accent:hover?muted:border,13*s,0,(g_ui.theme==i?2.0f:1.0f)*s);
        dl->AddRectFilled(P(x+16,y+17),P(x+293,y+36),i==0?IM_COL32(229,232,238,255):IM_COL32(41,47,60,255),6*s);
        dl->AddRectFilled(P(x+16,y+48),P(x+185,y+108),i==0?IM_COL32(218,226,241,255):IM_COL32(35,44,64,255),8*s);
        for(int j=0;j<3;++j)dl->AddRectFilled(P(x+200,y+49+j*23),P(x+293,y+62+j*23),i==0?IM_COL32(230,233,239,255):IM_COL32(45,51,65,255),5*s);
        text(x+17,y+125,i==0?"简洁白":"简洁黑",13,i==0?IM_COL32(62,67,74,255):IM_COL32(228,234,245,255));
        if(g_ui.theme==i)dl->AddCircleFilled(P(x+285,y+132),4*s,accent);
        if(clicked && g_ui.theme!=i){g_ui.theme=i;ApplyUITheme();SaveConfig();}
    }
    card(40,499,704,207);text(62,522,"UI 高斯模糊",16);
    text(62,563,"仅模糊界面背景",13,muted);text(62,592,"不影响游戏画面滤镜、准星颜色或预览图片",12,muted);
    if(hit("##uiblur",654,522,64,34)){g_ui.blur=!g_ui.blur;SaveConfig();}
    static UIAnimatedValue blur(g_ui.blur?1.0f:0.0f);
    float t=blur.Step(g_ui.blur?1.0f:0.0f,ImGui::GetIO().DeltaTime);
    ImU32 bg=ImGui::ColorConvertFloat4ToU32(ImLerp(ImVec4(.65f,.68f,.73f,1),ImVec4(.325f,.439f,.827f,1),t));
    dl->AddRectFilled(P(654,522),P(718,556),bg,17*s);dl->AddCircleFilled(P(671+30*t,539),12*s,IM_COL32_WHITE);
    text(62,648,g_ui.blur?"已开启 · 界面隐藏后停止模糊渲染":"已关闭 · 使用纯色界面",12,muted);
    card(766,211,394,268);text(788,235,"配置自动保存",16);
    dl->AddCircleFilled(P(794,285),4*s,g_configSaved?IM_COL32(91,170,125,255):IM_COL32(210,140,75,255));
    text(808,277,g_configSaved?"自动保存已启用":"配置写入失败",13);
    text(788,317,"画面、准星、主题与输入设置",13);
    text(788,348,"预览选择、页面与窗口位置",13);
    dl->AddLine(P(788,382),P(1138,382),border);
    text(788,406,"启动时自动读取上次配置",13);
    text(788,443,"恢复默认只影响当前页面",12,muted);

    card(766,499,394,207);text(788,522,"阻止输入穿透",16);
    if(hit("##blockinput",1074,522,64,34)) { SetPanelBlockInput(!g_ui.blockInput); SaveConfig(); }
    static UIAnimatedValue input(g_ui.blockInput?1.0f:0.0f);
    float inputT=input.Step(g_ui.blockInput?1.0f:0.0f,ImGui::GetIO().DeltaTime);
    ImU32 inputBg=ImGui::ColorConvertFloat4ToU32(ImLerp(ImVec4(.65f,.68f,.73f,1),ImVec4(.325f,.439f,.827f,1),inputT));
    dl->AddRectFilled(P(1074,522),P(1138,556),inputBg,17*s);
    dl->AddCircleFilled(P(1091+30*inputT,539),12*s,IM_COL32_WHITE);
    text(788,575,"开启：调节界面时拦截游戏输入",12,muted);
    text(788,604,"关闭：恢复原来的交互方式",12,muted);
    text(788,652,g_ui.blockInput?"已开启 · 隐藏后恢复游戏操作":"已关闭 · 游戏保留输入焦点",12,muted);
}