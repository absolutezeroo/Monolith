#include "voxel/world/SplineCurve.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace voxel::world;
using Catch::Matchers::WithinAbs;

// ── Evaluation at control points ─────────────────────────────────────────────

TEST_CASE("SplineCurve: evaluate at exact control point returns that point's height", "[world][spline]")
{
    SplineCurve spline({
        {-1.0f, 10.0f, 0.0f},
        { 0.0f, 50.0f, 0.0f},
        { 1.0f, 90.0f, 0.0f},
    });

    REQUIRE_THAT(spline.evaluate(-1.0f), WithinAbs(10.0, 0.01));
    REQUIRE_THAT(spline.evaluate(0.0f), WithinAbs(50.0, 0.01));
    REQUIRE_THAT(spline.evaluate(1.0f), WithinAbs(90.0, 0.01));
}

// ── Smooth interpolation ─────────────────────────────────────────────────────

TEST_CASE("SplineCurve: evaluate between two linear points interpolates smoothly", "[world][spline]")
{
    // Two points with tangents that produce a linear interpolation
    // For Hermite with tangent = slope = (90-10)/(1-(-1)) = 40 at both points → linear
    SplineCurve spline({
        {-1.0f, 10.0f, 40.0f},
        { 1.0f, 90.0f, 40.0f},
    });

    // Midpoint of a linear Hermite should be exactly 50
    REQUIRE_THAT(spline.evaluate(0.0f), WithinAbs(50.0, 0.5));

    // Quarter point should be roughly 30
    REQUIRE_THAT(spline.evaluate(-0.5f), WithinAbs(30.0, 0.5));
}

// ── Clamping at boundaries ───────────────────────────────────────────────────

TEST_CASE("SplineCurve: evaluate below min clamps to first height", "[world][spline]")
{
    SplineCurve spline({
        {-0.5f, 20.0f, 0.0f},
        { 0.5f, 80.0f, 0.0f},
    });

    REQUIRE_THAT(spline.evaluate(-1.0f), WithinAbs(20.0, 0.01));
    REQUIRE_THAT(spline.evaluate(-999.0f), WithinAbs(20.0, 0.01));
}

TEST_CASE("SplineCurve: evaluate above max clamps to last height", "[world][spline]")
{
    SplineCurve spline({
        {-0.5f, 20.0f, 0.0f},
        { 0.5f, 80.0f, 0.0f},
    });

    REQUIRE_THAT(spline.evaluate(1.0f), WithinAbs(80.0, 0.01));
    REQUIRE_THAT(spline.evaluate(999.0f), WithinAbs(80.0, 0.01));
}

// ── Monotonicity ─────────────────────────────────────────────────────────────

TEST_CASE("SplineCurve: monotonically increasing control points produce monotonically increasing output",
          "[world][spline]")
{
    // Use moderate tangents that preserve monotonicity
    SplineCurve spline({
        {-1.0f, 10.0f,  0.0f},
        {-0.5f, 30.0f, 30.0f},
        { 0.0f, 50.0f, 30.0f},
        { 0.5f, 70.0f, 30.0f},
        { 1.0f, 90.0f,  0.0f},
    });

    float prev = spline.evaluate(-1.0f);
    for (int i = 1; i <= 200; ++i)
    {
        float noise = -1.0f + static_cast<float>(i) * (2.0f / 200.0f);
        float current = spline.evaluate(noise);
        REQUIRE(current >= prev - 0.001f); // small epsilon for floating point
        prev = current;
    }
}

// ── Default spline ───────────────────────────────────────────────────────────

TEST_CASE("SplineCurve: default spline maps expected noise values to expected heights", "[world][spline]")
{
    SplineCurve spline = SplineCurve::createDefault();

    // At noise -1.0 → ~62 (plains floor)
    REQUIRE_THAT(spline.evaluate(-1.0f), WithinAbs(62.0, 1.0));

    // At noise 0.0 → ~68 (hills transition)
    REQUIRE_THAT(spline.evaluate(0.0f), WithinAbs(68.0, 1.0));

    // At noise 1.0 → ~150 (plateau)
    REQUIRE_THAT(spline.evaluate(1.0f), WithinAbs(150.0, 1.0));
}

TEST_CASE("SplineCurve: default spline has increasing output across range", "[world][spline]")
{
    SplineCurve spline = SplineCurve::createDefault();

    // Overall trend should be increasing from -1 to 1
    float atNegOne = spline.evaluate(-1.0f);
    float atZero = spline.evaluate(0.0f);
    float atOne = spline.evaluate(1.0f);

    REQUIRE(atNegOne < atZero);
    REQUIRE(atZero < atOne);
}

// ── Edge cases ───────────────────────────────────────────────────────────────

TEST_CASE("SplineCurve: exactly at boundary between segments", "[world][spline]")
{
    SplineCurve spline({
        {-1.0f, 10.0f, 0.0f},
        { 0.0f, 50.0f, 0.0f},
        { 1.0f, 90.0f, 0.0f},
    });

    // Evaluate at the exact internal control point
    REQUIRE_THAT(spline.evaluate(0.0f), WithinAbs(50.0, 0.01));
}

TEST_CASE("SplineCurve: two-point spline works correctly", "[world][spline]")
{
    SplineCurve spline({
        {0.0f, 0.0f, 100.0f},
        {1.0f, 100.0f, 100.0f},
    });

    // With matching tangents (slope=100), should be roughly linear
    REQUIRE_THAT(spline.evaluate(0.0f), WithinAbs(0.0, 0.01));
    REQUIRE_THAT(spline.evaluate(1.0f), WithinAbs(100.0, 0.01));
    REQUIRE_THAT(spline.evaluate(0.5f), WithinAbs(50.0, 1.0));
}
