# tests/CMakeLists.txt — VoxelTests executable

find_package(Catch2 3 REQUIRED)

add_executable(VoxelTests
    TestPlaceholder.cpp
    core/TestResult.cpp
    core/TestLog.cpp
    math/TestAABB.cpp
    math/TestCoordUtils.cpp
    renderer/TestGigabuffer.cpp
    renderer/TestStagingBuffer.cpp
)

target_link_libraries(VoxelTests
    PRIVATE VoxelEngine
    PRIVATE Catch2::Catch2WithMain
)

voxelforge_set_warnings(VoxelTests)
voxelforge_enable_sanitizers(VoxelTests)

include(CTest)
include(Catch)
catch_discover_tests(VoxelTests)
