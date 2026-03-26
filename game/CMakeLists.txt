# game/CMakeLists.txt — VoxelGame executable

add_executable(VoxelGame
        src/main.cpp
        src/GameApp.cpp
)

target_link_libraries(VoxelGame
        PRIVATE VoxelEngine
)

voxelforge_set_warnings(VoxelGame)
voxelforge_enable_sanitizers(VoxelGame)
