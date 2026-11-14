#include "shared/thread/lockfree_queue.h"
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <thread>
#include <vector>

TEST_CASE("LockFreeQueue basic push/pop", "[thread][queue]")
{
    mcg::thread::LockFreeQueue<int, 4> q;
    REQUIRE(q.empty());
    REQUIRE(q.push(1));
    REQUIRE(q.push(2));
    REQUIRE_FALSE(q.empty());

    int val = 0;
    REQUIRE(q.pop(val));
    REQUIRE(val == 1);
    REQUIRE(q.pop(val));
    REQUIRE(val == 2);
    REQUIRE_FALSE(q.pop(val));
    REQUIRE(q.empty());
}

TEST_CASE("LockFreeQueue MPMC stress test", "[thread][queue]")
{
    mcg::thread::LockFreeQueue<int, 1024> q;
    constexpr int num_threads = 4;
    constexpr int items_per_thread = 10000;

    std::vector<std::thread> producers;
    for (int i = 0; i < num_threads; ++i)
    {
        producers.emplace_back(
            [&q, i]()
            {
                for (int j = 0; j < items_per_thread; ++j)
                {
                    while (!q.push(i * items_per_thread + j))
                    {
                        std::this_thread::yield();
                    }
                }
            });
    }

    std::vector<std::thread> consumers;
    std::atomic<int> consumed{0};
    for (int i = 0; i < num_threads; ++i)
    {
        consumers.emplace_back(
            [&q, &consumed]()
            {
                int val;
                while (consumed.load() < num_threads * items_per_thread)
                {
                    if (q.pop(val))
                    {
                        consumed.fetch_add(1);
                    }
                    else
                    {
                        std::this_thread::yield();
                    }
                }
            });
    }

    for (auto& t : producers)
        t.join();
    for (auto& t : consumers)
        t.join();

    REQUIRE(consumed.load() == num_threads * items_per_thread);
    REQUIRE(q.empty());
}