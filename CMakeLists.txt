cmake_minimum_required(VERSION 3.25)

project(AirfixDogfighterReconstruction VERSION 0.1.0 LANGUAGES CXX)

option(AIRFIX_BUILD_TESTS "Build reconstruction tests" ON)
option(AIRFIX_BUILD_TOOLS "Build desktop reconstruction tools" ON)
option(AIRFIX_BUILD_IOS_APP "Build the data-less iOS application shell" OFF)
set(AIRFIX_IOS_DEPLOYMENT_TARGET "16.4" CACHE STRING
    "Minimum iOS version supported by the application")

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    enable_language(OBJCXX)
endif()

if(AIRFIX_BUILD_IOS_APP AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    message(FATAL_ERROR "AIRFIX_BUILD_IOS_APP requires an iOS CMake toolchain")
endif()

add_library(airfix_crypto
    src/airfix/crypto/Sha256.cpp
)
add_library(airfix::crypto ALIAS airfix_crypto)

target_include_directories(airfix_crypto
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_features(airfix_crypto PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(airfix_crypto PRIVATE /W4 /permissive-)
else()
    target_compile_options(airfix_crypto PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(airfix_runtime
    src/airfix/runtime/AppSession.cpp
)
add_library(airfix::runtime ALIAS airfix_runtime)

target_include_directories(airfix_runtime
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_features(airfix_runtime PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(airfix_runtime PRIVATE /W4 /permissive-)
else()
    target_compile_options(airfix_runtime PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(airfix_input
    src/airfix/input/ControllerInputBatchBridge.cpp
    src/airfix/input/InputRouter.cpp
)
add_library(airfix::input ALIAS airfix_input)

target_include_directories(airfix_input
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_features(airfix_input PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(airfix_input PRIVATE /W4 /permissive-)
else()
    target_compile_options(airfix_input PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(airfix_simulation
    src/airfix/simulation/LegacyAircraftDestroyedDiveAudioState.cpp
    src/airfix/simulation/LegacyAircraftEngineAudioState.cpp
    src/airfix/simulation/LegacyAircraftThrustState.cpp
    src/airfix/simulation/LegacyVehicleSleepState.cpp
    src/airfix/simulation/PlayerAircraftSimulation.cpp
)
add_library(airfix::simulation ALIAS airfix_simulation)

target_include_directories(airfix_simulation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_features(airfix_simulation PUBLIC cxx_std_20)
target_link_libraries(airfix_simulation PUBLIC airfix::input)

if(MSVC)
    target_compile_options(airfix_simulation PRIVATE /W4 /permissive-)
else()
    target_compile_options(airfix_simulation PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(airfix_udsp
    src/airfix/archive/UdspArchive.cpp
)
add_library(airfix::udsp ALIAS airfix_udsp)

target_include_directories(airfix_udsp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_features(airfix_udsp PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(airfix_udsp PRIVATE /W4 /permissive-)
else()
    target_compile_options(airfix_udsp PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(airfix_assets
    src/airfix/assets/AfChunkContainer.cpp
    src/airfix/assets/AssetResolver.cpp
    src/airfix/assets/CcfBlueprintGraph.cpp
    src/airfix/assets/CcfPlacedScene.cpp
    src/airfix/assets/CcfRoomDrawPlan.cpp
    src/airfix/assets/CcfRoomScene.cpp
    src/airfix/assets/LegacyFormats.cpp
    src/airfix/assets/MissionEntryResolver.cpp
    src/airfix/assets/MissionSetup.cpp
    src/airfix/assets/MissionWorldSpatialArena.cpp
    src/airfix/assets/MissionWorldSpatialLineTrace.cpp
    src/airfix/assets/MissionWorldRoomDrawPlan.cpp
    src/airfix/assets/MissionWorldRooms.cpp
    src/airfix/assets/WorldCcfTextureResolver.cpp
)
add_library(airfix::assets ALIAS airfix_assets)

target_include_directories(airfix_assets
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_features(airfix_assets PUBLIC cxx_std_20)
target_link_libraries(airfix_assets PUBLIC airfix::udsp)

if(MSVC)
    target_compile_options(airfix_assets PRIVATE /W4 /permissive-)
else()
    target_compile_options(airfix_assets PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(airfix_render
    src/airfix/render/CcfRoomDrawAssembly.cpp
    src/airfix/render/MissionWorldRoomDrawAssembly.cpp
    src/airfix/render/MissionWorldRoomTextureBindings.cpp
    src/airfix/render/MissionWorldRuntimeSpatialTrace.cpp
    src/airfix/render/MissionWorldRuntimeSphereCollision.cpp
    src/airfix/render/DiagnosticRasterizer.cpp
    src/airfix/render/DynamicInstancePose.cpp
    src/airfix/render/DrawMesh.cpp
    src/airfix/render/DrawSubmissionPlan.cpp
    src/airfix/render/LegacyCameraTransform.cpp
    src/airfix/render/LegacyCanvasLayout.cpp
    src/airfix/render/LegacyDepthState.cpp
    src/airfix/render/LegacyGameplayCameraChase.cpp
    src/airfix/render/LegacyGameplayCameraClipPacket.cpp
    src/airfix/render/LegacyGameplayCameraCollision.cpp
    src/airfix/render/LegacyGameplayCameraMissionRuntime.cpp
    src/airfix/render/LegacyGameplayCameraPacketExchange.cpp
    src/airfix/render/LegacyGameplayCameraPreset.cpp
    src/airfix/render/LegacyGameplayCameraPoseSnapshot.cpp
    src/airfix/render/LegacyGameplayCameraRoomState.cpp
    src/airfix/render/LegacyGameplayCameraStateOwner.cpp
    src/airfix/render/LegacyGameplayCameraStepCoordinator.cpp
    src/airfix/render/LegacyGeometry.cpp
    src/airfix/render/LegacyQuaternionRotation.cpp
    src/airfix/render/LegacyScreenProjection.cpp
    src/airfix/render/ObjectVisualDrawAssembly.cpp
    src/airfix/render/PlayerActorPoseFrame.cpp
    src/airfix/render/PlayerActorPoseRuntime.cpp
    src/airfix/render/PlayerActorSceneAssembly.cpp
    src/airfix/render/PlayerActorTextureBindings.cpp
    src/airfix/render/PlayerActorVisualDrawAssembly.cpp
    src/airfix/render/SnapshotGpuBudgetLedger.cpp
    src/airfix/render/TextureRuntimeData.cpp
    src/airfix/render/TextureRuntimePlan.cpp
)
add_library(airfix::render ALIAS airfix_render)

target_include_directories(airfix_render
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_features(airfix_render PUBLIC cxx_std_20)
target_link_libraries(airfix_render PUBLIC airfix::assets)

if(MSVC)
    target_compile_options(airfix_render PRIVATE /W4 /permissive-)
else()
    target_compile_options(airfix_render PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(airfix_io)
add_library(airfix::io ALIAS airfix_io)
if(WIN32)
    target_sources(airfix_io PRIVATE src/airfix/io/DurableFileWin32.cpp)
else()
    target_sources(airfix_io PRIVATE src/airfix/io/DurableFilePosix.cpp)
endif()
target_include_directories(airfix_io
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_features(airfix_io PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(airfix_io PRIVATE /W4 /permissive-)
else()
    target_compile_options(airfix_io PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_library(airfix_afpack
    src/airfix/package/AfPack.cpp
    src/airfix/package/AfPackActiveRecord.cpp
    src/airfix/package/AfPackInstaller.cpp
    src/airfix/package/AfPackManifest.cpp
    src/airfix/package/AfPackRecovery.cpp
    src/airfix/package/AfPackValidation.cpp
    src/airfix/package/AfPackWriter.cpp
)
add_library(airfix::afpack ALIAS airfix_afpack)

target_include_directories(airfix_afpack
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_features(airfix_afpack PUBLIC cxx_std_20)
target_link_libraries(airfix_afpack PUBLIC airfix::crypto airfix::io airfix::udsp)

if(MSVC)
    target_compile_options(airfix_afpack PRIVATE /W4 /permissive-)
else()
    target_compile_options(airfix_afpack PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(airfix_content
    src/airfix/content/MissionLoadManifest.cpp
    src/airfix/content/MissionWorldRoomPublication.cpp
    src/airfix/content/MissionWorldRoomLoader.cpp
    src/airfix/content/PlayerSpawnPoseBuilder.cpp
    src/airfix/content/VerifiedContentSession.cpp
    src/airfix/content/WorldRoomPublicationGate.cpp
    src/airfix/content/WorldRoomLoader.cpp
)
add_library(airfix::content ALIAS airfix_content)

target_include_directories(airfix_content
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_features(airfix_content PUBLIC cxx_std_20)
target_link_libraries(airfix_content
    PUBLIC airfix::afpack airfix::render airfix::simulation)

if(MSVC)
    target_compile_options(airfix_content PRIVATE /W4 /permissive-)
else()
    target_compile_options(airfix_content PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(AIRFIX_BUILD_TOOLS)
    add_executable(udsp-list tools/udsp/main.cpp)
    target_link_libraries(udsp-list PRIVATE airfix::udsp airfix::assets airfix::render)

    add_executable(afpack-create tools/afpack/main.cpp)
    target_link_libraries(afpack-create PRIVATE airfix::afpack airfix::udsp)

    add_executable(gti-preview tools/gti-preview/main.cpp)
    target_link_libraries(gti-preview PRIVATE airfix::assets airfix::udsp)

    add_executable(model-preview tools/model-preview/main.cpp)
    target_link_libraries(model-preview
        PRIVATE airfix::udsp airfix::assets airfix::render)
endif()

if(AIRFIX_BUILD_IOS_APP)
    add_subdirectory(apps/airfix-ios)
endif()

if(AIRFIX_BUILD_TESTS)
    enable_testing()
    add_executable(airfix_udsp_tests tests/UdspArchiveTests.cpp)
    target_link_libraries(airfix_udsp_tests PRIVATE airfix::udsp)
    add_test(NAME airfix_udsp_tests COMMAND airfix_udsp_tests)

    add_executable(airfix_udsp_limits_tests tests/UdspLimitsTests.cpp)
    target_link_libraries(airfix_udsp_limits_tests PRIVATE airfix::udsp)
    add_test(NAME airfix_udsp_limits_tests COMMAND airfix_udsp_limits_tests)

    add_executable(airfix_afpack_tests tests/AfPackTests.cpp)
    target_link_libraries(airfix_afpack_tests PRIVATE airfix::afpack)
    add_test(NAME airfix_afpack_tests COMMAND airfix_afpack_tests)

    add_executable(airfix_afpack_limits_tests tests/AfPackLimitsTests.cpp)
    target_link_libraries(airfix_afpack_limits_tests PRIVATE airfix::afpack)
    add_test(NAME airfix_afpack_limits_tests COMMAND airfix_afpack_limits_tests)

    add_executable(airfix_afpack_manifest_tests tests/AfPackManifestTests.cpp)
    target_link_libraries(airfix_afpack_manifest_tests PRIVATE airfix::afpack)
    add_test(NAME airfix_afpack_manifest_tests COMMAND airfix_afpack_manifest_tests)

    add_executable(airfix_afpack_validation_tests tests/AfPackValidationTests.cpp)
    target_link_libraries(airfix_afpack_validation_tests PRIVATE airfix::afpack)
    add_test(NAME airfix_afpack_validation_tests COMMAND airfix_afpack_validation_tests)

    add_executable(airfix_afpack_active_record_tests tests/AfPackActiveRecordTests.cpp)
    target_link_libraries(airfix_afpack_active_record_tests PRIVATE airfix::afpack)
    add_test(NAME airfix_afpack_active_record_tests COMMAND airfix_afpack_active_record_tests)

    add_executable(airfix_afpack_installer_tests tests/AfPackInstallerTests.cpp)
    target_link_libraries(airfix_afpack_installer_tests PRIVATE airfix::afpack)
    add_test(NAME airfix_afpack_installer_tests COMMAND airfix_afpack_installer_tests)

    add_executable(airfix_afpack_recovery_tests tests/AfPackRecoveryTests.cpp)
    target_link_libraries(airfix_afpack_recovery_tests PRIVATE airfix::afpack)
    add_test(NAME airfix_afpack_recovery_tests COMMAND airfix_afpack_recovery_tests)

    add_executable(airfix_verified_content_tests
        tests/VerifiedContentSessionTests.cpp)
    target_link_libraries(airfix_verified_content_tests
        PRIVATE airfix::content)
    add_test(NAME airfix_verified_content_tests
        COMMAND airfix_verified_content_tests)

    add_executable(airfix_world_room_loader_tests
        tests/WorldRoomLoaderTests.cpp)
    target_link_libraries(airfix_world_room_loader_tests
        PRIVATE airfix::content)
    add_test(NAME airfix_world_room_loader_tests
        COMMAND airfix_world_room_loader_tests)

    add_executable(airfix_mission_load_manifest_tests
        tests/MissionLoadManifestTests.cpp)
    target_link_libraries(airfix_mission_load_manifest_tests
        PRIVATE airfix::content)
    add_test(NAME airfix_mission_load_manifest_tests
        COMMAND airfix_mission_load_manifest_tests)

    add_executable(airfix_mission_world_room_loader_tests
        tests/MissionWorldRoomLoaderTests.cpp)
    target_link_libraries(airfix_mission_world_room_loader_tests
        PRIVATE airfix::content)
    add_test(NAME airfix_mission_world_room_loader_tests
        COMMAND airfix_mission_world_room_loader_tests)

    add_executable(airfix_mission_world_room_publication_tests
        tests/MissionWorldRoomPublicationTests.cpp)
    target_link_libraries(airfix_mission_world_room_publication_tests
        PRIVATE airfix::content)
    add_test(NAME airfix_mission_world_room_publication_tests
        COMMAND airfix_mission_world_room_publication_tests)

    add_executable(airfix_world_room_publication_gate_tests
        tests/WorldRoomPublicationGateTests.cpp)
    target_link_libraries(airfix_world_room_publication_gate_tests
        PRIVATE airfix::content)
    add_test(NAME airfix_world_room_publication_gate_tests
        COMMAND airfix_world_room_publication_gate_tests)

    add_executable(airfix_crypto_tests tests/Sha256Tests.cpp)
    target_link_libraries(airfix_crypto_tests PRIVATE airfix::crypto)
    add_test(NAME airfix_crypto_tests COMMAND airfix_crypto_tests)

    add_executable(airfix_durable_file_tests tests/DurableFileTests.cpp)
    target_link_libraries(airfix_durable_file_tests PRIVATE airfix::io)
    add_test(NAME airfix_durable_file_tests COMMAND airfix_durable_file_tests)

    add_executable(airfix_asset_tests tests/LegacyFormatsTests.cpp)
    target_link_libraries(airfix_asset_tests PRIVATE airfix::assets)
    add_test(NAME airfix_asset_tests COMMAND airfix_asset_tests)

    add_executable(airfix_afchunk_tests tests/AfChunkContainerTests.cpp)
    target_link_libraries(airfix_afchunk_tests PRIVATE airfix::assets)
    add_test(NAME airfix_afchunk_tests COMMAND airfix_afchunk_tests)

    add_executable(airfix_asset_resolver_tests tests/AssetResolverTests.cpp)
    target_link_libraries(airfix_asset_resolver_tests PRIVATE airfix::assets)
    add_test(NAME airfix_asset_resolver_tests COMMAND airfix_asset_resolver_tests)

    add_executable(airfix_mission_entry_resolver_tests
        tests/MissionEntryResolverTests.cpp)
    target_link_libraries(airfix_mission_entry_resolver_tests PRIVATE airfix::assets)
    add_test(NAME airfix_mission_entry_resolver_tests
        COMMAND airfix_mission_entry_resolver_tests)

    add_executable(airfix_mission_setup_tests
        tests/MissionSetupTests.cpp)
    target_link_libraries(airfix_mission_setup_tests PRIVATE airfix::assets)
    add_test(NAME airfix_mission_setup_tests
        COMMAND airfix_mission_setup_tests)

    add_executable(airfix_mission_world_rooms_tests
        tests/MissionWorldRoomsTests.cpp)
    target_link_libraries(airfix_mission_world_rooms_tests
        PRIVATE airfix::assets)
    add_test(NAME airfix_mission_world_rooms_tests
        COMMAND airfix_mission_world_rooms_tests)

    add_executable(airfix_mission_world_spatial_arena_tests
        tests/MissionWorldSpatialArenaTests.cpp)
    target_link_libraries(airfix_mission_world_spatial_arena_tests
        PRIVATE airfix::assets)
    add_test(NAME airfix_mission_world_spatial_arena_tests
        COMMAND airfix_mission_world_spatial_arena_tests)

    add_executable(airfix_mission_world_spatial_line_trace_tests
        tests/MissionWorldSpatialLineTraceTests.cpp)
    target_link_libraries(airfix_mission_world_spatial_line_trace_tests
        PRIVATE airfix::assets)
    add_test(NAME airfix_mission_world_spatial_line_trace_tests
        COMMAND airfix_mission_world_spatial_line_trace_tests)

    add_executable(airfix_world_ccf_texture_resolver_tests
        tests/WorldCcfTextureResolverTests.cpp)
    target_link_libraries(airfix_world_ccf_texture_resolver_tests
        PRIVATE airfix::assets)
    add_test(NAME airfix_world_ccf_texture_resolver_tests
        COMMAND airfix_world_ccf_texture_resolver_tests)

    add_executable(airfix_blueprint_graph_tests tests/CcfBlueprintGraphTests.cpp)
    target_link_libraries(airfix_blueprint_graph_tests PRIVATE airfix::assets)
    add_test(NAME airfix_blueprint_graph_tests COMMAND airfix_blueprint_graph_tests)

    add_executable(airfix_placed_scene_tests tests/CcfPlacedSceneTests.cpp)
    target_link_libraries(airfix_placed_scene_tests PRIVATE airfix::assets)
    add_test(NAME airfix_placed_scene_tests COMMAND airfix_placed_scene_tests)

    add_executable(airfix_room_scene_tests tests/CcfRoomSceneTests.cpp)
    target_link_libraries(airfix_room_scene_tests PRIVATE airfix::assets)
    add_test(NAME airfix_room_scene_tests COMMAND airfix_room_scene_tests)

    add_executable(airfix_room_draw_plan_tests
        tests/CcfRoomDrawPlanTests.cpp)
    target_link_libraries(airfix_room_draw_plan_tests PRIVATE airfix::assets)
    add_test(NAME airfix_room_draw_plan_tests
        COMMAND airfix_room_draw_plan_tests)

    add_executable(airfix_runtime_tests tests/AppSessionTests.cpp)
    target_link_libraries(airfix_runtime_tests PRIVATE airfix::runtime)
    add_test(NAME airfix_runtime_tests COMMAND airfix_runtime_tests)

    add_executable(airfix_input_tests tests/InputRouterTests.cpp)
    target_link_libraries(airfix_input_tests PRIVATE airfix::input)
    add_test(NAME airfix_input_tests COMMAND airfix_input_tests)

    add_executable(airfix_controller_input_batch_bridge_tests
        tests/ControllerInputBatchBridgeTests.cpp)
    target_link_libraries(airfix_controller_input_batch_bridge_tests
        PRIVATE airfix::input)
    add_test(NAME airfix_controller_input_batch_bridge_tests
        COMMAND airfix_controller_input_batch_bridge_tests)

    add_executable(airfix_simulation_tests
        tests/PlayerAircraftSimulationTests.cpp)
    target_link_libraries(airfix_simulation_tests
        PRIVATE airfix::simulation)
    add_test(NAME airfix_simulation_tests COMMAND airfix_simulation_tests)

    add_executable(airfix_legacy_vehicle_sleep_state_tests
        tests/LegacyVehicleSleepStateTests.cpp)
    target_link_libraries(airfix_legacy_vehicle_sleep_state_tests
        PRIVATE airfix::simulation)
    add_test(NAME airfix_legacy_vehicle_sleep_state_tests
        COMMAND airfix_legacy_vehicle_sleep_state_tests)

    add_executable(airfix_legacy_aircraft_thrust_state_tests
        tests/LegacyAircraftThrustStateTests.cpp)
    target_link_libraries(airfix_legacy_aircraft_thrust_state_tests
        PRIVATE airfix::simulation)
    add_test(NAME airfix_legacy_aircraft_thrust_state_tests
        COMMAND airfix_legacy_aircraft_thrust_state_tests)

    add_executable(airfix_legacy_aircraft_engine_audio_state_tests
        tests/LegacyAircraftEngineAudioStateTests.cpp)
    target_link_libraries(airfix_legacy_aircraft_engine_audio_state_tests
        PRIVATE airfix::simulation)
    add_test(NAME airfix_legacy_aircraft_engine_audio_state_tests
        COMMAND airfix_legacy_aircraft_engine_audio_state_tests)

    add_executable(airfix_legacy_aircraft_destroyed_dive_audio_state_tests
        tests/LegacyAircraftDestroyedDiveAudioStateTests.cpp)
    target_link_libraries(
        airfix_legacy_aircraft_destroyed_dive_audio_state_tests
        PRIVATE airfix::simulation)
    add_test(
        NAME airfix_legacy_aircraft_destroyed_dive_audio_state_tests
        COMMAND airfix_legacy_aircraft_destroyed_dive_audio_state_tests)

    add_executable(airfix_render_tests tests/LegacyGeometryTests.cpp)
    target_link_libraries(airfix_render_tests PRIVATE airfix::render)
    add_test(NAME airfix_render_tests COMMAND airfix_render_tests)

    add_executable(airfix_legacy_camera_transform_tests
        tests/LegacyCameraTransformTests.cpp)
    target_link_libraries(airfix_legacy_camera_transform_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_legacy_camera_transform_tests
        COMMAND airfix_legacy_camera_transform_tests)

    add_executable(airfix_legacy_canvas_layout_tests
        tests/LegacyCanvasLayoutTests.cpp)
    target_link_libraries(airfix_legacy_canvas_layout_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_legacy_canvas_layout_tests
        COMMAND airfix_legacy_canvas_layout_tests)

    add_executable(airfix_legacy_depth_state_tests
        tests/LegacyDepthStateTests.cpp)
    target_link_libraries(airfix_legacy_depth_state_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_legacy_depth_state_tests
        COMMAND airfix_legacy_depth_state_tests)

    add_executable(airfix_legacy_gameplay_camera_preset_tests
        tests/LegacyGameplayCameraPresetTests.cpp)
    target_link_libraries(airfix_legacy_gameplay_camera_preset_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_legacy_gameplay_camera_preset_tests
        COMMAND airfix_legacy_gameplay_camera_preset_tests)

    add_executable(airfix_legacy_gameplay_camera_chase_tests
        tests/LegacyGameplayCameraChaseTests.cpp)
    target_link_libraries(airfix_legacy_gameplay_camera_chase_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_legacy_gameplay_camera_chase_tests
        COMMAND airfix_legacy_gameplay_camera_chase_tests)

    add_executable(airfix_legacy_gameplay_camera_collision_tests
        tests/LegacyGameplayCameraCollisionTests.cpp)
    target_link_libraries(airfix_legacy_gameplay_camera_collision_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_legacy_gameplay_camera_collision_tests
        COMMAND airfix_legacy_gameplay_camera_collision_tests)

    add_executable(airfix_mission_world_runtime_spatial_trace_tests
        tests/MissionWorldRuntimeSpatialTraceTests.cpp)
    target_link_libraries(airfix_mission_world_runtime_spatial_trace_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_mission_world_runtime_spatial_trace_tests
        COMMAND airfix_mission_world_runtime_spatial_trace_tests)

    add_executable(airfix_mission_world_runtime_sphere_collision_tests
        tests/MissionWorldRuntimeSphereCollisionTests.cpp)
    target_link_libraries(airfix_mission_world_runtime_sphere_collision_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_mission_world_runtime_sphere_collision_tests
        COMMAND airfix_mission_world_runtime_sphere_collision_tests)

    add_executable(airfix_legacy_gameplay_camera_room_state_tests
        tests/LegacyGameplayCameraRoomStateTests.cpp)
    target_link_libraries(airfix_legacy_gameplay_camera_room_state_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_legacy_gameplay_camera_room_state_tests
        COMMAND airfix_legacy_gameplay_camera_room_state_tests)

    add_executable(airfix_legacy_gameplay_camera_state_owner_tests
        tests/LegacyGameplayCameraStateOwnerTests.cpp)
    target_link_libraries(
        airfix_legacy_gameplay_camera_state_owner_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_legacy_gameplay_camera_state_owner_tests
        COMMAND airfix_legacy_gameplay_camera_state_owner_tests)
    set_tests_properties(
        airfix_legacy_gameplay_camera_state_owner_tests
        PROPERTIES TIMEOUT 30)

    add_executable(airfix_legacy_gameplay_camera_pose_snapshot_tests
        tests/LegacyGameplayCameraPoseSnapshotTests.cpp)
    target_link_libraries(
        airfix_legacy_gameplay_camera_pose_snapshot_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_legacy_gameplay_camera_pose_snapshot_tests
        COMMAND airfix_legacy_gameplay_camera_pose_snapshot_tests)

    add_executable(airfix_legacy_gameplay_camera_clip_packet_tests
        tests/LegacyGameplayCameraClipPacketTests.cpp)
    target_link_libraries(
        airfix_legacy_gameplay_camera_clip_packet_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_legacy_gameplay_camera_clip_packet_tests
        COMMAND airfix_legacy_gameplay_camera_clip_packet_tests)

    add_executable(airfix_legacy_gameplay_camera_packet_exchange_tests
        tests/LegacyGameplayCameraPacketExchangeTests.cpp)
    target_link_libraries(
        airfix_legacy_gameplay_camera_packet_exchange_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_legacy_gameplay_camera_packet_exchange_tests
        COMMAND airfix_legacy_gameplay_camera_packet_exchange_tests)

    add_executable(airfix_legacy_gameplay_camera_mission_runtime_tests
        tests/LegacyGameplayCameraMissionRuntimeTests.cpp)
    target_link_libraries(
        airfix_legacy_gameplay_camera_mission_runtime_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_legacy_gameplay_camera_mission_runtime_tests
        COMMAND airfix_legacy_gameplay_camera_mission_runtime_tests)

    add_executable(airfix_legacy_gameplay_camera_step_coordinator_tests
        tests/LegacyGameplayCameraStepCoordinatorTests.cpp)
    target_link_libraries(
        airfix_legacy_gameplay_camera_step_coordinator_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_legacy_gameplay_camera_step_coordinator_tests
        COMMAND airfix_legacy_gameplay_camera_step_coordinator_tests)

    add_executable(airfix_legacy_quaternion_rotation_tests
        tests/LegacyQuaternionRotationTests.cpp)
    target_link_libraries(airfix_legacy_quaternion_rotation_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_legacy_quaternion_rotation_tests
        COMMAND airfix_legacy_quaternion_rotation_tests)

    add_executable(airfix_legacy_screen_projection_tests
        tests/LegacyScreenProjectionTests.cpp)
    target_link_libraries(airfix_legacy_screen_projection_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_legacy_screen_projection_tests
        COMMAND airfix_legacy_screen_projection_tests)

    add_executable(airfix_dynamic_instance_pose_tests
        tests/DynamicInstancePoseTests.cpp)
    target_link_libraries(airfix_dynamic_instance_pose_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_dynamic_instance_pose_tests
        COMMAND airfix_dynamic_instance_pose_tests)
    set_tests_properties(airfix_dynamic_instance_pose_tests
        PROPERTIES TIMEOUT 30)

    add_executable(airfix_snapshot_gpu_budget_ledger_tests
        tests/SnapshotGpuBudgetLedgerTests.cpp)
    target_link_libraries(airfix_snapshot_gpu_budget_ledger_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_snapshot_gpu_budget_ledger_tests
        COMMAND airfix_snapshot_gpu_budget_ledger_tests)

    add_executable(airfix_draw_mesh_tests tests/DrawMeshTests.cpp)
    target_link_libraries(airfix_draw_mesh_tests PRIVATE airfix::render)
    add_test(NAME airfix_draw_mesh_tests COMMAND airfix_draw_mesh_tests)

    add_executable(airfix_draw_submission_plan_tests
        tests/DrawSubmissionPlanTests.cpp)
    target_link_libraries(airfix_draw_submission_plan_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_draw_submission_plan_tests
        COMMAND airfix_draw_submission_plan_tests)

    add_executable(airfix_room_draw_assembly_tests
        tests/CcfRoomDrawAssemblyTests.cpp)
    target_link_libraries(airfix_room_draw_assembly_tests PRIVATE airfix::render)
    add_test(NAME airfix_room_draw_assembly_tests
        COMMAND airfix_room_draw_assembly_tests)

    add_executable(airfix_object_visual_draw_assembly_tests
        tests/ObjectVisualDrawAssemblyTests.cpp)
    target_link_libraries(airfix_object_visual_draw_assembly_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_object_visual_draw_assembly_tests
        COMMAND airfix_object_visual_draw_assembly_tests)

    add_executable(airfix_player_actor_visual_draw_assembly_tests
        tests/PlayerActorVisualDrawAssemblyTests.cpp)
    target_link_libraries(airfix_player_actor_visual_draw_assembly_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_player_actor_visual_draw_assembly_tests
        COMMAND airfix_player_actor_visual_draw_assembly_tests)

    add_executable(airfix_player_actor_scene_assembly_tests
        tests/PlayerActorSceneAssemblyTests.cpp)
    target_link_libraries(airfix_player_actor_scene_assembly_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_player_actor_scene_assembly_tests
        COMMAND airfix_player_actor_scene_assembly_tests)

    add_executable(airfix_player_actor_pose_frame_tests
        tests/PlayerActorPoseFrameTests.cpp)
    target_link_libraries(airfix_player_actor_pose_frame_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_player_actor_pose_frame_tests
        COMMAND airfix_player_actor_pose_frame_tests)

    add_executable(airfix_player_actor_pose_runtime_tests
        tests/PlayerActorPoseRuntimeTests.cpp)
    target_link_libraries(airfix_player_actor_pose_runtime_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_player_actor_pose_runtime_tests
        COMMAND airfix_player_actor_pose_runtime_tests)

    add_executable(airfix_player_actor_texture_bindings_tests
        tests/PlayerActorTextureBindingsTests.cpp)
    target_link_libraries(airfix_player_actor_texture_bindings_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_player_actor_texture_bindings_tests
        COMMAND airfix_player_actor_texture_bindings_tests)

    add_executable(airfix_mission_world_room_draw_tests
        tests/MissionWorldRoomDrawTests.cpp)
    target_link_libraries(airfix_mission_world_room_draw_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_mission_world_room_draw_tests
        COMMAND airfix_mission_world_room_draw_tests)

    add_executable(airfix_mission_world_room_texture_binding_tests
        tests/MissionWorldRoomTextureBindingTests.cpp)
    target_link_libraries(
        airfix_mission_world_room_texture_binding_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_mission_world_room_texture_binding_tests
        COMMAND airfix_mission_world_room_texture_binding_tests)

    add_executable(airfix_texture_runtime_plan_tests
        tests/TextureRuntimePlanTests.cpp)
    target_link_libraries(airfix_texture_runtime_plan_tests PRIVATE airfix::render)
    add_test(NAME airfix_texture_runtime_plan_tests
        COMMAND airfix_texture_runtime_plan_tests)

    add_executable(airfix_texture_runtime_data_tests
        tests/TextureRuntimeDataTests.cpp)
    target_link_libraries(airfix_texture_runtime_data_tests
        PRIVATE airfix::render)
    add_test(NAME airfix_texture_runtime_data_tests
        COMMAND airfix_texture_runtime_data_tests)

    add_executable(airfix_diagnostic_rasterizer_tests
        tests/DiagnosticRasterizerTests.cpp)
    target_link_libraries(airfix_diagnostic_rasterizer_tests PRIVATE airfix::render)
    add_test(NAME airfix_diagnostic_rasterizer_tests
        COMMAND airfix_diagnostic_rasterizer_tests)
endif()
