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
    BlockChanged,
    ChunkLoaded,
    ChunkUnloaded,
    ChunkGenerated
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

struct BlockChangedEvent
{
    math::IVec3 position;
    core::uint16 previousBlockId;
    core::uint16 newBlockId;
};

struct ChunkLoadedEvent
{
    math::IVec2 coord;
    bool fromDisk; ///< true if loaded from disk, false if freshly generated.
};

struct ChunkUnloadedEvent
{
    math::IVec2 coord;
};

struct ChunkGeneratedEvent
{
    math::IVec2 coord;
};

/// Compile-time mapping from EventType enum values to their event struct types.
/// Prevents mismatching EventType and TEvent at the call site.
template <EventType>
struct EventTypeTraits;

template <>
struct EventTypeTraits<EventType::BlockPlaced>
{
    using Type = BlockPlacedEvent;
};

template <>
struct EventTypeTraits<EventType::BlockBroken>
{
    using Type = BlockBrokenEvent;
};

template <>
struct EventTypeTraits<EventType::BlockChanged>
{
    using Type = BlockChangedEvent;
};

template <>
struct EventTypeTraits<EventType::ChunkLoaded>
{
    using Type = ChunkLoadedEvent;
};

template <>
struct EventTypeTraits<EventType::ChunkUnloaded>
{
    using Type = ChunkUnloadedEvent;
};

template <>
struct EventTypeTraits<EventType::ChunkGenerated>
{
    using Type = ChunkGeneratedEvent;
};

/// Opaque handle returned by subscribe(), used for unsubscribe().
using SubscriptionId = core::uint32;

/// Typed publish/subscribe event bus for inter-system communication.
///
/// Main-thread only — no mutex. Subscribe with a typed callback,
/// publish with a typed event. Type safety is enforced at compile time
/// via EventTypeTraits — the EventType template parameter determines
/// the event struct type, making mismatches impossible.
///
/// Usage:
/// @code
///   EventBus bus;
///   auto id = bus.subscribe<EventType::BlockPlaced>(
///       [](const BlockPlacedEvent& e) { /* react */ });
///   bus.publish<EventType::BlockPlaced>(BlockPlacedEvent{{1,2,3}, 5});
///   bus.unsubscribe(EventType::BlockPlaced, id);
/// @endcode
class EventBus
{
  public:
    /// Subscribe a typed callback to an event type. Returns a handle for unsubscribe.
    template <EventType E>
    SubscriptionId subscribe(std::function<void(const typename EventTypeTraits<E>::Type&)> callback)
    {
        VX_ASSERT(m_nextId != 0, "SubscriptionId overflow");
        SubscriptionId id = m_nextId++;
        auto erased = [cb = std::move(callback)](const void* data) {
            cb(*static_cast<const typename EventTypeTraits<E>::Type*>(data));
        };
        m_subscribers[E].push_back(Subscriber{id, std::move(erased)});
        return id;
    }

    /// Remove a subscription. No-op if the id is not found.
    void unsubscribe(EventType type, SubscriptionId id);

    /// Publish an event to all subscribers of the given type.
    template <EventType E>
    void publish(const typename EventTypeTraits<E>::Type& event)
    {
        auto it = m_subscribers.find(E);
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
