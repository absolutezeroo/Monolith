#pragma once

#include "voxel/core/ConcurrentQueue.h"
#include "voxel/core/Types.h"
#include "voxel/game/GameCommand.h"

#include <optional>

namespace voxel::game
{

/// Thread-safe command queue for game actions.
/// Wraps ConcurrentQueue<GameCommand> — input handlers push commands,
/// the simulation tick drains and processes them.
///
/// Usage (future Story 7.2+):
/// @code
///   // Input thread / handler:
///   m_commandQueue.push(GameCommand{CommandType::Jump, playerId, tick, JumpPayload{}});
///
///   // Simulation tick (main thread):
///   m_commandQueue.drain([this](GameCommand cmd) {
///       processCommand(std::move(cmd));
///   });
/// @endcode
class CommandQueue
{
  public:
    void push(GameCommand cmd) { m_queue.push(std::move(cmd)); }

    [[nodiscard]] std::optional<GameCommand> tryPop() { return m_queue.tryPop(); }

    [[nodiscard]] bool empty() const { return m_queue.empty(); }

    [[nodiscard]] core::usize size() const { return m_queue.size(); }

    /// Drain all queued commands, invoking handler for each in FIFO order.
    template <typename Func>
    void drain(Func&& handler)
    {
        while (auto cmd = tryPop())
        {
            handler(std::move(*cmd));
        }
    }

  private:
    core::ConcurrentQueue<GameCommand> m_queue;
};

} // namespace voxel::game
