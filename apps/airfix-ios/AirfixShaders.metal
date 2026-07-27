#include <metal_stdlib>

using namespace metal;

struct GpuVertex {
    float4 position;
    float4 normal;
    float2 uv;
    float2 padding;
};

struct GpuUniforms {
    float4x4 mvp;
};

struct RasterVertex {
    float4 position [[position]];
    float2 uv;
};

vertex RasterVertex airfixVertexMain(
    uint vertexId [[vertex_id]],
    constant GpuVertex* vertices [[buffer(0)]],
    constant GpuUniforms& uniforms [[buffer(1)]]) {
    RasterVertex output;
    output.position = uniforms.mvp * vertices[vertexId].position;
    output.uv = vertices[vertexId].uv;
    return output;
}

fragment float4 airfixFragmentMain(
    RasterVertex input [[stage_in]],
    texture2d<float> colorTexture [[texture(0)]],
    sampler colorSampler [[sampler(0)]]) {
    return colorTexture.sample(colorSampler, input.uv);
}
