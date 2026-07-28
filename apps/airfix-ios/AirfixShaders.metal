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

struct GpuGameplayUniforms {
    float4x4 modelFromLocal;
    float4 cameraAxisX;
    float4 cameraAxisY;
    float4 cameraAxisZ;
    float4 cameraTranslationAndInverseScaleSquared;
    float4 projection;
    float4 logicalCanvas;
};

struct DiagnosticRasterVertex {
    float4 position [[position]];
    float2 uv [[user(uv)]];
};

struct GameplayRasterVertex {
    float4 position [[position]];
    float2 uv [[user(uv)]];
    float farClipDistance [[clip_distance]];
};

struct RasterFragmentInput {
    float4 position [[position]];
    float2 uv [[user(uv)]];
};

vertex DiagnosticRasterVertex airfixVertexMain(
    uint vertexId [[vertex_id]],
    constant GpuVertex* vertices [[buffer(0)]],
    constant GpuUniforms& uniforms [[buffer(1)]]) {
    DiagnosticRasterVertex output;
    output.position = uniforms.mvp * vertices[vertexId].position;
    output.uv = vertices[vertexId].uv;
    return output;
}

vertex GameplayRasterVertex airfixGameplayVertexMain(
    uint vertexId [[vertex_id]],
    constant GpuVertex* vertices [[buffer(0)]],
    constant GpuGameplayUniforms& uniforms [[buffer(1)]]) {
    GameplayRasterVertex output;
    const float3 worldPosition =
        (uniforms.modelFromLocal * vertices[vertexId].position).xyz;
    const float3 cameraDelta =
        worldPosition
        - uniforms.cameraTranslationAndInverseScaleSquared.xyz;
    const float inverseScaleSquared =
        uniforms.cameraTranslationAndInverseScaleSquared.w;
    const float3 cameraPosition =
        float3(
            dot(cameraDelta, uniforms.cameraAxisX.xyz),
            dot(cameraDelta, uniforms.cameraAxisY.xyz),
            dot(cameraDelta, uniforms.cameraAxisZ.xyz))
        * inverseScaleSquared;

    const float nearDistance = uniforms.projection.x;
    const float farDistance = uniforms.projection.y;
    const float projectScale = uniforms.projection.z;
    const float centreX = uniforms.logicalCanvas.x;
    const float centreY = uniforms.logicalCanvas.y;
    const float canvasWidth = uniforms.logicalCanvas.z;
    const float canvasHeight = uniforms.logicalCanvas.w;
    const float homogeneousW = cameraPosition.z / nearDistance;
    const float clipX =
        ((2.0f * projectScale) / canvasWidth) * cameraPosition.x
        + ((2.0f * centreX) / canvasWidth - 1.0f) * homogeneousW;
    const float clipY =
        ((2.0f * projectScale) / canvasHeight) * cameraPosition.y
        + (1.0f - (2.0f * centreY) / canvasHeight) * homogeneousW;

    // After the homogeneous divide, z is exactly near/Z. Standard Metal
    // clipping rejects Z<near because z=1 exceeds w=Z/near. The user clip
    // distance supplies the separately recovered inclusive far plane.
    output.position = float4(clipX, clipY, 1.0f, homogeneousW);
    output.farClipDistance = farDistance - cameraPosition.z;
    output.uv = vertices[vertexId].uv;
    return output;
}

fragment float4 airfixFragmentMain(
    RasterFragmentInput input [[stage_in]],
    texture2d<float> colorTexture [[texture(0)]],
    sampler colorSampler [[sampler(0)]]) {
    return colorTexture.sample(colorSampler, input.uv);
}
