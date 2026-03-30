#pragma once

#include "voxel/math/AABB.h"
#include "voxel/world/ItemStack.h"

#include <sol/forward.hpp>

#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace voxel::world
{
struct BlockDefinition;
class BlockRegistry;
} // namespace voxel::world

namespace voxel::scripting
{

class EntityHandle;

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

    // --- Timer callbacks ---
    [[nodiscard]] bool invokeOnTimer(
        const world::BlockDefinition& def, const glm::ivec3& pos, float elapsed);

    // --- ABM/LBM action callbacks (standalone functions, not per-block) ---
    void invokeABMAction(
        const sol::protected_function& action,
        const glm::ivec3& pos,
        uint16_t blockId,
        int activeObjectCount);
    void invokeLBMAction(
        const sol::protected_function& action, const glm::ivec3& pos, uint16_t blockId, float dtimeS);

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

    // --- Entity-block interaction callbacks ---
    void invokeOnEntityInside(const world::BlockDefinition& def, const glm::ivec3& pos, EntityHandle& entity);
    void invokeOnEntityStepOn(const world::BlockDefinition& def, const glm::ivec3& pos, EntityHandle& entity);
    [[nodiscard]] float invokeOnEntityFallOn(
        const world::BlockDefinition& def,
        const glm::ivec3& pos,
        EntityHandle& entity,
        float fallDistance);
    void invokeOnEntityCollide(
        const world::BlockDefinition& def,
        const glm::ivec3& pos,
        EntityHandle& entity,
        const std::string& facing,
        const glm::vec3& velocity,
        bool isImpact);

    // --- Neighbor change callbacks ---
    void invokeOnNeighborChanged(
        const world::BlockDefinition& def,
        const glm::ivec3& pos,
        const glm::ivec3& neighborPos,
        const std::string& neighborNode);
    [[nodiscard]] std::optional<sol::object> invokeUpdateShape(
        const world::BlockDefinition& def,
        const glm::ivec3& pos,
        const std::string& direction,
        sol::object neighborState);
    [[nodiscard]] bool invokeCanSurvive(const world::BlockDefinition& def, const glm::ivec3& pos);

    // --- Inventory callbacks ---
    [[nodiscard]] int invokeAllowInventoryPut(
        const world::BlockDefinition& def,
        const glm::ivec3& pos,
        const std::string& listname,
        size_t index,
        const world::ItemStack& stack,
        uint32_t playerId);
    [[nodiscard]] int invokeAllowInventoryTake(
        const world::BlockDefinition& def,
        const glm::ivec3& pos,
        const std::string& listname,
        size_t index,
        const world::ItemStack& stack,
        uint32_t playerId);
    [[nodiscard]] int invokeAllowInventoryMove(
        const world::BlockDefinition& def,
        const glm::ivec3& pos,
        const std::string& fromList,
        size_t fromIdx,
        const std::string& toList,
        size_t toIdx,
        int count,
        uint32_t playerId);
    void invokeOnInventoryPut(
        const world::BlockDefinition& def,
        const glm::ivec3& pos,
        const std::string& listname,
        size_t index,
        const world::ItemStack& stack,
        uint32_t playerId);
    void invokeOnInventoryTake(
        const world::BlockDefinition& def,
        const glm::ivec3& pos,
        const std::string& listname,
        size_t index,
        const world::ItemStack& stack,
        uint32_t playerId);
    void invokeOnInventoryMove(
        const world::BlockDefinition& def,
        const glm::ivec3& pos,
        const std::string& fromList,
        size_t fromIdx,
        const std::string& toList,
        size_t toIdx,
        int count,
        uint32_t playerId);

    // --- Shape callbacks ---
    [[nodiscard]] std::vector<math::AABB> invokeGetCollisionShape(
        const world::BlockDefinition& def, const glm::ivec3& pos);
    [[nodiscard]] std::vector<math::AABB> invokeGetSelectionShape(
        const world::BlockDefinition& def, const glm::ivec3& pos);
    [[nodiscard]] bool invokeCanAttachAt(
        const world::BlockDefinition& def, const glm::ivec3& pos, const std::string& face);

private:
    std::vector<math::AABB> parseBoxList(const sol::table& table);
    sol::state& m_lua;
    world::BlockRegistry& m_registry;
};

} // namespace voxel::scripting
