# Sanitizers.cmake — VoxelForge
# Optional sanitizer toggles via CMake options.

option(VOXELFORGE_ENABLE_ASAN  "Enable AddressSanitizer"           OFF)
option(VOXELFORGE_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(VOXELFORGE_ENABLE_TSAN  "Enable ThreadSanitizer"            OFF)

function(voxelforge_enable_sanitizers target)
    if(MSVC)
        if(VOXELFORGE_ENABLE_ASAN)
            target_compile_options(${target} PRIVATE /fsanitize=address)
        endif()
        # MSVC does not support UBSan or TSan
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        if(VOXELFORGE_ENABLE_ASAN)
            target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address)
        endif()
        if(VOXELFORGE_ENABLE_UBSAN)
            target_compile_options(${target} PRIVATE -fsanitize=undefined)
            target_link_options(${target} PRIVATE -fsanitize=undefined)
        endif()
        if(VOXELFORGE_ENABLE_TSAN)
            target_compile_options(${target} PRIVATE -fsanitize=thread)
            target_link_options(${target} PRIVATE -fsanitize=thread)
        endif()
    endif()
endfunction()
