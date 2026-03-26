#include "voxel/world/SplineCurve.h"

#include "voxel/core/Assert.h"

#include <algorithm>

namespace voxel::world
{

SplineCurve::SplineCurve(std::vector<ControlPoint> points)
    : m_points(std::move(points))
{
    VX_ASSERT(m_points.size() >= 2, "SplineCurve requires at least 2 control points");

    // Verify sorted order
    for (size_t i = 1; i < m_points.size(); ++i)
    {
        VX_ASSERT(
            m_points[i].noise > m_points[i - 1].noise,
            "SplineCurve control points must be sorted by noise ascending with no duplicates"
        );
    }
}

float SplineCurve::evaluate(float noiseValue) const
{
    // Clamp below first point
    if (noiseValue <= m_points.front().noise)
    {
        return m_points.front().height;
    }

    // Clamp above last point
    if (noiseValue >= m_points.back().noise)
    {
        return m_points.back().height;
    }

    // Find the segment: last point where noise < noiseValue
    // Linear scan is fine for 5-10 control points
    size_t i = 0;
    for (size_t j = 1; j < m_points.size(); ++j)
    {
        if (m_points[j].noise >= noiseValue)
        {
            i = j - 1;
            break;
        }
    }

    const ControlPoint& p0 = m_points[i];
    const ControlPoint& p1 = m_points[i + 1];

    float dx = p1.noise - p0.noise; // interval width
    float t = (noiseValue - p0.noise) / dx;

    // Hermite basis functions
    float t2 = t * t;
    float t3 = t2 * t;

    float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f; // value at start
    float h10 = t3 - 2.0f * t2 + t;             // tangent at start
    float h01 = -2.0f * t3 + 3.0f * t2;         // value at end
    float h11 = t3 - t2;                         // tangent at end

    return h00 * p0.height + h10 * p0.tangent * dx + h01 * p1.height + h11 * p1.tangent * dx;
}

SplineCurve SplineCurve::createDefault()
{
    // Default terrain profile: plains → hills → mountains → plateau
    return SplineCurve({
        {-1.0f,  62.0f,  0.0f}, // deep ocean floor / lowest plains
        {-0.4f,  64.0f,  5.0f}, // sea level plains (flat region)
        { 0.0f,  68.0f, 15.0f}, // gentle rise starts (plains → hills transition)
        { 0.3f,  90.0f, 40.0f}, // hills
        { 0.6f, 120.0f, 60.0f}, // steep mountain rise
        { 0.8f, 140.0f, 20.0f}, // mountain peaks
        { 1.0f, 150.0f,  0.0f}, // plateau at extreme (flat mountaintops)
    });
}

} // namespace voxel::world
