#include "voxel/scripting/ComboDetector.h"

#include "voxel/core/Log.h"

#include <sol/protected_function_result.hpp>

namespace voxel::scripting
{

void ComboDetector::registerCombo(
    const std::string& name,
    std::vector<std::string> keys,
    float windowSec,
    sol::protected_function callback)
{
    if (keys.empty())
    {
        VX_LOG_WARN("voxel.register_combo(): combo '{}' has empty key sequence — ignored", name);
        return;
    }
    m_combos.push_back(ComboRegistration{name, std::move(keys), windowSec, std::move(callback)});
    VX_LOG_DEBUG("Registered combo '{}' ({} keys, {:.2f}s window)", name, m_combos.back().keys.size(), windowSec);
}

void ComboDetector::onKeyPress(const std::string& keyNameStr, float currentTime, sol::object playerHandle)
{
    // Add to ring buffer
    if (m_recentKeys.size() >= MAX_RECENT)
    {
        m_recentKeys.erase(m_recentKeys.begin());
    }
    m_recentKeys.push_back(KeyEvent{keyNameStr, currentTime});

    // Check all combos
    for (auto& combo : m_combos)
    {
        size_t comboLen = combo.keys.size();
        if (m_recentKeys.size() < comboLen)
        {
            continue;
        }

        // Check if the last N keys match the combo sequence
        size_t startIdx = m_recentKeys.size() - comboLen;
        bool matches = true;
        for (size_t i = 0; i < comboLen; ++i)
        {
            if (m_recentKeys[startIdx + i].key != combo.keys[i])
            {
                matches = false;
                break;
            }
        }

        if (!matches)
        {
            continue;
        }

        // Check time window: first key to last key must be within windowSeconds
        float elapsed = m_recentKeys.back().timestamp - m_recentKeys[startIdx].timestamp;
        if (elapsed > combo.windowSeconds)
        {
            continue;
        }

        // Combo matched — fire callback
        sol::protected_function_result result = combo.callback(playerHandle, combo.name);
        if (!result.valid())
        {
            sol::error err = result;
            VX_LOG_WARN("Lua combo '{}' callback error: {}", combo.name, err.what());
        }
    }
}

void ComboDetector::clearAll()
{
    m_combos.clear();
    m_recentKeys.clear();
}

size_t ComboDetector::comboCount() const
{
    return m_combos.size();
}

} // namespace voxel::scripting
