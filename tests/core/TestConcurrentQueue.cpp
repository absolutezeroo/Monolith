#include "voxel/core/ConcurrentQueue.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <thread>
#include <vector>

using namespace voxel::core;

TEST_CASE("ConcurrentQueue: push and tryPop return items in FIFO order", "[core][threading]")
{
    ConcurrentQueue<int> queue;

    for (int i = 0; i < 100; ++i)
    {
        queue.push(std::move(i));
    }

    for (int i = 0; i < 100; ++i)
    {
        auto val = queue.tryPop();
        REQUIRE(val.has_value());
        REQUIRE(*val == i);
    }
}

TEST_CASE("ConcurrentQueue: tryPop on empty queue returns nullopt", "[core][threading]")
{
    ConcurrentQueue<int> queue;

    auto val = queue.tryPop();
    REQUIRE_FALSE(val.has_value());
}

TEST_CASE("ConcurrentQueue: size and empty are correct", "[core][threading]")
{
    ConcurrentQueue<int> queue;

    REQUIRE(queue.empty());
    REQUIRE(queue.size() == 0);

    queue.push(1);
    REQUIRE_FALSE(queue.empty());
    REQUIRE(queue.size() == 1);

    queue.push(2);
    queue.push(3);
    REQUIRE(queue.size() == 3);

    auto v1 = queue.tryPop();
    REQUIRE(v1.has_value());
    REQUIRE(queue.size() == 2);

    auto v2 = queue.tryPop();
    auto v3 = queue.tryPop();
    REQUIRE(v2.has_value());
    REQUIRE(v3.has_value());
    REQUIRE(queue.empty());
    REQUIRE(queue.size() == 0);
}

TEST_CASE("ConcurrentQueue: multi-threaded MPSC — no loss, no duplicates", "[core][threading]")
{
    ConcurrentQueue<int> queue;
    constexpr int NUM_THREADS = 4;
    constexpr int ITEMS_PER_THREAD = 1000;
    constexpr int TOTAL = NUM_THREADS * ITEMS_PER_THREAD;

    // Launch producer threads
    std::vector<std::thread> producers;
    producers.reserve(NUM_THREADS);
    for (int t = 0; t < NUM_THREADS; ++t)
    {
        producers.emplace_back([&queue, t]() {
            for (int i = 0; i < ITEMS_PER_THREAD; ++i)
            {
                // Encode thread ID + item index: thread * 1000 + i
                int value = t * ITEMS_PER_THREAD + i;
                queue.push(std::move(value));
            }
        });
    }

    for (auto& thread : producers)
    {
        thread.join();
    }

    // Main thread pops all items
    REQUIRE(queue.size() == TOTAL);

    std::vector<int> popped;
    popped.reserve(TOTAL);
    while (true)
    {
        auto val = queue.tryPop();
        if (!val.has_value())
        {
            break;
        }
        popped.push_back(*val);
    }

    // Verify no loss
    REQUIRE(popped.size() == TOTAL);

    // Verify no duplicates — sort and check uniqueness
    std::sort(popped.begin(), popped.end());
    for (size_t i = 1; i < popped.size(); ++i)
    {
        REQUIRE(popped[i] != popped[i - 1]);
    }

    // Verify all expected values present
    for (int i = 0; i < TOTAL; ++i)
    {
        REQUIRE(popped[i] == i);
    }
}
