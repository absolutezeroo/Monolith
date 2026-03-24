#include "voxel/math/AABB.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace voxel::math;

TEST_CASE("AABB::contains", "[math][aabb]")
{
    AABB box{.min = Vec3{0.0f, 0.0f, 0.0f}, .max = Vec3{10.0f, 10.0f, 10.0f}};

    SECTION("interior point is contained")
    {
        REQUIRE(box.contains(Vec3{5.0f, 5.0f, 5.0f}));
    }

    SECTION("boundary point on min face is contained")
    {
        REQUIRE(box.contains(Vec3{0.0f, 0.0f, 0.0f}));
    }

    SECTION("boundary point on max face is contained")
    {
        REQUIRE(box.contains(Vec3{10.0f, 10.0f, 10.0f}));
    }

    SECTION("boundary point on edge is contained")
    {
        REQUIRE(box.contains(Vec3{0.0f, 5.0f, 10.0f}));
    }

    SECTION("exterior point is not contained")
    {
        REQUIRE_FALSE(box.contains(Vec3{11.0f, 5.0f, 5.0f}));
        REQUIRE_FALSE(box.contains(Vec3{-1.0f, 5.0f, 5.0f}));
        REQUIRE_FALSE(box.contains(Vec3{5.0f, -0.1f, 5.0f}));
    }
}

TEST_CASE("AABB::intersects", "[math][aabb]")
{
    AABB box{.min = Vec3{0.0f, 0.0f, 0.0f}, .max = Vec3{10.0f, 10.0f, 10.0f}};

    SECTION("overlapping AABBs intersect")
    {
        AABB other{.min = Vec3{5.0f, 5.0f, 5.0f}, .max = Vec3{15.0f, 15.0f, 15.0f}};
        REQUIRE(box.intersects(other));
        REQUIRE(other.intersects(box));
    }

    SECTION("adjacent AABBs (shared face) intersect")
    {
        AABB adjacent{.min = Vec3{10.0f, 0.0f, 0.0f}, .max = Vec3{20.0f, 10.0f, 10.0f}};
        REQUIRE(box.intersects(adjacent));
        REQUIRE(adjacent.intersects(box));
    }

    SECTION("separated AABBs do not intersect")
    {
        AABB separated{.min = Vec3{11.0f, 0.0f, 0.0f}, .max = Vec3{20.0f, 10.0f, 10.0f}};
        REQUIRE_FALSE(box.intersects(separated));
        REQUIRE_FALSE(separated.intersects(box));
    }

    SECTION("contained AABB intersects")
    {
        AABB inner{.min = Vec3{2.0f, 2.0f, 2.0f}, .max = Vec3{8.0f, 8.0f, 8.0f}};
        REQUIRE(box.intersects(inner));
        REQUIRE(inner.intersects(box));
    }

    SECTION("identical AABBs intersect")
    {
        REQUIRE(box.intersects(box));
    }
}

TEST_CASE("AABB::expand", "[math][aabb]")
{
    AABB box{.min = Vec3{0.0f, 0.0f, 0.0f}, .max = Vec3{10.0f, 10.0f, 10.0f}};

    SECTION("expand with point outside max grows AABB")
    {
        box.expand(Vec3{15.0f, 20.0f, 25.0f});
        REQUIRE(box.max.x == 15.0f);
        REQUIRE(box.max.y == 20.0f);
        REQUIRE(box.max.z == 25.0f);
        REQUIRE(box.min.x == 0.0f);
    }

    SECTION("expand with point outside min grows AABB")
    {
        box.expand(Vec3{-5.0f, -3.0f, -1.0f});
        REQUIRE(box.min.x == -5.0f);
        REQUIRE(box.min.y == -3.0f);
        REQUIRE(box.min.z == -1.0f);
        REQUIRE(box.max.x == 10.0f);
    }

    SECTION("expand with interior point does not change AABB")
    {
        box.expand(Vec3{5.0f, 5.0f, 5.0f});
        REQUIRE(box.min.x == 0.0f);
        REQUIRE(box.max.x == 10.0f);
    }

    SECTION("expand returns reference for chaining")
    {
        AABB& ref = box.expand(Vec3{20.0f, 20.0f, 20.0f});
        REQUIRE(&ref == &box);
    }
}

TEST_CASE("AABB::center", "[math][aabb]")
{
    AABB box{.min = Vec3{0.0f, 0.0f, 0.0f}, .max = Vec3{10.0f, 10.0f, 10.0f}};
    Vec3 c = box.center();
    REQUIRE(c.x == 5.0f);
    REQUIRE(c.y == 5.0f);
    REQUIRE(c.z == 5.0f);

    SECTION("asymmetric AABB")
    {
        AABB asym{.min = Vec3{-2.0f, 0.0f, 4.0f}, .max = Vec3{8.0f, 6.0f, 10.0f}};
        Vec3 ac = asym.center();
        REQUIRE(ac.x == 3.0f);
        REQUIRE(ac.y == 3.0f);
        REQUIRE(ac.z == 7.0f);
    }
}

TEST_CASE("AABB::extents", "[math][aabb]")
{
    AABB box{.min = Vec3{0.0f, 0.0f, 0.0f}, .max = Vec3{10.0f, 10.0f, 10.0f}};
    Vec3 e = box.extents();
    REQUIRE(e.x == 5.0f);
    REQUIRE(e.y == 5.0f);
    REQUIRE(e.z == 5.0f);

    SECTION("asymmetric AABB extents are half-widths")
    {
        AABB asym{.min = Vec3{-4.0f, 0.0f, 2.0f}, .max = Vec3{6.0f, 10.0f, 8.0f}};
        Vec3 ae = asym.extents();
        REQUIRE(ae.x == 5.0f);
        REQUIRE(ae.y == 5.0f);
        REQUIRE(ae.z == 3.0f);
    }
}
