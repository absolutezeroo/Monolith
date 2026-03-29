#include "voxel/game/EventBus.h"

#include <algorithm>

namespace voxel::game
{

void EventBus::unsubscribe(EventType type, SubscriptionId id)
{
    auto it = m_subscribers.find(type);
    if (it == m_subscribers.end())
    {
        return;
    }

    auto& subs = it->second;
    auto removed = std::remove_if(subs.begin(), subs.end(), [id](const Subscriber& s) { return s.id == id; });
    subs.erase(removed, subs.end());
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
