# cmake/CompileShaders.cmake — GLSL to SPIR-V compilation

find_program(GLSLANG_VALIDATOR glslangValidator
    HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "$ENV{VULKAN_SDK}/bin"
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/glslang"
        "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/glslang"
)

set(SHADER_COMPILER_IS_GLSLC FALSE)

if(NOT GLSLANG_VALIDATOR)
    find_program(GLSLANG_VALIDATOR glslc
        HINTS
            "$ENV{VULKAN_SDK}/Bin"
            "$ENV{VULKAN_SDK}/bin"
    )
    if(GLSLANG_VALIDATOR)
        set(SHADER_COMPILER_IS_GLSLC TRUE)
    endif()
endif()

if(NOT GLSLANG_VALIDATOR)
    message(WARNING "Neither glslangValidator nor glslc found — shaders will NOT be compiled automatically. "
        "Install the Vulkan SDK or set VULKAN_SDK env var.")
endif()

# compile_shaders(<target> <shader_dir> <output_dir>)
#   Finds all .vert, .frag, .comp files in shader_dir,
#   compiles each to .spv in output_dir, and creates a
#   custom target named <target> that depends on all outputs.
#   If no shader compiler is found, creates an empty target.
function(compile_shaders TARGET_NAME SHADER_DIR OUTPUT_DIR)
    if(NOT GLSLANG_VALIDATOR)
        add_custom_target(${TARGET_NAME} ALL
            COMMENT "Shader compilation skipped — no GLSL compiler found"
        )
        return()
    endif()

    file(GLOB SHADER_SOURCES CONFIGURE_DEPENDS
        "${SHADER_DIR}/*.vert"
        "${SHADER_DIR}/*.frag"
        "${SHADER_DIR}/*.comp"
    )

    set(SPV_OUTPUTS "")

    foreach(SHADER ${SHADER_SOURCES})
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        set(SPV_OUTPUT "${OUTPUT_DIR}/${SHADER_NAME}.spv")

        if(SHADER_COMPILER_IS_GLSLC)
            # glslc uses different flags: --target-env=vulkan1.3 (with =)
            add_custom_command(
                OUTPUT ${SPV_OUTPUT}
                COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_DIR}"
                COMMAND ${GLSLANG_VALIDATOR} --target-env=vulkan1.3 "${SHADER}" -o "${SPV_OUTPUT}"
                DEPENDS ${SHADER}
                COMMENT "Compiling shader ${SHADER_NAME} -> ${SHADER_NAME}.spv (glslc)"
                VERBATIM
            )
        else()
            # glslangValidator uses -V --target-env vulkan1.3
            add_custom_command(
                OUTPUT ${SPV_OUTPUT}
                COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_DIR}"
                COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.3 "${SHADER}" -o "${SPV_OUTPUT}"
                DEPENDS ${SHADER}
                COMMENT "Compiling shader ${SHADER_NAME} -> ${SHADER_NAME}.spv"
                VERBATIM
            )
        endif()

        list(APPEND SPV_OUTPUTS ${SPV_OUTPUT})
    endforeach()

    add_custom_target(${TARGET_NAME} ALL DEPENDS ${SPV_OUTPUTS})
endfunction()