#include "voxel/core/ConfigManager.h"

#include "voxel/core/Log.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace voxel::core
{

Result<void> ConfigManager::load(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        VX_LOG_INFO("Config file not found at '{}' — using defaults", path);
        return {};
    }

    nlohmann::json j = nlohmann::json::parse(file, nullptr, false);
    if (j.is_discarded())
    {
        VX_LOG_WARN("Failed to parse config '{}' — using defaults", path);
        return std::unexpected(EngineError{ErrorCode::InvalidFormat, "Malformed JSON in config: " + path});
    }

    // Window
    if (j.contains("window"))
    {
        auto& w = j["window"];
        if (w.contains("width") && w["width"].is_number_integer())
            m_windowWidth = w["width"].get<int>();
        if (w.contains("height") && w["height"].is_number_integer())
            m_windowHeight = w["height"].get<int>();
        if (w.contains("fullscreen") && w["fullscreen"].is_boolean())
            m_fullscreen = w["fullscreen"].get<bool>();
    }

    // Camera
    if (j.contains("camera"))
    {
        auto& c = j["camera"];
        if (c.contains("fov") && c["fov"].is_number())
            m_fov = c["fov"].get<float>();
        if (c.contains("sensitivity") && c["sensitivity"].is_number())
            m_sensitivity = c["sensitivity"].get<float>();
    }

    // Rendering
    if (j.contains("rendering"))
    {
        auto& r = j["rendering"];
        if (r.contains("render_distance") && r["render_distance"].is_number_integer())
            m_renderDistance = r["render_distance"].get<int>();
    }

    // World
    if (j.contains("world"))
    {
        auto& w = j["world"];
        if (w.contains("seed") && w["seed"].is_number_integer())
            m_seed = w["seed"].get<int64_t>();
        if (w.contains("last_player_position") && w["last_player_position"].is_array() &&
            w["last_player_position"].size() == 3)
        {
            auto& pos = w["last_player_position"];
            m_lastPlayerPosition.x = pos[0].get<float>();
            m_lastPlayerPosition.y = pos[1].get<float>();
            m_lastPlayerPosition.z = pos[2].get<float>();
        }
    }

    VX_LOG_INFO("Config loaded from '{}'", path);
    return {};
}

void ConfigManager::save(const std::string& path)
{
    nlohmann::json j;

    j["window"]["width"] = m_windowWidth;
    j["window"]["height"] = m_windowHeight;
    j["window"]["fullscreen"] = m_fullscreen;

    j["camera"]["fov"] = m_fov;
    j["camera"]["sensitivity"] = m_sensitivity;

    j["rendering"]["render_distance"] = m_renderDistance;

    j["world"]["seed"] = m_seed;
    j["world"]["last_player_position"] = {m_lastPlayerPosition.x, m_lastPlayerPosition.y, m_lastPlayerPosition.z};

    std::ofstream file(path);
    if (!file.is_open())
    {
        VX_LOG_ERROR("Failed to open config file for writing: {}", path);
        return;
    }

    file << j.dump(4);
    VX_LOG_INFO("Config saved to '{}'", path);
}

} // namespace voxel::core
