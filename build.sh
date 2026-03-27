#!/usr/bin/env bash
# Build script for VoxelForge — wraps build.bat to set up MSVC environment.
# Usage: bash build.sh [target]    (default target: all)
# Example: bash build.sh VoxelTests

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cmd //c "$(cygpath -w "$SCRIPT_DIR/build.bat")" "$@"
