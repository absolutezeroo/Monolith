#pragma once

#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

namespace voxel::core
{

/// Thread-safe MPSC queue. V1: std::mutex + std::deque.
/// Many producers can push concurrently; single consumer pops on the main thread.
template <typename T>
class ConcurrentQueue
{
  public:
    void push(T&& item)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(std::move(item));
    }

    [[nodiscard]] std::optional<T> tryPop()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty())
        {
            return std::nullopt;
        }
        T item = std::move(m_queue.front());
        m_queue.pop_front();
        return item;
    }

    [[nodiscard]] size_t size() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

    [[nodiscard]] bool empty() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

  private:
    mutable std::mutex m_mutex;
    std::deque<T> m_queue;
};

} // namespace voxel::core
