#pragma once

#include <sol/forward.hpp>

#include <glm/vec3.hpp>

#include <cstdint>
#include <string>

namespace voxel::world
{
struct BlockDefinition;
class BlockRegistry;
} // namespace voxel::world

namespace voxel::scripting
{

/// Invokes Lua callbacks on BlockDefinitions in an exception-free manner.
/// Each invoker checks for null, calls the protected_function, validates the result,
/// and returns a safe default on error.
class BlockCallbackInvoker
{
public:
    BlockCallbackInvoker(sol::state& lua, world::BlockRegistry& registry);

    // --- Placement callbacks ---
    [[nodiscard]] bool invokeCanPlace(const world::BlockDefinition& def, const glm::ivec3& pos, uint32_t playerId);
    void invokeOnPlace(const world::BlockDefinition& def, const glm::ivec3& pos, uint32_t playerId);
    void invokeOnConstruct(const world::BlockDefinition& def, const glm::ivec3& pos);
    void invokeAfterPlace(const world::BlockDefinition& def, const glm::ivec3& pos, uint32_t playerId);

    // --- Destruction callbacks ---
    [[nodiscard]] bool invokeCanDig(const world::BlockDefinition& def, const glm::ivec3& pos, uint32_t playerId);
    void invokeOnDestruct(const world::BlockDefinition& def, const glm::ivec3& pos);
    [[nodiscard]] bool invokeOnDig(
        const world::BlockDefinition& def, const glm::ivec3& pos, uint16_t blockId, uint32_t playerId);
    void invokeAfterDestruct(const world::BlockDefinition& def, const glm::ivec3& pos, uint16_t oldBlockId);
    void invokeAfterDig(
        const world::BlockDefinition& def, const glm::ivec3& pos, uint16_t oldBlockId, uint32_t playerId);

    // --- Interaction callbacks ---
    void invokeOnRightclick(
        const world::BlockDefinition& def, const glm::ivec3& pos, uint16_t blockId, uint32_t playerId);
    void invokeOnPunch(
        const world::BlockDefinition& def, const glm::ivec3& pos, uint16_t blockId, uint32_t playerId);
    void invokeOnSecondaryUse(const world::BlockDefinition& def, uint32_t playerId);
    [[nodiscard]] bool invokeOnInteractStart(
        const world::BlockDefinition& def, const glm::ivec3& pos, uint32_t playerId);
    [[nodiscard]] bool invokeOnInteractStep(
        const world::BlockDefinition& def, const glm::ivec3& pos, uint32_t playerId, float elapsedSeconds);
    void invokeOnInteractStop(
        const world::BlockDefinition& def, const glm::ivec3& pos, uint32_t playerId, float elapsedSeconds);
    [[nodiscard]] bool invokeOnInteractCancel(
        const world::BlockDefinition& def,
        const glm::ivec3& pos,
        uint32_t playerId,
        float elapsedSeconds,
        const std::string& reason);

private:
    sol::state& m_lua;
    world::BlockRegistry& m_registry;
};

} // namespace voxel::scripting
