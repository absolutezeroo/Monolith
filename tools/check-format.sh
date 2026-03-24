#!/usr/bin/env bash
# check-format.sh — Verify all C++ source files conform to .clang-format.
# Returns 0 if all files are formatted correctly, non-zero otherwise.

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# Find clang-format: prefer PATH, fall back to VS-bundled
if command -v clang-format &>/dev/null; then
    CLANG_FMT="clang-format"
elif [ -x "/c/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin/clang-format.exe" ]; then
    CLANG_FMT="/c/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin/clang-format.exe"
elif [ -x "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin/clang-format.exe" ]; then
    CLANG_FMT="C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin/clang-format.exe"
else
    echo "ERROR: clang-format not found in PATH or Visual Studio installation." >&2
    exit 1
fi

# Collect all C++ source files under engine/, game/, tests/
FILES=()
while IFS= read -r -d '' file; do
    FILES+=("$file")
done < <(find "$PROJECT_ROOT/engine" "$PROJECT_ROOT/game" "$PROJECT_ROOT/tests" \
    -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.cpp' -o -name '*.cxx' -o -name '*.inl' \) \
    -print0 2>/dev/null)

if [ ${#FILES[@]} -eq 0 ]; then
    echo "No C++ source files found."
    exit 0
fi

FAILED=0
for file in "${FILES[@]}"; do
    if ! "$CLANG_FMT" --dry-run -Werror "$file" >/dev/null 2>&1; then
        echo "NEEDS FORMATTING: $file"
        FAILED=1
    fi
done

if [ "$FAILED" -ne 0 ]; then
    echo ""
    echo "Some files need formatting. Run: clang-format -i <file>"
    exit 1
fi

echo "All ${#FILES[@]} files are correctly formatted."
exit 0
