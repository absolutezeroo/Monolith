# Story 1.6: CI Pipeline (GitHub Actions)

Status: review

## Story

As a developer,
I want automated builds and tests on every push,
so that regressions are caught immediately on both Windows and Linux.

## Acceptance Criteria

1. GitHub Actions workflow file at `.github/workflows/ci.yml`
2. Matrix build: Windows (MSVC 2022) + Ubuntu (GCC 13)
3. Steps per job: checkout → vcpkg setup → cmake configure → build → ctest
4. Build both Debug and Release configurations (4 total jobs: 2 OS × 2 configs)
5. Test results reported in CI output (ctest `--output-on-failure`)
6. Build status badge added to `README.md` (create `README.md` if it does not exist)
7. Fail on any compiler warning (already enforced by `cmake/CompilerWarnings.cmake` via `-Werror` / `/WX`)
8. Cache vcpkg binary packages between runs for speed

## Tasks / Subtasks

- [x] Task 1: Create `.github/workflows/ci.yml` (AC: 1, 2, 3, 4, 5, 7, 8)
  - [x] 1.1 Define trigger: `push` to any branch + `pull_request` to `main`
  - [x] 1.2 Define matrix: `{os: [windows-latest, ubuntu-latest], preset: [debug, release]}` with proper mapping
  - [x] 1.3 Map presets per OS: Windows → `msvc-debug`/`msvc-release`, Ubuntu → `debug`/`release`
  - [x] 1.4 Steps: checkout, setup vcpkg with caching, cmake configure via preset, build via preset, ctest
  - [x] 1.5 Add MSVC test presets to `CMakePresets.json` (currently missing — only `debug` and `release` test presets exist)
- [x] Task 2: Add clang-format check step to CI (bonus quality gate)
  - [x] 2.1 Run `tools/check-format.sh` on Ubuntu only (clang-format available via apt)
- [x] Task 3: Create or update `README.md` with build badge (AC: 6)
  - [x] 3.1 Badge URL format: `![CI](https://github.com/<owner>/<repo>/actions/workflows/ci.yml/badge.svg)`
  - [x] 3.2 Detect actual GitHub remote URL from `git remote get-url origin` for correct badge path (no remote configured — used OWNER/REPO placeholder)

## Dev Notes

### Workflow Architecture

Use the **`lukka/run-vcpkg@v11` + `lukka/run-cmake@v10`** action pair. These are the standard for C++ projects with vcpkg on GitHub Actions:
- `run-vcpkg` handles vcpkg binary caching automatically (no manual `actions/cache` needed for vcpkg packages)
- `run-cmake` reads `CMakePresets.json` directly for configure/build/test steps
- Both parse and annotate CMake/compiler errors in the workflow summary

Alternative approach (if `lukka` actions are avoided): "pure" workflow using direct shell commands:
```
vcpkg install --triplet=... (manifest auto-install via CMAKE_TOOLCHAIN_FILE)
cmake --preset <preset>
cmake --build --preset <preset>
ctest --preset <preset>
```

### CMakePresets.json — Required Change

The current `CMakePresets.json` has test presets only for `debug` and `release` (Unix). MSVC test presets are missing. Add:

```json
{
  "name": "msvc-debug",
  "configurePreset": "msvc-debug",
  "configuration": "Debug",
  "output": { "outputOnFailure": true }
},
{
  "name": "msvc-release",
  "configurePreset": "msvc-release",
  "configuration": "Release",
  "output": { "outputOnFailure": true }
}
```

These must be added to the `"testPresets"` array. Without these, `ctest --preset msvc-debug` will fail.

### Matrix Strategy

```yaml
strategy:
  matrix:
    include:
      - os: ubuntu-latest
        preset: debug
      - os: ubuntu-latest
        preset: release
      - os: windows-latest
        preset: msvc-debug
      - os: windows-latest
        preset: msvc-release
```

Using `include` (not a full cross-product) because presets are OS-specific. The `debug`/`release` presets are for GCC/Clang, `msvc-debug`/`msvc-release` are Windows-only (they have a `condition` block in `CMakePresets.json`).

### vcpkg Setup — Critical Details

- **VCPKG_ROOT**: The `lukka/run-vcpkg` action sets `VCPKG_ROOT` env var automatically. The `CMakePresets.json` base preset references `$env{VCPKG_ROOT}` for the toolchain file — this is already correct.
- **Caching**: `run-vcpkg` uses vcpkg's built-in binary caching backed by GitHub Actions cache. No additional `actions/cache` step needed for vcpkg packages.
- **Manifest mode**: The project's `vcpkg.json` (13 dependencies) is installed automatically when CMake configures via the vcpkg toolchain file.
- **vcpkg version pinning**: Consider pinning `vcpkgGitCommitId` in the `run-vcpkg` action for reproducibility. Alternatively, add a `builtin-baseline` to `vcpkg.json`.

### Compiler & Platform Specifics

| Platform | Compiler | C++ Standard | Key Flags |
|----------|----------|-------------|-----------|
| Windows (CI) | MSVC 2022 (GitHub `windows-latest`) | C++23 (`/std:c++latest`) | `/W4 /WX /permissive- /GR-`, `_HAS_EXCEPTIONS=0` |
| Ubuntu (CI) | GCC 13 (`ubuntu-latest` ships GCC 13) | C++23 (`-std=c++23`) | `-Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti` |

**Ubuntu GCC version**: `ubuntu-latest` (currently `ubuntu-24.04`) ships GCC 13. If GCC 14 is the default, use `apt install g++-13` and set `CC=gcc-13 CXX=g++-13` in the environment, OR accept the default and update the acceptance criteria. Check the runner's default GCC version.

**MSVC version**: `windows-latest` (currently `windows-2022`) ships MSVC 2022 v17.x. This is correct per spec.

### Sanitizers in CI

The `debug` preset enables ASan + UBSan. This is fine for Ubuntu CI but **MSVC ASan may cause issues** — the `msvc-debug` preset currently does NOT enable sanitizers (no `VOXELFORGE_ENABLE_ASAN: ON`). This is correct; MSVC ASan is less mature and the debug preset condition already handles this.

### Format Check

`tools/check-format.sh` exists and returns non-zero if files need formatting. Run this in CI on Ubuntu only (clang-format required). Steps:
1. `sudo apt-get install -y clang-format` (or use the version pre-installed on the runner)
2. `chmod +x tools/check-format.sh && bash tools/check-format.sh`

Run this as a separate job or as the first step of the Ubuntu build, so format failures give fast feedback.

### README.md

No `README.md` currently exists. Create a minimal one with:
- Project title (VoxelForge)
- One-line description
- Build badge pointing to the CI workflow
- Quick build instructions referencing CMakePresets

Badge markdown format:
```markdown
[![CI](https://github.com/OWNER/REPO/actions/workflows/ci.yml/badge.svg)](https://github.com/OWNER/REPO/actions/workflows/ci.yml)
```

Detect `OWNER/REPO` from `git remote get-url origin`.

### Project Structure Notes

- `.github/workflows/ci.yml` — new file, standard GitHub Actions location
- `CMakePresets.json` — add MSVC test presets (modify existing file)
- `README.md` — new file at project root
- No changes to `engine/`, `game/`, `tests/`, or `cmake/` directories

### Anti-Patterns to Avoid

- **DO NOT** use `actions/cache` for vcpkg packages — `run-vcpkg` handles this
- **DO NOT** use `cmake -B build -S .` manually — use presets via `cmake --preset`
- **DO NOT** install vcpkg globally or add it to PATH manually — `run-vcpkg` manages this
- **DO NOT** skip the `ctest` step — tests MUST run in CI
- **DO NOT** use `continue-on-error: true` — all steps should fail the build on error
- **DO NOT** use `--no-verify` or skip any quality checks
- **DO NOT** hardcode the GitHub repo URL in the badge — derive from `git remote`

### Dependencies on Previous Stories

All of these are done and their artifacts are in the repo:
- **1.1** (CMake + vcpkg + Presets) — provides `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`
- **1.2** (.clang-format + .clang-tidy) — provides `tools/check-format.sh`, `.clang-format`
- **1.3** (Core Types) — provides `VoxelEngine` with source files to compile
- **1.4** (Logging) — provides spdlog integration
- **1.5** (Math Types) — provides additional source files and tests

### References

- [Source: _bmad-output/planning-artifacts/epics/epic-01-foundation.md — Story 1.6]
- [Source: _bmad-output/planning-artifacts/architecture.md — Build System, CI/CD sections]
- [Source: _bmad-output/project-context.md — Technology Stack, Testing Strategy]
- [Source: CMakePresets.json — existing preset definitions]
- [Source: cmake/CompilerWarnings.cmake — warning flags]
- [Source: cmake/Sanitizers.cmake — sanitizer toggles]
- [Source: tools/check-format.sh — format checking script]
- [Source: vcpkg.json — dependency manifest]

### Testing Requirements

CI itself IS the test. Verify by:
1. Push the workflow to a branch and confirm all 4 matrix jobs pass
2. Confirm vcpkg cache is populated on first run and restored on second run
3. Confirm test output shows pass count (currently: 19 test cases from math + core)
4. Confirm format check catches intentionally malformatted code
5. Confirm badge renders correctly in `README.md`

## Dev Agent Record

### Agent Model Used

Claude Opus 4.6

### Debug Log References

- Validated CMakePresets.json is valid JSON after adding MSVC test presets
- Verified `cmake --list-presets` shows all 5 configure presets correctly
- Built project with `msvc-debug` preset: VoxelEngine.lib, VoxelGame.exe, VoxelTests.exe all compile
- Ran `ctest --preset msvc-debug`: 19/19 tests pass, 0 failures
- Validated ci.yml content: all required fields (triggers, matrix, actions, format check) present

### Completion Notes List

- Created `.github/workflows/ci.yml` with 4-job matrix build (ubuntu debug/release, windows msvc-debug/msvc-release) using `lukka/run-vcpkg@v11` + `lukka/run-cmake@v10`
- Added separate `format-check` job running `tools/check-format.sh` on Ubuntu with clang-format
- Added `concurrency` group to cancel redundant runs on the same branch
- Added MSVC test presets (`msvc-debug`, `msvc-release`) to `CMakePresets.json` testPresets array with `outputOnFailure: true`
- Created `README.md` with CI badge (placeholder OWNER/REPO — no git remote configured), project description, and quick build instructions
- No remote URL detected — badge uses `OWNER/REPO` placeholder; update when remote is configured
- All 19 existing tests pass with no regressions

### Change Log

- 2026-03-24: Implemented CI pipeline (GitHub Actions), MSVC test presets, README with badge

### File List

- `.github/workflows/ci.yml` (new)
- `CMakePresets.json` (modified — added msvc-debug and msvc-release test presets)
- `README.md` (new)