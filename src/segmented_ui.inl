static int DrawAnimatedSegments(const char* id, ImVec2 pos, ImVec2 size,
    const char* const* labels, int count, int selected, UIAnimatedValue& animation, float scale) {
    ImGui::PushID(id);
    ImDrawList* dl=ImGui::GetWindowDrawList();
    auto point=[&](float x,float y){return ImVec2(pos.x+x*scale,pos.y+y*scale);};
    float cell=size.x/count;
    dl->AddRectFilled(pos,point(size.x,size.y),UIColor(235,237,240,255),12*scale);
    for(int i=0;i<count;++i) {
        ImGui::PushID(i); ImGui::SetCursorScreenPos(point(i*cell,0));
        if(ImGui::InvisibleButton("##segment",ImVec2(cell*scale,size.y*scale))) selected=i;
        if(ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            dl->AddRectFilled(point(i*cell+3,3),point((i+1)*cell-3,size.y-3),UIColor(255,255,255,75),9*scale);
        }
        ImGui::PopID();
    }
    float value=animation.Step((float)selected,ImGui::GetIO().DeltaTime);
    float x=value*cell+3;
    dl->AddRectFilled(point(x-1,5),point(x+cell-5,size.y-1),UIColor(33,43,67,14),10*scale);
    dl->AddRectFilled(point(x,3),point(x+cell-6,size.y-3),UITileWhite(),9*scale);
    for(int i=0;i<count;++i) {
        float emphasis=1-min(1.0f,fabsf(value-i));
        ImU32 color=ImGui::ColorConvertFloat4ToU32(ImLerp(ImVec4(.52f,.55f,.59f,1),UISelectedText(),emphasis));
        float tw=g_fontBody->CalcTextSizeA(13*scale,FLT_MAX,0,labels[i]).x;
        dl->AddText(g_fontBody,13*scale,ImVec2(pos.x+(i*cell+cell*.5f)*scale-tw*.5f,pos.y+(size.y-13)*.5f*scale),color,labels[i]);
    }
    ImGui::PopID(); return selected;
}


