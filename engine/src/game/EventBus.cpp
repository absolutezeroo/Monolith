#include "voxel/game/EventBus.h"

namespace voxel::game
{

void EventBus::unsubscribe(EventType type, SubscriptionId id)
{
    auto it = m_subscribers.find(type);
    if (it == m_subscribers.end())
    {
        return;
    }

    std::erase_if(it->second, [id](const Subscriber& s) { return s.id == id; });
}

core::usize EventBus::subscriberCount(EventType type) const
{
    auto it = m_subscribers.find(type);
    if (it == m_subscribers.end())
    {
        return 0;
    }
    return it->second.size();
}

} // namespace voxel::game
