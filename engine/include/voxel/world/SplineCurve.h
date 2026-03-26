#pragma once

#include <vector>

namespace voxel::world
{

/// A single control point for the spline curve.
struct ControlPoint
{
    float noise;   ///< Input noise value (typically in [-1, 1])
    float height;  ///< Output height value
    float tangent; ///< Slope at this point (height units per noise unit)
};

/**
 * @brief Cubic Hermite spline that maps noise values to terrain heights.
 *
 * Uses explicit tangents at each control point (not Catmull-Rom derived).
 * Evaluates in O(n) where n is the number of control points (typically 5-10).
 */
class SplineCurve
{
  public:
    /// Construct from sorted control points. Asserts >= 2 points, sorted by noise ascending.
    explicit SplineCurve(std::vector<ControlPoint> points);

    /// Evaluate the spline at the given noise value.
    /// Values outside the control point range clamp to first/last height.
    [[nodiscard]] float evaluate(float noiseValue) const;

    /// Access the control points (read-only).
    [[nodiscard]] const std::vector<ControlPoint>& getPoints() const { return m_points; }

    /// Create the default terrain elevation profile.
    [[nodiscard]] static SplineCurve createDefault();

  private:
    std::vector<ControlPoint> m_points;
};

} // namespace voxel::world
