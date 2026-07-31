cbuffer SmokeUniforms : register(b0)
{
    row_major float4x4 modelFromLocal;
};

cbuffer GameplayUniforms : register(b1)
{
    row_major float4x4 gameplayModelFromLocal;
    float4 cameraAxisX;
    float4 cameraAxisY;
    float4 cameraAxisZ;
    float4 cameraTranslationAndInverseScaleSquared;
    float4 projection;
    float4 logicalCanvas;
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

struct GameplayRasterInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float farClipDistance : SV_ClipDistance0;
};

struct PresentationRasterInput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

cbuffer OverlayUniforms : register(b2)
{
    float4 outputAndPanelSize;
    float4 panelOrigin;
    float4 overlayTint;
};

SmokeRasterInput AirfixSmokeVS(SmokeVertexInput input)
{
    SmokeRasterInput output;
    output.position = mul(input.position, modelFromLocal);
    output.uv = input.uv;
    output.light = saturate(input.normal.z * 0.25f + 0.75f);
    return output;
}

GameplayRasterInput AirfixGameplayVS(SmokeVertexInput input)
{
    GameplayRasterInput output;
    const float3 worldPosition =
        mul(input.position, gameplayModelFromLocal).xyz;
    const float3 cameraDelta =
        worldPosition - cameraTranslationAndInverseScaleSquared.xyz;
    const float inverseScaleSquared =
        cameraTranslationAndInverseScaleSquared.w;
    const float3 cameraPosition =
        float3(
            dot(cameraDelta, cameraAxisX.xyz),
            dot(cameraDelta, cameraAxisY.xyz),
            dot(cameraDelta, cameraAxisZ.xyz))
        * inverseScaleSquared;

    const float nearDistance = projection.x;
    const float farDistance = projection.y;
    const float projectScale = projection.z;
    const float centreX = logicalCanvas.x;
    const float centreY = logicalCanvas.y;
    const float canvasWidth = logicalCanvas.z;
    const float canvasHeight = logicalCanvas.w;
    const float homogeneousW = cameraPosition.z / nearDistance;
    const float clipX =
        ((2.0f * projectScale) / canvasWidth) * cameraPosition.x
        + ((2.0f * centreX) / canvasWidth - 1.0f) * homogeneousW;
    const float clipY =
        ((2.0f * projectScale) / canvasHeight) * cameraPosition.y
        + (1.0f - (2.0f * centreY) / canvasHeight) * homogeneousW;

    // D3D11 uses the same [0,1] homogeneous depth contract as Metal here.
    // z=1 and w=Z/near reject Z<near; the explicit clip distance supplies
    // the separately recovered inclusive far plane.
    output.position = float4(clipX, clipY, 1.0f, homogeneousW);
    output.farClipDistance = farDistance - cameraPosition.z;
    output.uv = input.uv;
    return output;
}

PresentationRasterInput AirfixPresentationVS(
    uint vertexId : SV_VertexID)
{
    PresentationRasterInput output;
    const float2 uv = float2(
        (vertexId << 1U) & 2U,
        vertexId & 2U);
    output.position = float4(
        uv.x * 2.0f - 1.0f,
        1.0f - uv.y * 2.0f,
        0.0f,
        1.0f);
    output.uv = uv;
    return output;
}

PresentationRasterInput AirfixOverlayVS(
    uint vertexId : SV_VertexID)
{
    static const float2 unitPositions[6] = {
        float2(0.0f, 0.0f),
        float2(1.0f, 0.0f),
        float2(0.0f, 1.0f),
        float2(1.0f, 0.0f),
        float2(1.0f, 1.0f),
        float2(0.0f, 1.0f),
    };
    const float2 uv = unitPositions[vertexId];
    const float2 outputSize = outputAndPanelSize.xy;
    const float2 panelSize = outputAndPanelSize.zw;
    const float2 pixelPosition = panelOrigin.xy + uv * panelSize;

    PresentationRasterInput output;
    output.position = float4(
        pixelPosition.x * 2.0f / outputSize.x - 1.0f,
        1.0f - pixelPosition.y * 2.0f / outputSize.y,
        0.0f,
        1.0f);
    output.uv = uv;
    return output;
}

Texture2D colorTexture : register(t0);
SamplerState colorSampler : register(s0);

float4 AirfixSmokePS(SmokeRasterInput input) : SV_TARGET
{
    float4 color = colorTexture.Sample(colorSampler, input.uv);
    return float4(color.rgb * input.light, color.a);
}

float4 AirfixGameplayPS(GameplayRasterInput input) : SV_TARGET
{
    return colorTexture.Sample(colorSampler, input.uv);
}

float4 AirfixPresentationPS(PresentationRasterInput input) : SV_TARGET
{
    return colorTexture.Sample(colorSampler, input.uv);
}

float4 AirfixOverlayPS(PresentationRasterInput input) : SV_TARGET
{
    return colorTexture.Sample(colorSampler, input.uv) * overlayTint;
}
