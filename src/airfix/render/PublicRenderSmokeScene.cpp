#include "airfix/render/PublicRenderSmokeScene.hpp"

#include <optional>
#include <utility>

namespace airfix::render {

PublicRenderSmokeScene makePublicRenderSmokeScene() {
  // Mesh zero deliberately contains both a missing primary texture and
  // TexcoordMode::none. Backends map those explicit states to the one-pixel
  // fallback without modifying the portable payload.
  DrawMeshPayload meshZero;
  meshZero.vertices = {
      DrawVertex{Vec3{-0.7F, -0.7F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F},
                 Vec2{0.0F, 1.0F}},
      DrawVertex{Vec3{0.7F, -0.7F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F},
                 Vec2{1.0F, 1.0F}},
      DrawVertex{Vec3{0.7F, 0.7F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F},
                 Vec2{1.0F, 0.0F}},
      DrawVertex{Vec3{-0.7F, 0.7F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F},
                 Vec2{0.0F, 0.0F}},
  };
  meshZero.indices = {0U, 1U, 2U, 0U, 2U, 3U};
  meshZero.materials = {
      DrawMaterial{0U, std::nullopt, std::nullopt, std::nullopt},
      DrawMaterial{1U, TextureAssetId{0U}, std::nullopt, std::nullopt},
  };
  meshZero.ranges = {
      DrawRange{0U, 3U, 0U, TexcoordMode::uv0},
      DrawRange{3U, 3U, 1U, TexcoordMode::none},
  };
  meshZero.localBounds = {
      Vec3{-0.7F, -0.7F, 0.0F},
      Vec3{0.7F, 0.7F, 0.0F},
  };

  DrawMeshPayload meshOne;
  meshOne.vertices = {
      DrawVertex{Vec3{-0.65F, -0.55F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F},
                 Vec2{0.0F, 1.0F}},
      DrawVertex{Vec3{0.65F, -0.55F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F},
                 Vec2{1.0F, 1.0F}},
      DrawVertex{Vec3{0.0F, 0.7F, 0.0F}, Vec3{0.0F, 0.0F, 1.0F},
                 Vec2{0.5F, 0.0F}},
  };
  meshOne.indices = {0U, 1U, 2U};
  meshOne.materials = {
      DrawMaterial{2U, TextureAssetId{0U}, std::nullopt, std::nullopt},
  };
  meshOne.ranges = {
      DrawRange{0U, 3U, 0U, TexcoordMode::uv0},
  };
  meshOne.localBounds = {
      Vec3{-0.65F, -0.55F, 0.0F},
      Vec3{0.65F, 0.7F, 0.0F},
  };

  PublicRenderSmokeScene result;
  result.model.meshes.push_back(std::move(meshZero));
  result.model.meshes.push_back(std::move(meshOne));

  // The deliberately non-monotonic order proves that draw submission follows
  // instance order and rebinds reusable mesh buffers: 1, 0, 1.
  result.model.instances = {
      DrawMeshInstance{
          .meshSlot = 1U,
          .sourceNodeReference = 1U,
          .modelLinear = Mat3{{
              Vec3{0.48F, 0.0F, 0.0F},
              Vec3{0.0F, 0.48F, 0.0F},
              Vec3{0.0F, 0.0F, 0.48F},
          }},
          .modelTranslation = Vec3{-0.52F, 0.28F, 0.35F},
      },
      DrawMeshInstance{
          .meshSlot = 0U,
          .sourceNodeReference = 2U,
          .modelLinear = Mat3{{
              Vec3{0.46F, 0.0F, 0.0F},
              Vec3{0.0F, 0.46F, 0.0F},
              Vec3{0.0F, 0.0F, 0.46F},
          }},
          .modelTranslation = Vec3{0.0F, -0.3F, 0.2F},
      },
      DrawMeshInstance{
          .meshSlot = 1U,
          .sourceNodeReference = 3U,
          .modelLinear = Mat3{{
              Vec3{0.38F, 0.0F, 0.0F},
              Vec3{0.0F, 0.38F, 0.0F},
              Vec3{0.0F, 0.0F, 0.38F},
          }},
          .modelTranslation = Vec3{0.54F, 0.3F, 0.1F},
      },
  };

  result.textureRgba8 = {
      238U, 91U,  72U,  255U, 247U, 201U, 72U,  255U,
      54U,  179U, 126U, 255U, 64U,  129U, 216U, 255U,
  };
  result.fallbackRgba8 = {255U, 255U, 255U, 255U};
  return result;
}

} // namespace airfix::render
