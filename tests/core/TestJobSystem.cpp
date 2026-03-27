#include "voxel/core/JobSystem.h"

#include <catch2/catch_test_macros.hpp>

#include <enkiTS/TaskScheduler.h>

#include <atomic>
#include <memory>

using namespace voxel::core;

TEST_CASE("JobSystem: lifecycle and task submission", "[core][threading]")
{
    JobSystem js;
    REQUIRE_FALSE(js.isInitialized());
    REQUIRE(js.threadCount() == 0);

    SECTION("init succeeds and threadCount > 0")
    {
        auto result = js.init();
        REQUIRE(result.has_value());
        REQUIRE(js.isInitialized());
        REQUIRE(js.threadCount() > 0);
    }

    SECTION("double init returns error")
    {
        auto result = js.init();
        REQUIRE(result.has_value());

        auto result2 = js.init();
        REQUIRE_FALSE(result2.has_value());
    }

    SECTION("submit a simple task and verify it ran")
    {
        auto result = js.init();
        REQUIRE(result.has_value());

        std::atomic<int> counter{0};

        struct SimpleTask : enki::ITaskSet
        {
            std::atomic<int>& ref;
            explicit SimpleTask(std::atomic<int>& r) : ref(r) { m_SetSize = 1; }
            void ExecuteRange(enki::TaskSetPartition, uint32_t) override { ref.fetch_add(1); }
        };

        SimpleTask task(counter);
        js.getScheduler().AddTaskSetToPipe(&task);
        js.getScheduler().WaitforTask(&task);

        REQUIRE(counter.load() == 1);
    }

    SECTION("submit 100 tasks and verify all completed")
    {
        auto result = js.init();
        REQUIRE(result.has_value());

        std::atomic<int> counter{0};
        constexpr int NUM_TASKS = 100;

        struct CounterTask : enki::ITaskSet
        {
            std::atomic<int>& ref;
            explicit CounterTask(std::atomic<int>& r) : ref(r) { m_SetSize = 1; }
            void ExecuteRange(enki::TaskSetPartition, uint32_t) override { ref.fetch_add(1); }
        };

        for (int i = 0; i < NUM_TASKS; ++i)
        {
            auto task = std::make_unique<CounterTask>(counter);
            js.getScheduler().AddTaskSetToPipe(task.get());
            js.getScheduler().WaitforTask(task.get());
        }

        REQUIRE(counter.load() == NUM_TASKS);
    }

    // shutdown() is called by ~JobSystem() for all sections
}
