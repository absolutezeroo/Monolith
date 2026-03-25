#!/usr/bin/env bash
# Compile all GLSL shaders to SPIR-V
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SHADER_DIR="$SCRIPT_DIR/../assets/shaders"

for shader in "$SHADER_DIR"/*.vert "$SHADER_DIR"/*.frag "$SHADER_DIR"/*.comp; do
    [ -f "$shader" ] || continue
    echo "Compiling $(basename "$shader")"
    glslangValidator -V --target-env vulkan1.3 "$shader" -o "${shader}.spv"
done
