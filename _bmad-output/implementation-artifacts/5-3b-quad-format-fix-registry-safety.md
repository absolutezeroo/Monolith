# Story 5.3b: Quad Format Fix + Registry Safety Hardening

Status: done

## Story

As a **developer**,
I want the quad format expanded to 16-bit state IDs and the registry access corrected,
so that the meshing pipeline doesn't break when multi-state blocks are introduced in Story 5.4.

## Acceptance Criteria

1. **AC-1: 16-bit block state ID in quad format** — `packQuad()` stores 16-bit state ID (bits 30–45) instead of 10-bit. All unpack functions and bit layout comment updated. Existing roundtrip tests pass.
2. **AC-2: `getBlockType()` used everywhere** — All engine code that reads block IDs from chunks or padded arrays uses `getBlockType(stateId)` instead of `getBlock(id)`. `getBlock()` renamed to clarify it's type-index-only.
3. **AC-3: Safe fallback in release** — `getBlockType()` returns air block (index 0) for out-of-range state IDs in release builds, with a log warning. Debug builds still assert.
4. **AC-4: AO cross-boundary test** — At least 2 new test cases verifying AO values for blocks at section boundaries with opaque neighbors in adjacent sections.

## Tasks / Subtasks

- [x] **Task 1: Expand quad format to 16-bit state ID** (AC: 1)
  - [x] Update bit layout comment in `ChunkMesh.h` to new layout
  - [x] Update `packQuad()`: mask 0xFFFF, shifts 46/49/51/53/55/57
  - [x] Update `unpackBlockStateId()`: mask `0xFFFF` instead of `0x3FF`
  - [x] Update `unpackFace()`: shift `>> 46` instead of `>> 40`, mask `0x7`
  - [x] Update `unpackAO()`: shifts to 49, 51, 53, 55
  - [x] Update `unpackFlip()`: shift `>> 57`
  - [x] Run all existing roundtrip tests — they must pass (238 assertions, 11 cases)

- [x] **Task 2: Rename `getBlock()` and migrate to `getBlockType()`** (AC: 2)
  - [x] In `BlockRegistry.h`: rename `getBlock(uint16_t id)` to `getBlockByTypeIndex(uint16_t typeIndex)`
  - [x] In `BlockRegistry.cpp`: rename the implementation accordingly
  - [x] In `MeshBuilder.cpp`: change `m_registry.getBlock(neighborId)` to `m_registry.getBlockType(neighborId)`
  - [x] In `AmbientOcclusion.h` (7 sites): change `registry.getBlock(blockId)` to `registry.getBlockType(blockId)` in `buildOpacityPad()`
  - [x] In `tests/world/TestBlockRegistry.cpp` (~30 sites): change `registry.getBlock(...)` to `registry.getBlockByTypeIndex(...)`
  - [x] Verify: no remaining `registry.getBlock(` calls in engine code (grep confirmed)

- [x] **Task 3: Safe fallback in `getBlockType()`** (AC: 3)
  - [x] In `BlockRegistry.cpp`, added bounds check with `VX_LOG_WARN` and air fallback
  - [x] `VX_ASSERT` fires in debug → catches bugs during development
  - [x] Bounds check in release → returns air instead of crashing on corrupt data
  - [x] Add test: `getBlockType(9999)` → returns air (guarded with `#ifdef NDEBUG`, debug asserts correctly)

- [x] **Task 4: AO cross-boundary tests** (AC: 4)
  - [x] Test: block at (15, 0, 0) with PosX neighbor opaque at (0, 1, 0) → +X face corners 1,2 have AO < 3
  - [x] Test: block at (0, 15, 0) with PosY neighbor opaque at (1, 0, 0) → +Y face corners 1,2 have AO < 3
  - [x] Both tests tagged `[cross-boundary]` for easy filtering

- [x] **Task 5: Update epic quad format reference table** (AC: 1)
  - [x] Updated `epic-05-meshing-pipeline.md` table to 16-bit state ID layout with note about Epic 8 impact

## Dev Notes

### Why 16 bits for state ID?

The BlockRegistry uses `uint16_t` state IDs (max 65535). A door has 32 states (facing×half×open×hinge), an escalier has 80. With 29 blocks and zero multi-state blocks today, the total is 29. But a modestly sized game with 200 block types and 50 stateful blocks could easily exceed 1024. Truncating to 10 bits would silently render the wrong block — no crash, no warning, just visual corruption. 16 bits matches the registry exactly.

### Why rename instead of delete `getBlock()`?

`getBlock(typeIndex)` is still valid for code that has a type index (e.g., from `getIdByName()` which returns type indices). Tests use this pattern. Renaming to `getBlockByTypeIndex()` makes the distinction explicit — if you have a state ID from a chunk, you must use `getBlockType()`. If you have a type index from `getIdByName()`, you use `getBlockByTypeIndex()`.

### Files modified

```
engine/include/voxel/renderer/ChunkMesh.h        — quad format (pack/unpack/comment)
engine/include/voxel/renderer/AmbientOcclusion.h  — getBlock → getBlockType (7 sites)
engine/include/voxel/world/BlockRegistry.h         — rename getBlock → getBlockByTypeIndex
engine/src/world/BlockRegistry.cpp                 — rename + safe fallback
engine/src/renderer/MeshBuilder.cpp                — getBlock → getBlockType (1 site), removed duplicate blockPadIndex() (uses padIndex from AO header), added default case to sliceToLocal switch
tests/world/TestBlockRegistry.cpp                  — getBlock → getBlockByTypeIndex (~10 sites)
tests/renderer/TestAmbientOcclusion.cpp            — add 2 cross-boundary tests
```

### What NOT to do

- **Do NOT add 26-neighbor AO support** — that's deferred to Story 5.6 where ChunkManager::update() exists
- **Do NOT change ChunkSection storage** — sections already store uint16_t, no change needed
- **Do NOT touch the greedy mesher algorithm** — it uses the opacity pad which will be corrected by the getBlockType fix
- **Do NOT change `getIdByName()`** — it returns type indices, which is correct for block registration and lookup

### Impact on Epic 8 (Lighting)

The old quad format had bits 53–60 reserved for sky light (4 bits) and block light (4 bits). The new layout uses those bits for AO corners, flip, and tint. Epic 8 Story 8.0 references packing light into "bits 49–56" — this is now invalid. Epic 8 will need to use a secondary data channel (e.g., a second `uint32_t` per quad, or a separate light SSBO indexed by quad position). This is a spec update for Epic 8, not a code change for this story.

### Existing code patterns

- `VX_ASSERT` is debug-only, defined in `engine/include/voxel/core/Assert.h`
- `VX_LOG_WARN` is always active, defined in `engine/include/voxel/core/Log.h`
- Quad format pack/unpack functions are `inline constexpr` in the header
- Test convention: `TEST_CASE("description", "[tag1][tag2]")` with `SECTION` blocks

## Dev Agent Record

### Agent Model Used

Claude Opus 4.6

### Debug Log References

- All 125 test cases pass (474,232 assertions) — zero regressions
- Cross-boundary AO tests: 2 new test cases, 8 assertions, all pass
- Roundtrip tests: 238 assertions across 11 meshing/AO test cases, all pass

### Completion Notes List

- Expanded block state ID from 10→16 bits in packed quad format, shifting face/AO/flip fields by 6 positions
- Renamed `getBlock()` → `getBlockByTypeIndex()` to clarify type-index-only usage; migrated all engine code to `getBlockType()` for state ID lookups
- Added release-safe bounds check in `getBlockType()` — returns air with `VX_LOG_WARN` for out-of-range state IDs; `VX_ASSERT` still fires in debug
- Added 2 cross-boundary AO tests verifying opacity pad correctly reads neighbor sections at section edges
- Updated epic-05 quad format reference table; added note about Epic 8 needing secondary data channel for lighting

### Change Log

- 2026-03-27: Implemented Story 5.3b — Quad format expanded to 16-bit state ID, registry access corrected, safe fallback added, cross-boundary AO tests added
- 2026-03-27: Code review fix — Removed false TestMeshBuilder.cpp reference from Dev Notes (file was not modified); documented MeshBuilder.cpp cleanup (blockPadIndex removal, default switch case); added 5-3 story and sprint-status to File List

### File List

- engine/include/voxel/renderer/ChunkMesh.h (modified — quad format pack/unpack/comment)
- engine/include/voxel/renderer/AmbientOcclusion.h (modified — getBlock → getBlockType, 7 sites)
- engine/include/voxel/world/BlockRegistry.h (modified — rename getBlock → getBlockByTypeIndex)
- engine/src/world/BlockRegistry.cpp (modified — rename + safe fallback in getBlockType)
- engine/src/renderer/MeshBuilder.cpp (modified — getBlock → getBlockType, removed duplicate blockPadIndex, added default switch case)
- tests/world/TestBlockRegistry.cpp (modified — getBlock → getBlockByTypeIndex, ~30 sites + new out-of-range test)
- tests/renderer/TestAmbientOcclusion.cpp (modified — 2 new cross-boundary tests)
- _bmad-output/planning-artifacts/epics/epic-05-meshing-pipeline.md (modified — quad format table updated)
- _bmad-output/implementation-artifacts/5-3-binary-greedy-meshing-implementation.md (modified — status → done, quad format table corrected to 5.2 layout)
- _bmad-output/implementation-artifacts/sprint-status.yaml (modified — 5.3 → done, 5.3b → review)
