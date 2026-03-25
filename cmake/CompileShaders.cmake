# cmake/CompileShaders.cmake — GLSL to SPIR-V compilation

find_program(GLSLANG_VALIDATOR glslangValidator)
if(NOT GLSLANG_VALIDATOR)
    find_program(GLSLANG_VALIDATOR glslc)
endif()

if(NOT GLSLANG_VALIDATOR)
    message(WARNING "Neither glslangValidator nor glslc found — shaders will NOT be compiled")
endif()

# compile_shaders(<target> <shader_dir> <output_dir>)
#   Finds all .vert, .frag, .comp files in shader_dir,
#   compiles each to .spv in output_dir, and creates a
#   custom target named <target> that depends on all outputs.
function(compile_shaders TARGET_NAME SHADER_DIR OUTPUT_DIR)
    if(NOT GLSLANG_VALIDATOR)
        return()
    endif()

    file(GLOB SHADER_SOURCES
        "${SHADER_DIR}/*.vert"
        "${SHADER_DIR}/*.frag"
        "${SHADER_DIR}/*.comp"
    )

    set(SPV_OUTPUTS "")

    foreach(SHADER ${SHADER_SOURCES})
        get_filename_component(SHADER_NAME ${SHADER} NAME)
        set(SPV_OUTPUT "${OUTPUT_DIR}/${SHADER_NAME}.spv")

        add_custom_command(
            OUTPUT ${SPV_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${OUTPUT_DIR}"
            COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.3 "${SHADER}" -o "${SPV_OUTPUT}"
            DEPENDS ${SHADER}
            COMMENT "Compiling shader ${SHADER_NAME} -> ${SHADER_NAME}.spv"
            VERBATIM
        )

        list(APPEND SPV_OUTPUTS ${SPV_OUTPUT})
    endforeach()

    add_custom_target(${TARGET_NAME} ALL DEPENDS ${SPV_OUTPUTS})
endfunction()
