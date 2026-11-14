#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace mcg::thread
{

// Очередь Вьюкова (bounded MPMC queue).
// Capacity должно быть степенью двойки.
template <typename T, size_t Capacity> class LockFreeQueue
{
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static_assert(Capacity > 1, "Capacity must be greater than 1");

    struct Cell
    {
        std::atomic<size_t> sequence;
        std::optional<T> data;
    };

    std::array<Cell, Capacity> buffer_;

    // Выравнивание по кэш-линии (64 байта), чтобы избежать false sharing
    alignas(64) std::atomic<size_t> enqueue_pos_{0};
    alignas(64) std::atomic<size_t> dequeue_pos_{0};

public:
    LockFreeQueue()
    {
        for (size_t i = 0; i < Capacity; ++i)
        {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    // Добавляет элемент. Возвращает false, если очередь переполнена.
    bool push(T const& data)
    {
        Cell* cell;
        size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
        for (;;)
        {
            cell = &buffer_[pos & (Capacity - 1)];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t dif = (intptr_t)seq - (intptr_t)pos;

            if (dif == 0)
            {
                if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                {
                    break;
                }
            }
            else if (dif < 0)
            {
                return false; // Очередь полна
            }
            else
            {
                pos = enqueue_pos_.load(std::memory_order_relaxed);
            }
        }
        cell->data.emplace(data);
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    // Извлекает элемент. Возвращает false, если очередь пуста.
    bool pop(T& data)
    {
        Cell* cell;
        size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
        for (;;)
        {
            cell = &buffer_[pos & (Capacity - 1)];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t dif = (intptr_t)seq - (intptr_t)(pos + 1);

            if (dif == 0)
            {
                if (dequeue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                {
                    break;
                }
            }
            else if (dif < 0)
            {
                return false; // Очередь пуста
            }
            else
            {
                pos = dequeue_pos_.load(std::memory_order_relaxed);
            }
        }
        data = std::move(cell->data.value());
        cell->data.reset();
        cell->sequence.store(pos + Capacity, std::memory_order_release);
        return true;
    }

    bool empty() const
    {
        return enqueue_pos_.load(std::memory_order_acquire) ==
               dequeue_pos_.load(std::memory_order_acquire);
    }
};

} // namespace mcg::thread