// Taisl 游戏助手 — 准星设置页：样式、颜色、尺寸与位置
// 本文件是 Taisl 游戏助手的一部分，以 GNU GPL v3.0 发布，详见根目录 LICENSE。
static void DrawCrosshairPage(ImVec2 origin,float s) {
    EnsureCrosshairSample();
    ImDrawList* dl=ImGui::GetWindowDrawList(); auto& io=ImGui::GetIO();
    auto P=[&](float x,float y){return ImVec2(origin.x+x*s,origin.y+y*s);};
    const ImU32 ink=UIColor(62,67,74,255),muted=UIColor(145,149,154,255),accent=UIColor(83,112,211,255),border=UIColor(225,227,223,255);
    auto text=[&](float x,float y,const char* label,float fs=14.0f,ImU32 color=UIColor(62,67,74,255)){
        dl->AddText(g_fontBody,fs*s,P(x,y),color,label);
    };
    auto hit=[&](const char* id,float x,float y,float w,float h){
        ImGui::SetCursorScreenPos(P(x,y)); bool clicked=ImGui::InvisibleButton(id,ImVec2(w*s,h*s));
        if(ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); return clicked;
    };
    auto box=[&](float x,float y,float w,float h,ImU32 fill=UIColor(251,251,247,255)){
        dl->AddRectFilled(P(x,y),P(x+w,y+h),fill,14*s); dl->AddRect(P(x,y),P(x+w,y+h),border,14*s);
    };
    auto section=[&](float x,float y,const char* label){dl->AddRectFilled(P(x,y),P(x+3,y+22),accent,1.5f*s);text(x+15,y+1,label,16);};
    bool changed=false;
    auto toggle=[&](const char* id,float x,float y,bool& enabled){
        bool clicked=hit(id,x,y,43,25); if(clicked){enabled=!enabled;changed=true;}
        ImGuiID key=ImGui::GetID(id); float value=ImGui::GetStateStorage()->GetFloat(key,enabled?1.0f:0.0f);
        value+=(float(enabled)-value)*(1-expf(-18*io.DeltaTime)); ImGui::GetStateStorage()->SetFloat(key,value);
        ImU32 bg=ImGui::ColorConvertFloat4ToU32(ImLerp(ImVec4(.75f,.77f,.80f,1),ImVec4(.325f,.439f,.827f,1),value));
        dl->AddRectFilled(P(x,y),P(x+43,y+25),bg,13*s);dl->AddCircleFilled(P(x+12.5f+18*value,y+12.5f),9.5f*s,IM_COL32_WHITE);
    };
    auto slider=[&](const char* id,const char* label,float y,float& value,float lo,float hi,const char* suffix,bool disabled=false){
        text(768,y+10,label,13,disabled?muted:ink);
        const float tx=875,tw=204,cy=y+18;
        ImGui::BeginDisabled(disabled);
        hit(id,tx-8,y,tw+16,37);
        bool active=ImGui::IsItemActive(),hover=ImGui::IsItemHovered();
        if(active){float next=lo+max(0.0f,min(1.0f,(io.MousePos.x-origin.x-tx*s)/(tw*s)))*(hi-lo);
            next=hi>1 ? roundf(next):roundf(next*1000)/1000;
            if(next!=value){value=next;changed=true;}}
        if(hover && io.MouseWheel!=0){value=max(lo,min(hi,value+io.MouseWheel*(hi>1 ? 1.0f:0.01f)));changed=true;}
        ImGui::EndDisabled();
        float knob=tx+(value-lo)/(hi-lo)*tw;
        dl->AddLine(P(tx,cy),P(tx+tw,cy),UIColor(216,220,225,disabled?120:255),4*s);
        dl->AddLine(P(tx,cy),P(knob,cy),UIColor(132,153,221,disabled?80:255),4*s);
        dl->AddCircleFilled(P(knob,cy),8*s,UIColor(251,251,247,255));
        dl->AddCircle(P(knob,cy),8*s,disabled?border:accent,0,1.6f*s);
        char number[32];snprintf(number,sizeof(number),hi<=1 ? "%.0f%s":"%.0f%s",hi<=1?value*100:value,suffix);
        text(1100,y+11,number,12,muted);
    };
    box(24,120,710,602); section(40,140,"准星预览");
    text(494,145,"自由位置下拖动预览定位",11,muted);
    const float bx=40,by=182,bw=678,bh=350;
    dl->AddRectFilled(P(bx-3,by-3),P(bx+bw+3,by+bh+3),UIColor(225,229,232,255),15*s);
    // Fill the whole card with the dedicated outdoor image; screen position
    // remains normalized over the entire preview, with no letterbox hit zones.
    const float px=bx,py=by,w=bw,h=bh,ratio=bw/bh;
    dl->AddRectFilled(P(bx,by),P(bx+bw,by+bh),UIColor(28,34,41,255),12*s);
    dl->PushClipRect(P(px,py),P(px+w,py+h),true);
    if(g_crosshairSampleSRV) {
        float source=(float)g_crosshairSampleW/max(1U,g_crosshairSampleH),u=min(1.0f,ratio/source),v=min(1.0f,source/ratio);
        dl->AddImageRounded((ImTextureID)(intptr_t)g_crosshairSampleSRV,P(px,py),P(px+w,py+h),ImVec2((1-u)*.5f,(1-v)*.5f),ImVec2((1+u)*.5f,(1+v)*.5f),IM_COL32_WHITE,12*s);
    }
    ImVec2 at=CrosshairPosition(ImVec2(w,h));
    dl->AddLine(P(px+w*.5f,py),P(px+w*.5f,py+h),UIColor(255,255,255,35));
    dl->AddLine(P(px,py+h*.5f),P(px+w,py+h*.5f),UIColor(255,255,255,35));
    DrawCrosshairShape(dl,P(px+at.x,py+at.y),g_crosshair,s*w/max(1.0f,io.DisplaySize.x));
    dl->PopClipRect();
    hit("##crossposition",px,py,w,h);
    if(!g_crosshair.centered && ImGui::IsItemActive()){
        g_crosshair.x=max(0.0f,min(1.0f,(io.MousePos.x-origin.x-px*s)/(w*s)));
        g_crosshair.y=max(0.0f,min(1.0f,(io.MousePos.y-origin.y-py*s)/(h*s)));changed=true;
    }
    text(55,197,g_crosshair.enabled ? "准星已启用":"准星预览 · 尚未启用",11,IM_COL32(240,244,250,255));
    section(40,557,"准星样式"); text(550,561,"点击卡片切换样式",11,muted);
    const char* styles[]={"十字","圆点","圆环","环形十字","T 形"};
    for(int i=0;i<5;++i){float x=40+i*138.0f,y=594;
        ImGui::PushID(700+i);bool clicked=hit("##style",x,y,126,110);bool hover=ImGui::IsItemHovered();ImGui::PopID();
        bool selected=g_crosshair.style==i;
        box(x,y,126,110,selected?UIColor(239,243,254,255):hover?UIColor(243,245,248,255):UIColor(250,250,246,255));
        if(selected)dl->AddRect(P(x,y),P(x+126,y+110),accent,14*s,0,1.7f*s);
        CrosshairParams example=g_crosshair;example.style=i;example.size=12;example.thickness=2;example.gap=4;
        DrawCrosshairShape(dl,P(x+63,y+40),example,s);
        float tw=g_fontBody->CalcTextSizeA(13*s,FLT_MAX,0,styles[i]).x;
        dl->AddText(g_fontBody,13*s,ImVec2(P(x+63,y+80).x-tw*.5f,P(x,y+80).y),selected?accent:ink,styles[i]);
        if(clicked){g_crosshair.style=i;changed=true;}
    }
    section(754,140,"准星设置");text(1018,145,"实时预览 · 自动保存",11,muted);
    box(750,182,426,54);text(768,201,"启用准星",13);toggle("##crossenabled",861,197,g_crosshair.enabled);
    text(982,201,"描边",13);toggle("##crossoutline",1115,197,g_crosshair.outline);
    box(750,248,426,82);text(768,264,"准星颜色",13);
    const ImVec4 palette[]={ImVec4(.25f,1,.5f,1),ImVec4(1,1,1,1),ImVec4(1,.25f,.30f,1),ImVec4(.25f,.75f,1,1),ImVec4(1,.85f,.15f,1),ImVec4(.9f,.4f,1,1)};
    for(int i=0;i<6;++i){float x=864+i*32.0f;ImGui::PushID(800+i);
        if(hit("##color",x,263,23,23)){g_crosshair.color[0]=palette[i].x;g_crosshair.color[1]=palette[i].y;g_crosshair.color[2]=palette[i].z;changed=true;}
        dl->AddCircleFilled(P(x+11.5f,274.5f),9*s,ImGui::ColorConvertFloat4ToU32(palette[i]));
        dl->AddCircle(P(x+11.5f,274.5f),10*s,border);
        if(fabs(g_crosshair.color[0]-palette[i].x)+fabs(g_crosshair.color[1]-palette[i].y)+fabs(g_crosshair.color[2]-palette[i].z)<.02f)
            dl->AddCircle(P(x+11.5f,274.5f),12*s,accent,0,1.4f*s);
        ImGui::PopID();}
    text(768,302,"自定义",12,muted);ImGui::SetCursorScreenPos(P(864,296));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,5*s);ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(12,12));
    changed|=ImGui::ColorEdit4("##customcrosscolor",g_crosshair.color,ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_NoLabel|ImGuiColorEditFlags_AlphaBar|ImGuiColorEditFlags_AlphaPreviewHalf);
    ImGui::PopStyleVar(2);text(900,302,"点击色块修改",11,muted);
    text(982,302,"中心原点",12);toggle("##crossdot",1115,296,g_crosshair.centerDot);
    box(750,344,426,214);
    slider("##crosssize","大小",354,g_crosshair.size,2,40," px");
    slider("##crossthickness","粗细",404,g_crosshair.thickness,1,8," px");
    slider("##crossgap","间距 (0 闭合)",454,g_crosshair.gap,0,20," px");
    slider("##crossalpha","透明度",504,g_crosshair.color[3],0,1,"%");
    box(750,570,426,134);text(768,587,"位置",13);
    static UIAnimatedValue positionAnimation(0);
    const char* modes[]={"屏幕居中","自由位置"};
    int mode=DrawAnimatedSegments("##positionmode",P(916,579),ImVec2(244,32),modes,2,g_crosshair.centered?0:1,positionAnimation,s);
    if((mode==0)!=g_crosshair.centered){g_crosshair.centered=mode==0;changed=true;}
    float displayX=g_crosshair.centered ? 0.5f:g_crosshair.x, displayY=g_crosshair.centered ? 0.5f:g_crosshair.y;
    slider("##crossx","水平位置",616,displayX,0,1,"%",g_crosshair.centered);
    slider("##crossy","垂直位置",658,displayY,0,1,"%",g_crosshair.centered);
    if(!g_crosshair.centered){g_crosshair.x=displayX;g_crosshair.y=displayY;}
    text(760,713,"位置按屏幕百分比保存 · 隐藏面板后继续显示",10,muted);
    static bool dirty=false; dirty|=changed;
    if(dirty && !ImGui::IsMouseDown(0)){ValidateCrosshair();SaveConfig();dirty=false;}
}




