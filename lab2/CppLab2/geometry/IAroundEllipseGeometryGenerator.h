#pragma once

#include "../domain/GeometryTypes.h"
#include "AroundRequest.h"
#include <vector>

// Defines positioning algorithms that construct geometry around an ellipse.
struct IAroundEllipseGeometryGenerator {
	virtual ~IAroundEllipseGeometryGenerator() = default;

	virtual std::vector<Point2D> CalculateAround(
		const EllipseGeometry& ellipse,
		const AroundRequest& request) const = 0;
};

