#include "AroundEllipseGeometryGenerator.h"
#include <cmath>
#include <stdexcept>
#include <utility>

std::vector<Point2D> AroundEllipseGeometryGenerator::CalculateAround(
	const EllipseGeometry& ellipse,
	const AroundRequest& request) const {
	if (request.kind != Kind::Pentagon || request.with != With::TangentMidpoints) {
		throw std::invalid_argument("Unsupported around geometry request");
	}
	if (!std::isfinite(ellipse.cx) || !std::isfinite(ellipse.cy) ||
		!std::isfinite(ellipse.rx) || !std::isfinite(ellipse.ry) ||
		ellipse.rx <= 0.0f || ellipse.ry <= 0.0f) {
		throw std::invalid_argument("Ellipse geometry contains invalid dimensions");
	}

	constexpr int pointCount = 5;
	std::vector<Point2D> result;
	result.reserve(pointCount);

	std::vector<std::pair<float, float>> tangentPoints;
	tangentPoints.reserve(pointCount);

	constexpr float pi = 3.14159265358979323846f;
	// Sample equally spaced unit-circle points. These represent the tangent
	// directions used to construct the regular pentagon around the ellipse.
	for (int i = 0; i < pointCount; ++i) {
		const float angle = 2.0f * pi * i / static_cast<float>(pointCount);
		tangentPoints.push_back({ std::cos(angle), std::sin(angle) });
	}

	std::vector<std::pair<float, float>> pentagonPoints;
	pentagonPoints.reserve(pointCount);

	// Intersect each pair of adjacent tangent lines to obtain one vertex of
	// the normalized pentagon surrounding the unit circle.
	for (int i = 0; i < pointCount; ++i) {
		const int next = (i == pointCount - 1) ? 0 : i + 1;
		const float x1 = tangentPoints[i].first;
		const float y1 = tangentPoints[i].second;
		const float x2 = tangentPoints[next].first;
		const float y2 = tangentPoints[next].second;

		const float denominatorX = x2 * y1 - x1 * y2;
		const float denominatorY = x1 * y2 - x2 * y1;
		if (std::fabs(denominatorX) < 1e-6f || std::fabs(denominatorY) < 1e-6f) {
			throw std::domain_error("Around geometry contains parallel tangent lines");
		}

		pentagonPoints.push_back({
			(y1 - y2) / denominatorX,
			(x1 - x2) / denominatorY
		});
	}

	// Map the normalized vertices onto the requested ellipse by applying its
	// radii and translating the result to its center.
	for (const auto& point : pentagonPoints) {
		result.push_back({
			ellipse.cx + point.first * ellipse.rx,
			ellipse.cy + point.second * ellipse.ry
		});
	}

	return result;
}

