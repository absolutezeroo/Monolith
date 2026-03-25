#!/usr/bin/env bash
# tools/setup-msvc-env.sh — Source this to set up MSVC environment in Git Bash / MSYS2.
# Usage: source tools/setup-msvc-env.sh
#
# After sourcing, cmake/ninja/cl work directly:
#   cmake --preset msvc-debug
#   cmake --build --preset msvc-debug
#   ctest --preset msvc-debug

set -euo pipefail

VSDEVCMD="C:/Program Files/Microsoft Visual Studio/18/Community/Common7/Tools/VsDevCmd.bat"

if [[ ! -f "$VSDEVCMD" ]]; then
    echo "ERROR: VsDevCmd.bat not found at: $VSDEVCMD" >&2
    return 1 2>/dev/null || exit 1
fi

echo "Loading MSVC environment..."

# Kill stale PDB server to prevent C1902 errors
taskkill //f //im mspdbsrv.exe > /dev/null 2>&1 || true

# Extract environment variables from VsDevCmd.bat and export them into bash.
# We run VsDevCmd inside cmd, then print all env vars, and parse the diff.
_before=$(env | sort)
while IFS='=' read -r key value; do
    # Only export vars that are new or changed
    export "$key=$value"
done < <(
    cmd //c "call \"$VSDEVCMD\" -arch=amd64 -no_logo >nul 2>&1 && set" 2>/dev/null |
    tr -d '\r' |
    grep '=' |
    sort |
    comm -13 <(echo "$_before") -
)
unset _before

# Add CLion's Ninja to PATH if not already available
if ! command -v ninja &>/dev/null; then
    NINJA_DIR="/c/Program Files/JetBrains/CLion 2025.3.4/bin/ninja/win/x64"
    if [[ -d "$NINJA_DIR" ]]; then
        export PATH="$NINJA_DIR:$PATH"
    fi
fi

# Verify
if command -v cl &>/dev/null && command -v ninja &>/dev/null; then
    echo "MSVC environment ready."
    echo "  cl:    $(which cl)"
    echo "  ninja: $(which ninja)"
    echo ""
    echo "Usage:"
    echo "  cmake --preset msvc-debug          # configure"
    echo "  cmake --build --preset msvc-debug   # build"
    echo "  ctest --preset msvc-debug           # test"
else
    echo "WARNING: Some tools not found in PATH." >&2
    command -v cl    &>/dev/null || echo "  MISSING: cl.exe" >&2
    command -v ninja &>/dev/null || echo "  MISSING: ninja" >&2
fi
