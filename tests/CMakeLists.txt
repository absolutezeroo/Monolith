# tests/CMakeLists.txt — VoxelTests executable

find_package(Catch2 3 REQUIRED)

add_executable(VoxelTests
    TestPlaceholder.cpp
    core/TestResult.cpp
    core/TestLog.cpp
    core/TestConcurrentQueue.cpp
    core/TestJobSystem.cpp
    math/TestAABB.cpp
    math/TestCoordUtils.cpp
    renderer/TestGigabuffer.cpp
    renderer/TestStagingBuffer.cpp
    renderer/TestCamera.cpp
    renderer/TestMeshBuilder.cpp
    renderer/TestAmbientOcclusion.cpp
    renderer/TestGreedyMeshing.cpp
    renderer/TestNonCubicMeshing.cpp
    renderer/TestTintWaving.cpp
    renderer/TestAsyncMeshing.cpp
    renderer/TestChunkUpload.cpp
    renderer/TestQuadIndexBuffer.cpp
    renderer/TestMeshingLight.cpp
    world/TestChunkSection.cpp
    world/TestChunkColumn.cpp
    world/TestBlockRegistry.cpp
    world/TestLightMap.cpp
    world/TestChunkManager.cpp
    world/TestPaletteCompression.cpp
    world/TestChunkSerializer.cpp
    world/TestBiomeSystem.cpp
    world/TestCaveCarver.cpp
    world/TestSplineCurve.cpp
    world/TestStructureGenerator.cpp
    world/TestBlockLightPropagator.cpp
    world/TestDynamicLightUpdater.cpp
    world/TestSkyLightPropagator.cpp
    world/TestWorldGenerator.cpp
    game/TestCommandQueue.cpp
    game/TestEventBus.cpp
    game/TestPlayerController.cpp
    game/TestMiningState.cpp
    game/TestBlockPlacement.cpp
    physics/TestRaycast.cpp
    scripting/TestScriptEngine.cpp
    scripting/TestLuaBindings.cpp
    scripting/TestBlockInteraction.cpp
    scripting/TestBlockTimers.cpp
    scripting/TestNeighborShape.cpp
)

target_link_libraries(VoxelTests
    PRIVATE VoxelEngine
    PRIVATE Catch2::Catch2WithMain
)

voxelforge_set_warnings(VoxelTests)
voxelforge_enable_sanitizers(VoxelTests)

# Provide path to assets directory for tests that load data files
target_compile_definitions(VoxelTests PRIVATE
    VOXELFORGE_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
)

include(CTest)
include(Catch)
catch_discover_tests(VoxelTests)
