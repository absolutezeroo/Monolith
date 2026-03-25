#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "voxel/renderer/Camera.h"

#include <cmath>

using namespace voxel::renderer;
using Catch::Matchers::WithinAbs;

TEST_CASE("Camera", "[renderer][camera]")
{
    Camera camera;

    SECTION("default state produces valid matrices")
    {
        auto view = camera.getViewMatrix();
        auto proj = camera.getProjectionMatrix();

        // Matrices should not contain NaN
        for (int c = 0; c < 4; ++c)
        {
            for (int r = 0; r < 4; ++r)
            {
                REQUIRE_FALSE(std::isnan(view[c][r]));
                REQUIRE_FALSE(std::isnan(proj[c][r]));
            }
        }

        // Default position is (0, 80, 0)
        REQUIRE_THAT(camera.getPosition().x, WithinAbs(0.0, 0.001));
        REQUIRE_THAT(camera.getPosition().y, WithinAbs(80.0, 0.001));
        REQUIRE_THAT(camera.getPosition().z, WithinAbs(0.0, 0.001));
    }

    SECTION("pitch clamps at +/-89 degrees")
    {
        // Push pitch way positive
        camera.processMouseDelta(0.0f, -10000.0f);
        REQUIRE(camera.getPitch() <= 89.0f);
        REQUIRE(camera.getPitch() >= -89.0f);

        // Push pitch way negative
        Camera cam2;
        cam2.processMouseDelta(0.0f, 10000.0f);
        REQUIRE(cam2.getPitch() >= -89.0f);
        REQUIRE(cam2.getPitch() <= 89.0f);
    }

    SECTION("yaw wraps correctly — no NaN/Inf")
    {
        // Apply extreme yaw values
        camera.processMouseDelta(100000.0f, 0.0f);
        REQUIRE_FALSE(std::isnan(camera.getYaw()));
        REQUIRE_FALSE(std::isinf(camera.getYaw()));

        auto fwd = camera.getForward();
        REQUIRE_FALSE(std::isnan(fwd.x));
        REQUIRE_FALSE(std::isnan(fwd.y));
        REQUIRE_FALSE(std::isnan(fwd.z));
    }

    SECTION("frustum plane extraction produces 6 normalized planes")
    {
        auto planes = camera.extractFrustumPlanes();
        REQUIRE(planes.size() == 6);

        for (const auto& p : planes)
        {
            float normalLen = glm::length(glm::vec3(p));
            REQUIRE_THAT(static_cast<double>(normalLen), WithinAbs(1.0, 0.001));
        }
    }

    SECTION("forward/right/up vectors are orthonormal")
    {
        camera.processMouseDelta(30.0f, -15.0f);

        auto fwd = camera.getForward();
        auto rgt = camera.getRight();
        auto up = camera.getUp();

        // Unit length
        REQUIRE_THAT(static_cast<double>(glm::length(fwd)), WithinAbs(1.0, 0.001));
        REQUIRE_THAT(static_cast<double>(glm::length(rgt)), WithinAbs(1.0, 0.001));
        REQUIRE_THAT(static_cast<double>(glm::length(up)), WithinAbs(1.0, 0.001));

        // Orthogonal (dot products ≈ 0)
        REQUIRE_THAT(static_cast<double>(glm::dot(fwd, rgt)), WithinAbs(0.0, 0.001));
        REQUIRE_THAT(static_cast<double>(glm::dot(fwd, up)), WithinAbs(0.0, 0.001));
        REQUIRE_THAT(static_cast<double>(glm::dot(rgt, up)), WithinAbs(0.0, 0.001));
    }

    SECTION("WASD movement updates position")
    {
        auto startPos = camera.getPosition();

        // Move forward for 1 second at default speed
        camera.update(1.0f, true, false, false, false, false, false);

        auto endPos = camera.getPosition();
        double dist = glm::length(endPos - startPos);

        // Should have moved approximately moveSpeed (10.0) units
        REQUIRE_THAT(dist, WithinAbs(10.0, 0.5));
    }

    SECTION("aspect ratio change updates projection matrix")
    {
        auto proj1 = camera.getProjectionMatrix();
        camera.setAspectRatio(2.0f);
        auto proj2 = camera.getProjectionMatrix();

        // Matrices should differ
        bool differs = false;
        for (int c = 0; c < 4 && !differs; ++c)
        {
            for (int r = 0; r < 4 && !differs; ++r)
            {
                if (std::abs(proj1[c][r] - proj2[c][r]) > 0.0001f)
                {
                    differs = true;
                }
            }
        }
        REQUIRE(differs);
    }

    SECTION("setPitch clamps value")
    {
        camera.setPitch(100.0f);
        REQUIRE(camera.getPitch() <= 89.0f);

        camera.setPitch(-100.0f);
        REQUIRE(camera.getPitch() >= -89.0f);
    }
}
