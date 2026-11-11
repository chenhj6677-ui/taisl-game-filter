// TaiCH 游戏助手 — 面板毛玻璃背景：仅对面板区域做可分离高斯模糊
// 本文件是 TaiCH 游戏助手的一部分，以 GNU GPL v3.0 发布，详见根目录 LICENSE。
// A separate, cropped, separable Gaussian pass for the UI background only.
// No filter shader, GPUParams value, capture texture or desktop pixel is changed.
static PreviewTarget g_uiBlurA,g_uiBlurB;
static ID3D11PixelShader* g_uiBlurPS=nullptr;
static ID3D11Buffer* g_uiBlurCB=nullptr;
static UINT g_uiBlurW=0,g_uiBlurH=0;
static bool g_uiBlurUnavailable=false;
struct UIBlurParams { float uvX,uvY,uvW,uvH,stepX,stepY,pad[2]; };
static const char* kUIBlurShader=R"HLSL(
cbuffer Blur : register(b0) { float4 rect; float2 axis; float2 pad; };
Texture2D image : register(t0); SamplerState linearClamp : register(s0);
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
float4 PSMain(VSOut i) : SV_TARGET {
    float2 uv=rect.xy+i.uv*rect.zw;
    float4 color=image.SampleLevel(linearClamp,uv,0)*0.2270270270;
    color+=(image.SampleLevel(linearClamp,uv+axis,0)+image.SampleLevel(linearClamp,uv-axis,0))*0.1945945946;
    color+=(image.SampleLevel(linearClamp,uv+axis*2,0)+image.SampleLevel(linearClamp,uv-axis*2,0))*0.1216216216;
    color+=(image.SampleLevel(linearClamp,uv+axis*3,0)+image.SampleLevel(linearClamp,uv-axis*3,0))*0.0540540541;
    color+=(image.SampleLevel(linearClamp,uv+axis*4,0)+image.SampleLevel(linearClamp,uv-axis*4,0))*0.0162162162;
    return float4(color.rgb,1);
})HLSL";
static void ReleaseUIBlur() {
    g_uiBlurA.Release();g_uiBlurB.Release();g_uiBlurW=g_uiBlurH=0;
    if(g_uiBlurPS){g_uiBlurPS->Release();g_uiBlurPS=nullptr;}
    if(g_uiBlurCB){g_uiBlurCB->Release();g_uiBlurCB=nullptr;}
}
static void DrawUIBlurPass(PreviewTarget& output,ID3D11ShaderResourceView* source,const UIBlurParams& params) {
    ID3D11ShaderResourceView* empty=nullptr;g_ctx->PSSetShaderResources(0,1,&empty);
    g_ctx->OMSetRenderTargets(1,&output.rtv,nullptr);
    D3D11_VIEWPORT vp={0,0,(float)g_uiBlurW,(float)g_uiBlurH,0,1};g_ctx->RSSetViewports(1,&vp);
    g_ctx->RSSetState(nullptr);g_ctx->OMSetBlendState(nullptr,nullptr,0xffffffff);g_ctx->OMSetDepthStencilState(nullptr,0);
    g_ctx->IASetInputLayout(nullptr);g_ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_ctx->UpdateSubresource(g_uiBlurCB,0,nullptr,&params,0,0);
    g_ctx->VSSetShader(g_vs,nullptr,0);g_ctx->PSSetShader(g_uiBlurPS,nullptr,0);
    g_ctx->PSSetConstantBuffers(0,1,&g_uiBlurCB);g_ctx->PSSetShaderResources(0,1,&source);g_ctx->PSSetSamplers(0,1,&g_smp);
    g_ctx->Draw(3,0);
}
static bool UpdateUIBlur(ImVec2 origin,ImVec2 size) {
    if(!g_ui.blur || !g_capSRV || !g_capW || !g_capH || g_uiBlurUnavailable)return false;
    if(!g_uiBlurPS) {
        ID3DBlob* blob=nullptr;
        HRESULT hr=D3DCompile(kUIBlurShader,strlen(kUIBlurShader),"ui-blur.hlsl",nullptr,nullptr,"PSMain","ps_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&blob,nullptr);
        if(FAILED(hr)){g_uiBlurUnavailable=true;return false;}
        hr=g_dev->CreatePixelShader(blob->GetBufferPointer(),blob->GetBufferSize(),nullptr,&g_uiBlurPS);blob->Release();
        D3D11_BUFFER_DESC desc={};desc.ByteWidth=sizeof(UIBlurParams);desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        if(FAILED(hr)||FAILED(g_dev->CreateBuffer(&desc,nullptr,&g_uiBlurCB))){ReleaseUIBlur();g_uiBlurUnavailable=true;return false;}
    }
    UINT w=max(1U,(UINT)ceilf(size.x*.5f)),h=max(1U,(UINT)ceilf(size.y*.5f));
    if(w!=g_uiBlurW || h!=g_uiBlurH) {
        g_ctx->OMSetRenderTargets(0,nullptr,nullptr);
        ID3D11ShaderResourceView* empty=nullptr;g_ctx->PSSetShaderResources(0,1,&empty);
        g_uiBlurA.Release();g_uiBlurB.Release();
        if(!CreatePreviewTarget(g_uiBlurA,w,h)||!CreatePreviewTarget(g_uiBlurB,w,h)){g_uiBlurA.Release();g_uiBlurB.Release();g_uiBlurW=g_uiBlurH=0;return false;}
        g_uiBlurW=w;g_uiBlurH=h;
    }
    ImVec2 display=ImGui::GetIO().DisplaySize;
    UIBlurParams horizontal={origin.x/display.x,origin.y/display.y,size.x/display.x,size.y/display.y,3.0f/g_capW,0,{0,0}};
    DrawUIBlurPass(g_uiBlurA,g_capSRV,horizontal);
    UIBlurParams vertical={0,0,1,1,0,2.0f/g_uiBlurH,{0,0}};
    DrawUIBlurPass(g_uiBlurB,g_uiBlurA.srv,vertical);
    ID3D11ShaderResourceView* empty=nullptr;g_ctx->PSSetShaderResources(0,1,&empty);g_ctx->OMSetRenderTargets(0,nullptr,nullptr);
    return true;
}
