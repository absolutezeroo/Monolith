#pragma once

#include "voxel/core/Result.h"

#include <glm/vec3.hpp>

#include <cstdint>
#include <string>

namespace voxel::core
{

/**
 * @brief Loads and saves game configuration from/to a JSON file.
 *
 * Stores typed settings for window, camera, rendering, and world.
 * Uses nlohmann/json for serialization. Missing keys use defaults.
 */
class ConfigManager
{
  public:
    /**
     * @brief Loads configuration from a JSON file.
     * @param path Path to the config file. If missing, defaults are kept.
     * @return Success, or EngineError on parse failure.
     */
    Result<void> load(const std::string& path);

    /**
     * @brief Saves current configuration to a JSON file.
     * @param path Path to write the config file.
     */
    void save(const std::string& path);

    // --- Window ---
    [[nodiscard]] int getWindowWidth() const { return m_windowWidth; }
    [[nodiscard]] int getWindowHeight() const { return m_windowHeight; }
    [[nodiscard]] bool isFullscreen() const { return m_fullscreen; }
    void setWindowWidth(int w) { m_windowWidth = w; }
    void setWindowHeight(int h) { m_windowHeight = h; }
    void setFullscreen(bool fs) { m_fullscreen = fs; }

    // --- Camera ---
    [[nodiscard]] float getFov() const { return m_fov; }
    [[nodiscard]] float getSensitivity() const { return m_sensitivity; }
    void setFov(float fov) { m_fov = fov; }
    void setSensitivity(float sensitivity) { m_sensitivity = sensitivity; }

    // --- Rendering ---
    [[nodiscard]] int getRenderDistance() const { return m_renderDistance; }
    void setRenderDistance(int rd) { m_renderDistance = rd; }

    // --- World ---
    [[nodiscard]] int64_t getSeed() const { return m_seed; }
    [[nodiscard]] const glm::vec3& getLastPlayerPosition() const { return m_lastPlayerPosition; }
    void setSeed(int64_t seed) { m_seed = seed; }
    void setLastPlayerPosition(const glm::vec3& pos) { m_lastPlayerPosition = pos; }

  private:
    int m_windowWidth = 1280;
    int m_windowHeight = 720;
    bool m_fullscreen = false;
    float m_fov = 70.0f;
    float m_sensitivity = 0.1f;
    int m_renderDistance = 16;
    int64_t m_seed = 8675309;
    glm::vec3 m_lastPlayerPosition{127.5f, 72.0f, -56.0f};
};

} // namespace voxel::core
