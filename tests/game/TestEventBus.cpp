#include "voxel/game/EventBus.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace voxel::game;

TEST_CASE("EventBus: subscribe and publish invokes callback", "[game][event]")
{
    EventBus bus;
    bool called = false;

    bus.subscribe<EventType::BlockPlaced>([&called](const BlockPlacedEvent&) { called = true; });

    bus.publish<EventType::BlockPlaced>(BlockPlacedEvent{{1, 2, 3}, 5});
    REQUIRE(called);
}

TEST_CASE("EventBus: multiple subscribers all invoked", "[game][event]")
{
    EventBus bus;
    int callCount = 0;

    bus.subscribe<EventType::BlockBroken>([&callCount](const BlockBrokenEvent&) { callCount++; });
    bus.subscribe<EventType::BlockBroken>([&callCount](const BlockBrokenEvent&) { callCount++; });
    bus.subscribe<EventType::BlockBroken>([&callCount](const BlockBrokenEvent&) { callCount++; });

    bus.publish<EventType::BlockBroken>(BlockBrokenEvent{{0, 0, 0}, 10});
    REQUIRE(callCount == 3);
}

TEST_CASE("EventBus: callback not invoked for different event type", "[game][event]")
{
    EventBus bus;
    bool called = false;

    bus.subscribe<EventType::BlockPlaced>([&called](const BlockPlacedEvent&) { called = true; });

    // Publish a different event type
    bus.publish<EventType::BlockBroken>(BlockBrokenEvent{{0, 0, 0}, 1});
    REQUIRE_FALSE(called);
}

TEST_CASE("EventBus: unsubscribe prevents future callbacks", "[game][event]")
{
    EventBus bus;
    int callCount = 0;

    auto id = bus.subscribe<EventType::ChunkLoaded>([&callCount](const ChunkLoadedEvent&) { callCount++; });

    bus.publish<EventType::ChunkLoaded>(ChunkLoadedEvent{{0, 0}});
    REQUIRE(callCount == 1);

    bus.unsubscribe(EventType::ChunkLoaded, id);

    bus.publish<EventType::ChunkLoaded>(ChunkLoadedEvent{{1, 1}});
    REQUIRE(callCount == 1); // Not incremented
}

TEST_CASE("EventBus: event data passed correctly to subscriber", "[game][event]")
{
    EventBus bus;
    voxel::math::IVec3 receivedPos{};
    voxel::core::uint16 receivedBlockId = 0;

    bus.subscribe<EventType::BlockPlaced>([&](const BlockPlacedEvent& e) {
        receivedPos = e.position;
        receivedBlockId = e.blockId;
    });

    bus.publish<EventType::BlockPlaced>(BlockPlacedEvent{{10, 20, 30}, 42});
    REQUIRE(receivedPos.x == 10);
    REQUIRE(receivedPos.y == 20);
    REQUIRE(receivedPos.z == 30);
    REQUIRE(receivedBlockId == 42);
}

TEST_CASE("EventBus: subscriberCount reflects state", "[game][event]")
{
    EventBus bus;

    REQUIRE(bus.subscriberCount(EventType::BlockPlaced) == 0);

    auto id1 = bus.subscribe<EventType::BlockPlaced>([](const BlockPlacedEvent&) {});
    REQUIRE(bus.subscriberCount(EventType::BlockPlaced) == 1);

    auto id2 = bus.subscribe<EventType::BlockPlaced>([](const BlockPlacedEvent&) {});
    REQUIRE(bus.subscriberCount(EventType::BlockPlaced) == 2);

    bus.unsubscribe(EventType::BlockPlaced, id1);
    REQUIRE(bus.subscriberCount(EventType::BlockPlaced) == 1);

    bus.unsubscribe(EventType::BlockPlaced, id2);
    REQUIRE(bus.subscriberCount(EventType::BlockPlaced) == 0);
}

TEST_CASE("EventBus: unsubscribe with invalid id is a no-op", "[game][event]")
{
    EventBus bus;
    int callCount = 0;

    bus.subscribe<EventType::BlockPlaced>([&callCount](const BlockPlacedEvent&) { callCount++; });

    // Unsubscribe with a bogus id
    bus.unsubscribe(EventType::BlockPlaced, 9999);

    bus.publish<EventType::BlockPlaced>(BlockPlacedEvent{{0, 0, 0}, 1});
    REQUIRE(callCount == 1); // Subscriber still active
}

TEST_CASE("EventBus: publish with no subscribers is a no-op", "[game][event]")
{
    EventBus bus;
    // Should not crash or throw
    bus.publish<EventType::ChunkLoaded>(ChunkLoadedEvent{{5, 5}});
    REQUIRE(bus.subscriberCount(EventType::ChunkLoaded) == 0);
}
