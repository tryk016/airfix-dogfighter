cbuffer SmokeUniforms : register(b0)
{
    row_major float4x4 modelFromLocal;
};

struct SmokeVertexInput
{
    float4 position : POSITION;
    float4 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct SmokeRasterInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float light : TEXCOORD1;
};

SmokeRasterInput AirfixSmokeVS(SmokeVertexInput input)
{
    SmokeRasterInput output;
    output.position = mul(input.position, modelFromLocal);
    output.uv = input.uv;
    output.light = saturate(input.normal.z * 0.25f + 0.75f);
    return output;
}

Texture2D colorTexture : register(t0);
SamplerState colorSampler : register(s0);

float4 AirfixSmokePS(SmokeRasterInput input) : SV_TARGET
{
    float4 color = colorTexture.Sample(colorSampler, input.uv);
    return float4(color.rgb * input.light, color.a);
}
