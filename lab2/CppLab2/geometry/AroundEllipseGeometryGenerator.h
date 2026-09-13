#pragma once

#include "IAroundEllipseGeometryGenerator.h"

// Builds the requested geometry around an ellipse using tangent-line construction.
class AroundEllipseGeometryGenerator final : public IAroundEllipseGeometryGenerator {
public:
	std::vector<Point2D> CalculateAround(
		const EllipseGeometry& ellipse,
		const AroundRequest& request) const override;
};

