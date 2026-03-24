# CompilerWarnings.cmake — VoxelForge
# Applies strict compiler warnings and disables exceptions/RTTI project-wide.

function(voxelforge_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /WX
            /permissive-
            /GR-          # Disable RTTI
        )
        # Disable STL exception usage on MSVC (do NOT use /EHsc-)
        target_compile_definitions(${target} PRIVATE _HAS_EXCEPTIONS=0)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Werror
            -fno-exceptions
            -fno-rtti
        )
    endif()
endfunction()
