#pragma once

#include <glm/glm.hpp>

#include <array>

namespace voxel::renderer
{

class Camera
{
  public:
    Camera() = default;

    /// Apply raw mouse delta to yaw/pitch. Pitch clamped to +/-89 degrees.
    void processMouseDelta(float dx, float dy);

    /// Update fly-mode position based on directional input and delta time.
    void update(float dt, bool forward, bool backward, bool left, bool right, bool up, bool down);

    void setAspectRatio(float aspect) { m_aspectRatio = aspect; }
    void setFov(float fov) { m_fov = fov; }
    void setSensitivity(float sensitivity) { m_sensitivity = sensitivity; }
    void setMoveSpeed(float speed) { m_moveSpeed = speed; }
    void setPosition(const glm::dvec3& position) { m_position = position; }
    void setYaw(float yaw) { m_yaw = yaw; }
    void setPitch(float pitch);

    [[nodiscard]] const glm::dvec3& getPosition() const { return m_position; }
    [[nodiscard]] float getYaw() const { return m_yaw; }
    [[nodiscard]] float getPitch() const { return m_pitch; }
    [[nodiscard]] float getFov() const { return m_fov; }
    [[nodiscard]] float getSensitivity() const { return m_sensitivity; }
    [[nodiscard]] float getMoveSpeed() const { return m_moveSpeed; }
    [[nodiscard]] float getAspectRatio() const { return m_aspectRatio; }
    [[nodiscard]] float getNearPlane() const { return m_nearPlane; }
    [[nodiscard]] float getFarPlane() const { return m_farPlane; }

    [[nodiscard]] glm::mat4 getViewMatrix() const;
    [[nodiscard]] glm::mat4 getProjectionMatrix() const;
    [[nodiscard]] std::array<glm::vec4, 6> extractFrustumPlanes() const;

    [[nodiscard]] glm::vec3 getForward() const;
    [[nodiscard]] glm::vec3 getRight() const;
    [[nodiscard]] glm::vec3 getUp() const;

  private:
    glm::dvec3 m_position{0.0, 80.0, 0.0};
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;

    float m_fov = 70.0f;
    float m_nearPlane = 0.1f;
    float m_farPlane = 1000.0f;
    float m_aspectRatio = 16.0f / 9.0f;

    float m_moveSpeed = 10.0f;
    float m_sensitivity = 0.1f;
};

} // namespace voxel::renderer
