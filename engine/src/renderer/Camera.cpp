#include "voxel/renderer/Camera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace voxel::renderer
{

void Camera::processMouseDelta(float dx, float dy)
{
    m_yaw += dx * m_sensitivity;
    m_pitch -= dy * m_sensitivity;
    m_pitch = std::clamp(m_pitch, -89.0f, 89.0f);

    // Wrap yaw to [0, 360) to prevent float precision loss at extreme values
    m_yaw = m_yaw - 360.0f * std::floor(m_yaw / 360.0f);
}

void Camera::setPitch(float pitch)
{
    m_pitch = std::clamp(pitch, -89.0f, 89.0f);
}

void Camera::update(float dt, bool forward, bool backward, bool left, bool right, bool up, bool down)
{
    glm::vec3 fwd = getForward();
    glm::vec3 rgt = getRight();
    constexpr glm::vec3 worldUp{0.0f, 1.0f, 0.0f};

    glm::dvec3 velocity{0.0};

    if (forward) velocity += glm::dvec3(fwd);
    if (backward) velocity -= glm::dvec3(fwd);
    if (right) velocity += glm::dvec3(rgt);
    if (left) velocity -= glm::dvec3(rgt);
    if (up) velocity += glm::dvec3(worldUp);
    if (down) velocity -= glm::dvec3(worldUp);

    if (glm::length(velocity) > 0.0)
    {
        velocity = glm::normalize(velocity);
    }

    m_position += velocity * static_cast<double>(m_moveSpeed * dt);
}

glm::vec3 Camera::getForward() const
{
    float yawRad = glm::radians(m_yaw);
    float pitchRad = glm::radians(m_pitch);

    return glm::normalize(glm::vec3{
        std::cos(pitchRad) * std::sin(yawRad),
        std::sin(pitchRad),
        std::cos(pitchRad) * std::cos(yawRad)});
}

glm::vec3 Camera::getRight() const
{
    return glm::normalize(glm::cross(glm::vec3{0.0f, 1.0f, 0.0f}, getForward()));
}

glm::vec3 Camera::getUp() const
{
    return glm::normalize(glm::cross(getRight(), getForward()));
}

glm::mat4 Camera::getViewMatrix() const
{
    glm::vec3 pos = glm::vec3(m_position);
    return glm::lookAt(pos, pos + getForward(), glm::vec3{0.0f, 1.0f, 0.0f});
}

glm::mat4 Camera::getProjectionMatrix() const
{
    return glm::perspective(glm::radians(m_fov), m_aspectRatio, m_nearPlane, m_farPlane);
}

std::array<glm::vec4, 6> Camera::extractFrustumPlanes() const
{
    glm::mat4 vp = getProjectionMatrix() * getViewMatrix();
    std::array<glm::vec4, 6> planes;

    // Gribb-Hartmann method — GLM is column-major: vp[col][row]
    // Left, Right, Bottom, Top, Near, Far
    planes[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0], vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]);
    planes[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0], vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]);
    planes[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1], vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]);
    planes[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1], vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]);
    planes[4] = glm::vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2], vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]);
    planes[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2], vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]);

    // Normalize each plane
    for (auto& p : planes)
    {
        float len = glm::length(glm::vec3(p));
        p /= len;
    }

    return planes;
}

} // namespace voxel::renderer
