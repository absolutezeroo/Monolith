# Story 5.3b: Quad Format Fix + Registry Safety Hardening

Status: ready-for-dev

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

- [ ] **Task 1: Expand quad format to 16-bit state ID** (AC: 1)
  - [ ] Update bit layout comment in `ChunkMesh.h` to new layout:
    ```
    [0:5]   X position (6 bits)
    [6:11]  Y position (6 bits)
    [12:17] Z position (6 bits)
    [18:23] Width-1 (6 bits)
    [24:29] Height-1 (6 bits)
    [30:45] Block state ID (16 bits)   ← was 10
    [46:48] Face direction (3 bits)    ← shifted from 40
    [49:50] AO corner 0 (2 bits)       ← shifted from 43
    [51:52] AO corner 1 (2 bits)       ← shifted from 45
    [53:54] AO corner 2 (2 bits)       ← shifted from 47
    [55:56] AO corner 3 (2 bits)       ← shifted from 49
    [57]    Quad diagonal flip (1 bit) ← shifted from 51
    [58]    Is non-cubic model (1 bit) ← Story 5.4
    [59:60] Tint index (2 bits)        ← Story 5.5
    [61]    Waving flag (1 bit)        ← Story 5.5
    [62:63] Reserved (2 bits)
    ```
  - [ ] Update `packQuad()`:
    - Change `blockStateId & 0x3FF` to `blockStateId & 0xFFFF`
    - Change `<< 30` stays the same (start bit unchanged)
    - Change face shift from `<< 40` to `<< 46`
    - Change ao0 shift from `<< 43` to `<< 49`
    - Change ao1 shift from `<< 45` to `<< 51`
    - Change ao2 shift from `<< 47` to `<< 53`
    - Change ao3 shift from `<< 49` to `<< 55`
    - Change flip shift from `<< 51` to `<< 57`
  - [ ] Update `unpackBlockStateId()`: mask `0xFFFF` instead of `0x3FF`
  - [ ] Update `unpackFace()`: shift `>> 46` instead of `>> 40`, mask `0x7`
  - [ ] Update `unpackAO()`: shifts to 49, 51, 53, 55
  - [ ] Update `unpackFlip()`: shift `>> 57`
  - [ ] Run all existing roundtrip tests — they must pass

- [ ] **Task 2: Rename `getBlock()` and migrate to `getBlockType()`** (AC: 2)
  - [ ] In `BlockRegistry.h`: rename `getBlock(uint16_t id)` to `getBlockByTypeIndex(uint16_t typeIndex)` — keep it public for now (tests use it with type indices from `getIdByName()`)
  - [ ] In `BlockRegistry.cpp`: rename the implementation accordingly
  - [ ] In `MeshBuilder.cpp` line 348: change `m_registry.getBlock(neighborId)` to `m_registry.getBlockType(neighborId)`
  - [ ] In `AmbientOcclusion.h` (7 sites, lines 111/128/145/162/179/196/213): change `registry.getBlock(blockId)` to `registry.getBlockType(blockId)` in `buildOpacityPad()`
  - [ ] In `tests/world/TestBlockRegistry.cpp` (10 sites): change `registry.getBlock(...)` to `registry.getBlockByTypeIndex(...)` — these tests use type indices from `getIdByName()` so the rename is correct
  - [ ] Verify: no remaining `registry.getBlock(` or `m_registry.getBlock(` calls in engine code (grep to confirm)

- [ ] **Task 3: Safe fallback in `getBlockType()`** (AC: 3)
  - [ ] In `BlockRegistry.cpp`, update `getBlockType()`:
    ```cpp
    const BlockDefinition& BlockRegistry::getBlockType(uint16_t stateId) const
    {
        VX_ASSERT(stateId < m_stateToBlockIndex.size(), "State ID out of range");
        if (stateId >= m_stateToBlockIndex.size())
        {
            VX_LOG_WARN("getBlockType: invalid state ID {} (max {}), returning air",
                        stateId, m_stateToBlockIndex.size());
            return m_blocks[0]; // air is always index 0
        }
        uint16_t typeIndex = m_stateToBlockIndex[stateId];
        return m_blocks[typeIndex];
    }
    ```
  - [ ] `VX_ASSERT` fires in debug → catches bugs during development
  - [ ] Bounds check in release → returns air instead of crashing on corrupt data
  - [ ] Add test: `getBlockType(9999)` on a registry with < 100 state IDs → returns air, no crash

- [ ] **Task 4: AO cross-boundary tests** (AC: 4)
  - [ ] In `tests/renderer/TestAmbientOcclusion.cpp`, add:
    - Test: block at (15, 0, 0) with PosX neighbor having opaque block at (0, 1, 0) → +X face top corners have AO < 3 (the neighbor occludes from above the face)
    - Test: block at (0, 15, 0) with PosY neighbor having opaque block at (1, 0, 0) → +Y face should show some occlusion from the adjacent block above
    - These tests document current behavior (edge/corner positions in the opacity pad default to air)
    - Add `[cross-boundary]` tag for easy filtering

- [ ] **Task 5: Update epic quad format reference table** (AC: 1)
  - [ ] In `_bmad-output/planning-artifacts/epics/epic-05-meshing-pipeline.md`, update the quad format reference table at the top to match the new bit layout (already done in the epic file — verify it matches the code)

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
engine/src/renderer/MeshBuilder.cpp                — getBlock → getBlockType (1 site)
tests/world/TestBlockRegistry.cpp                  — getBlock → getBlockByTypeIndex (~10 sites)
tests/renderer/TestAmbientOcclusion.cpp            — add 2 cross-boundary tests
tests/renderer/TestMeshBuilder.cpp                 — roundtrip tests verify new bit layout
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

(to be filled by dev agent)

### Debug Log References

### Completion Notes List

### File List
