cbuffer Transform : register(b0, space1) {
    float4x4 mvp;
    float4 light;
};
struct VertexIn { float3 position : TEXCOORD0; float3 normal : TEXCOORD1; float2 uv : TEXCOORD2; };
struct VertexOut { float4 position : SV_Position; float2 uv : TEXCOORD0; float shade : TEXCOORD1; float2 bgUV : TEXCOORD2; };
VertexOut VSMain(VertexIn input) {
    VertexOut output;
    output.position = mul(mvp, float4(input.position, 1));
    output.uv = input.uv;
    output.bgUV = input.normal.xy;
    output.shade = light.w + max(0, dot(input.normal, light.xyz));
    return output;
}
Texture2D image : register(t0, space2);
SamplerState imageSampler : register(s0, space2);
Texture2D<float> sceneDepth : register(t1, space2);
SamplerState depthSampler : register(s1, space2);
#ifdef ED_POST
cbuffer PostParams : register(b0, space3) {float4 content;float4 scanGrain;float4 filmTime;};
float filmNoise(float2 cell,float frame) {
    uint hash=uint(int(cell.x))*1597334677u^uint(int(cell.y))*3812015801u^uint(frame)*2798796415u;
    hash=(hash^(hash>>16))*2246822519u;hash=(hash^(hash>>13))*3266489917u;
    return float((hash^(hash>>16))&65535u)/65535.0-.5;
}
float4 PSPost(VertexOut input) : SV_Target0 {
    float4 color=image.SampleLevel(imageSampler,input.uv,0);
    const float2 pixel=input.position.xy;
    if(any(pixel<content.xy)||any(pixel>=content.xy+content.zw))return color;
    const float2 uv=(pixel-content.xy)/content.zw;
    const float lines=min(scanGrain.y,max(1.0,content.w*.5));
    color.rgb*=1.0-scanGrain.x*(0.5-0.5*cos((pixel.y-content.y-0.5)/content.w*lines*6.2831853));
    const float luma=dot(color.rgb,float3(.2126,.7152,.0722));
    const float2 centered=uv*2.0-1.0;
    const float vignette=1.0-filmTime.x*pow(saturate(dot(centered,centered)*.5),1.5);
    const float frame=floor(filmTime.z*24.0);
    const float flutter=1.0+filmTime.y*(.55*sin(filmTime.z*10.681415)+.3*sin(filmTime.z*52.150438)+.3*filmNoise(0,frame));
    const float2 grainPosition=uv*float2(960,720)/scanGrain.w;
    const float2 cell=floor(grainPosition),blend=smoothstep(0.0,1.0,frac(grainPosition));
    const float grain=lerp(lerp(filmNoise(cell,frame),filmNoise(cell+float2(1,0),frame),blend.x),
        lerp(filmNoise(cell+float2(0,1),frame),filmNoise(cell+1,frame),blend.x),blend.y);
    const float fine=filmNoise(floor(grainPosition*1.93),frame+101);
    color.rgb=color.rgb*vignette*flutter+(grain*1.3+fine*.4)*scanGrain.z*sqrt(saturate(luma*8.0));
    return float4(saturate(color.rgb),color.a);
}
#else
#ifdef ED_PARTICLE
cbuffer HudColor : register(b0, space3) { float4 hudColor; float4 particleDepthBand; };
#else
cbuffer HudColor : register(b0, space3) { float4 hudColor; };
#endif
float4 PSMain(VertexOut input) : SV_Target0 {
    float4 color = image.Sample(imageSampler, input.uv);
    clip(color.a - 0.05);
    return float4(color.rgb * input.shade, color.a) * hudColor;
}
float4 PSOpaque(VertexOut input) : SV_Target0 {
    return float4(image.Sample(imageSampler,input.uv).rgb * input.shade * hudColor.rgb,1);
}
float4 PSHud(VertexOut input) : SV_Target0 {
    return float4(hudColor.rgb,hudColor.a*image.Sample(imageSampler,input.uv).r);
}
float4 PSParticle(VertexOut input) : SV_Target0 {
#ifdef ED_PARTICLE
    if(input.position.z<=particleDepthBand.x||input.position.z>particleDepthBand.y)discard;
#endif
    return hudColor * image.Sample(imageSampler,input.uv);
}
struct BackgroundOut { float4 color : SV_Target0; float depth : SV_Depth; };
BackgroundOut PSBackground(VertexOut input) {
    BackgroundOut output;
    output.color = image.Sample(imageSampler, input.uv) * hudColor;
    output.depth = sceneDepth.SampleLevel(depthSampler, input.uv, 0);
    return output;
}

float4 PSDCMask(VertexOut input) : SV_Target0 {
    const float3 background = image.Sample(imageSampler,input.bgUV).rgb * hudColor.rgb;
    const float alpha = sceneDepth.Sample(depthSampler,input.uv);
    return float4(background,alpha);
}

#endif
