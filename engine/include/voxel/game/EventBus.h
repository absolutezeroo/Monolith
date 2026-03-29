#pragma once

#include "voxel/core/Assert.h"
#include "voxel/core/Types.h"
#include "voxel/math/MathTypes.h"

#include <functional>
#include <unordered_map>
#include <vector>

namespace voxel::game
{

/// Event types published by the simulation tick after processing commands.
enum class EventType : core::uint8
{
    BlockPlaced,
    BlockBroken,
    ChunkLoaded
};

struct BlockPlacedEvent
{
    math::IVec3 position;
    core::uint16 blockId;
};

struct BlockBrokenEvent
{
    math::IVec3 position;
    core::uint16 previousBlockId;
};

struct ChunkLoadedEvent
{
    math::IVec2 coord;
};

/// Opaque handle returned by subscribe(), used for unsubscribe().
using SubscriptionId = core::uint32;

/// Typed publish/subscribe event bus for inter-system communication.
///
/// Main-thread only — no mutex. Subscribe with a typed callback,
/// publish with a typed event. Type safety is enforced at compile time
/// via templates; internally callbacks are stored type-erased.
///
/// Usage:
/// @code
///   EventBus bus;
///   auto id = bus.subscribe<BlockPlacedEvent>(EventType::BlockPlaced,
///       [](const BlockPlacedEvent& e) { /* react */ });
///   bus.publish(EventType::BlockPlaced, BlockPlacedEvent{{1,2,3}, 5});
///   bus.unsubscribe(EventType::BlockPlaced, id);
/// @endcode
class EventBus
{
  public:
    /// Subscribe a typed callback to an event type. Returns a handle for unsubscribe.
    template <typename TEvent>
    SubscriptionId subscribe(EventType type, std::function<void(const TEvent&)> callback)
    {
        SubscriptionId id = m_nextId++;
        // Wrap the typed callback into a type-erased void* callback.
        auto erased = [cb = std::move(callback)](const void* data) { cb(*static_cast<const TEvent*>(data)); };
        m_subscribers[type].push_back(Subscriber{id, std::move(erased)});
        return id;
    }

    /// Remove a subscription. No-op if the id is not found.
    void unsubscribe(EventType type, SubscriptionId id);

    /// Publish an event to all subscribers of the given type.
    template <typename TEvent>
    void publish(EventType type, const TEvent& event)
    {
        auto it = m_subscribers.find(type);
        if (it == m_subscribers.end())
        {
            return;
        }
        for (auto& sub : it->second)
        {
            sub.callback(&event);
        }
    }

    /// Returns the number of subscribers for a given event type.
    [[nodiscard]] core::usize subscriberCount(EventType type) const;

  private:
    struct Subscriber
    {
        SubscriptionId id;
        std::function<void(const void*)> callback;
    };

    std::unordered_map<EventType, std::vector<Subscriber>> m_subscribers;
    SubscriptionId m_nextId = 1;
};

} // namespace voxel::game
