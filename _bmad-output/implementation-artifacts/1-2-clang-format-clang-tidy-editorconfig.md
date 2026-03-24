# Story 1.2: .clang-format + .clang-tidy + .editorconfig

Status: done

---

## Story

As a **developer**,
I want **code formatting and linting enforced by tooling**,
so that **all code follows the project conventions automatically**.

## Acceptance Criteria

1. `.clang-format` at project root matching the config in `project-context.md` (Microsoft base, Allman braces, 120 col, include regroup)
2. `.clang-tidy` at project root matching the config in `project-context.md` (naming checks, bugprone, modernize, performance)
3. `.editorconfig` at project root matching the config in `project-context.md`
4. Running `clang-format -i` on any source file produces no changes after initial format
5. A format check script `tools/check-format.sh` that returns non-zero if any file needs formatting

## Tasks / Subtasks

- [x] Task 1: Create `.clang-format` at project root (AC: #1)
  - [x] 1.1 Use the exact YAML config from the Dev Notes section below
  - [x] 1.2 Verify the file is valid by running `clang-format --dump-config` from project root
- [x] Task 2: Create `.clang-tidy` at project root (AC: #2)
  - [x] 2.1 Use the exact YAML config from the Dev Notes section below
  - [x] 2.2 Verify `Checks` string has no syntax errors (no trailing commas, proper negation with `-`)
- [x] Task 3: Create `.editorconfig` at project root (AC: #3)
  - [x] 3.1 Use the exact INI config from the Dev Notes section below
  - [x] 3.2 Ensure `root = true` is first line (stops editor traversal)
- [x] Task 4: Format all existing source files (AC: #4)
  - [x] 4.1 Run `clang-format -i` on all `.cpp` and `.h` files under `engine/`, `game/`, `tests/`
  - [x] 4.2 Verify idempotency: run `clang-format --dry-run -Werror` — must produce zero warnings
- [x] Task 5: Create `tools/check-format.sh` (AC: #5)
  - [x] 5.1 Create `tools/` directory
  - [x] 5.2 Write bash script that finds all `.h`, `.hpp`, `.cpp`, `.cxx`, `.inl` files under `engine/`, `game/`, `tests/`
  - [x] 5.3 Script runs `clang-format --dry-run -Werror` on each file
  - [x] 5.4 Script exits non-zero if any file needs formatting
  - [x] 5.5 Make script executable (`chmod +x`)
  - [x] 5.6 Test: run the script — must exit 0 (all files already formatted from Task 4)

## Dev Notes

### Exact `.clang-format` Config

Copy this verbatim to `.clang-format` at project root:

```yaml
Language: Cpp
BasedOnStyle: Microsoft
Standard: c++20
ColumnLimit: 120
IndentWidth: 4
UseTab: Never
BreakBeforeBraces: Allman
PointerAlignment: Left
DerivePointerAlignment: false
AllowShortFunctionsOnASingleLine: Inline
AllowShortLambdasOnASingleLine: Inline
AllowShortIfStatementsOnASingleLine: Never
AllowShortLoopsOnASingleLine: false
AllowShortBlocksOnASingleLine: Empty
NamespaceIndentation: None
CompactNamespaces: false
FixNamespaceComments: true
SortIncludes: CaseSensitive
IncludeBlocks: Regroup
IncludeCategories:
  - Regex: '^"voxel/'
    Priority: 1
  - Regex: '^<(entt|sol|glm|vulkan|VkBootstrap|vk_mem_alloc|volk|glfw|spdlog|fmt|imgui|enkiTS|FastNoiseLite|catch2|lz4)'
    Priority: 2
  - Regex: '^<'
    Priority: 3
BinPackArguments: false
BinPackParameters: false
AlignAfterOpenBracket: AlwaysBreak
BreakConstructorInitializers: BeforeComma
PackConstructorInitializers: NextLine
Cpp11BracedListStyle: true
SpaceAfterCStyleCast: false
SpaceBeforeParens: ControlStatements
EmptyLineAfterAccessModifier: Never
EmptyLineBeforeAccessModifier: LogicalBlock
SeparateDefinitionBlocks: Leave
```

**Source**: [project-context.md — .clang-format section]

### Exact `.clang-tidy` Config

Copy this verbatim to `.clang-tidy` at project root:

```yaml
Checks: >
  -*,
  bugprone-*,
  -bugprone-easily-swappable-parameters,
  clang-analyzer-*,
  cppcoreguidelines-*,
  -cppcoreguidelines-avoid-magic-numbers,
  -cppcoreguidelines-pro-type-reinterpret-cast,
  -cppcoreguidelines-owning-memory,
  misc-*,
  -misc-non-private-member-variables-in-classes,
  modernize-*,
  -modernize-use-trailing-return-type,
  -modernize-use-nodiscard,
  performance-*,
  readability-*,
  -readability-magic-numbers,
  -readability-identifier-length,
  -readability-function-cognitive-complexity

WarningsAsErrors: ''

CheckOptions:
  - key: readability-identifier-naming.ClassCase
    value: CamelCase
  - key: readability-identifier-naming.FunctionCase
    value: camelBack
  - key: readability-identifier-naming.VariableCase
    value: camelBack
  - key: readability-identifier-naming.MemberPrefix
    value: 'm_'
  - key: readability-identifier-naming.ConstantCase
    value: UPPER_CASE
  - key: readability-identifier-naming.EnumConstantCase
    value: CamelCase
  - key: readability-identifier-naming.NamespaceCase
    value: lower_case
  - key: readability-identifier-naming.TemplateParameterCase
    value: CamelCase
  - key: cppcoreguidelines-special-member-functions.AllowSoleDefaultDtor
    value: true
  - key: performance-move-const-arg.CheckTriviallyCopyableMove
    value: false
```

**Source**: [project-context.md — .clang-tidy section]

**Why these checks are disabled:**
- `-bugprone-easily-swappable-parameters`: Too noisy for math-heavy code (e.g., `int x, int y, int z`)
- `-cppcoreguidelines-avoid-magic-numbers` / `-readability-magic-numbers`: Voxel engine uses numeric constants everywhere (16, 256, etc.)
- `-cppcoreguidelines-pro-type-reinterpret-cast`: Needed for Vulkan/VMA interop and packed bit manipulation
- `-cppcoreguidelines-owning-memory`: Project uses `std::unique_ptr`/RAII, not GSL `owner<T>`
- `-misc-non-private-member-variables-in-classes`: POD components (ECS) use public members by design
- `-modernize-use-trailing-return-type`: Team preference for traditional syntax
- `-modernize-use-nodiscard`: Would be too noisy to enable retroactively
- `-readability-identifier-length`: Short loop vars (`i`, `x`, `y`, `z`) are idiomatic in voxel code
- `-readability-function-cognitive-complexity`: Meshing/noise algorithms have inherent complexity

### Exact `.editorconfig` Config

Copy this verbatim to `.editorconfig` at project root:

```ini
root = true

[*]
charset = utf-8
end_of_line = lf
insert_final_newline = true
trim_trailing_whitespace = true

[*.{h,hpp,cpp,cxx,inl}]
indent_style = space
indent_size = 4
max_line_length = 120

[*.{json,yaml,yml,toml}]
indent_style = space
indent_size = 2

[CMakeLists.txt]
indent_style = space
indent_size = 4

[*.cmake]
indent_style = space
indent_size = 4

[*.md]
trim_trailing_whitespace = false

[*.{glsl,vert,frag,comp,geom}]
indent_style = space
indent_size = 4

[*.lua]
indent_style = space
indent_size = 4

[Makefile]
indent_style = tab
```

**Source**: [project-context.md — .editorconfig section]

### `tools/check-format.sh` Script Requirements

The script must:
1. Find all C++ source files (`.h`, `.hpp`, `.cpp`, `.cxx`, `.inl`) under `engine/`, `game/`, `tests/`
2. Run `clang-format --dry-run -Werror` on each
3. Exit 0 if all files are formatted correctly, non-zero otherwise
4. Print which files need formatting on failure
5. Use `#!/usr/bin/env bash` shebang for portability
6. Work on both Linux and MSYS2/Git Bash on Windows (the project's shell environment)

**Important**: Use `find` with explicit directory list to avoid scanning `build/`, `_bmad/`, `third_party/`, `vcpkg_installed/`.

### Existing Source Files to Format

These are ALL current `.cpp` and `.h` files in the project (from Story 1.1):

| File | Content | Notes |
|------|---------|-------|
| `engine/src/Placeholder.cpp` | Comment-only placeholder | May produce empty-file result after format |
| `game/src/main.cpp` | `int main() { return 0; }` | Allman braces will apply |
| `tests/TestPlaceholder.cpp` | Catch2 basic test | Include + TEST_CASE macro |

**There are currently NO `.h` files** — those come in Story 1.3+. The format config must be correct for when headers are added.

### Critical: Line Endings

`.editorconfig` specifies `end_of_line = lf`. On Windows (MSYS2 environment), ensure:
- All newly created config files use LF line endings, not CRLF
- Git should handle this via `.gitattributes` if present, otherwise the dev agent should verify
- `clang-format` on Windows does NOT change line endings — this is handled by editor/git

### Previous Story Intelligence

From Story 1.1 implementation:
- **Build system is fully operational**: MSVC 2022 with msvc-debug preset verified working
- **MSVC-specific gotchas**: `/GR-` for no-RTTI, `_HAS_EXCEPTIONS=0` — clang-tidy's naming checks should not conflict with these
- **PCH is configured**: `engine/CMakeLists.txt` has precompiled headers — clang-tidy should work alongside PCH (no special configuration needed)
- **compile_commands.json**: Generated with Ninja/Makefiles generator; VS generator doesn't produce it. clang-tidy REQUIRES `compile_commands.json` — dev agent should note this in completion notes if VS generator is used
- **Project compiles clean** with `-Wall -Wextra -Wpedantic -Werror` — adding clang-tidy should not introduce new issues on the existing minimal codebase

### Files to Create/Modify

| File | Action | Description |
|------|--------|-------------|
| `.clang-format` | CREATE | Project root, exact config above |
| `.clang-tidy` | CREATE | Project root, exact config above |
| `.editorconfig` | CREATE | Project root, exact config above |
| `tools/check-format.sh` | CREATE | Format verification script |
| `engine/src/Placeholder.cpp` | MODIFY (format) | Apply clang-format |
| `game/src/main.cpp` | MODIFY (format) | Apply clang-format (Allman braces) |
| `tests/TestPlaceholder.cpp` | MODIFY (format) | Apply clang-format |

### Architecture Compliance

- **File locations**: All three config files go at project root (per architecture.md project tree)
- **`tools/` directory**: Per architecture project tree, `tools/` exists for utility scripts
- **No code in this story**: Config files + one shell script only — no C++ code to write
- **Naming**: Config files are dot-files (standard tooling names, not PascalCase)

### Project Structure Notes

- `tools/` directory does NOT exist yet — must be created
- All config files at project root, alongside `CMakeLists.txt` and `vcpkg.json`
- No other files or directories need to be created

### References

- [Source: _bmad-output/project-context.md — .clang-format, .clang-tidy, .editorconfig sections]
- [Source: _bmad-output/planning-artifacts/architecture.md — Project Tree]
- [Source: _bmad-output/planning-artifacts/epics/epic-01-foundation.md — Story 1.2]
- [Source: _bmad-output/implementation-artifacts/1-1-cmake-vcpkg-presets-setup.md — Previous story learnings]
- [Source: CLAUDE.md — Naming Conventions, Project Structure]

## Dev Agent Record

### Agent Model Used

Claude Opus 4.6

### Debug Log References

- clang-format validated via `clang-format --dump-config` (VS 2022 bundled, x64)
- Existing 3 source files were already Allman-compliant — `clang-format -i` produced no diff
- `clang-format --dry-run -Werror` confirmed idempotency (exit 0)
- `check-format.sh` tested both positive (exit 0) and negative (exit 1 on intentionally malformed file) cases
- Full project build verified with `cmake --preset msvc-debug` + `cmake --build` — no regressions
- Catch2 test suite passes (1 assertion in 1 test case)

### Completion Notes List

- All three config files (`.clang-format`, `.clang-tidy`, `.editorconfig`) created verbatim from project-context.md specs
- Existing source files were already conformant — no formatting changes needed
- `tools/check-format.sh` auto-discovers VS-bundled clang-format on Windows (MSYS2) and falls back gracefully
- Note: clang-tidy requires `compile_commands.json` which is only generated by Ninja/Makefiles generators, not the VS generator. The msvc-debug preset uses VS generator, so clang-tidy cannot be run against this preset. Future stories using Ninja preset will enable clang-tidy integration.
- No C++ code was written — this story is config-only + one shell script

### File List

| File | Action |
|------|--------|
| `.clang-format` | CREATED |
| `.clang-tidy` | CREATED |
| `.editorconfig` | CREATED |
| `tools/check-format.sh` | CREATED |

### Change Log

- 2026-03-24: Story 1.2 implemented — created .clang-format, .clang-tidy, .editorconfig config files and tools/check-format.sh verification script. All existing source files verified as conformant.
- 2026-03-24: Code review (AI) — all 5 ACs verified, all tasks confirmed done. Fixed 2 LOW issues: converted all 4 new files from CRLF to LF line endings (matching .editorconfig `end_of_line = lf`). Recommended `.gitattributes` with `*.sh text eol=lf` as follow-up in Story 1.6 (CI). Status → done.