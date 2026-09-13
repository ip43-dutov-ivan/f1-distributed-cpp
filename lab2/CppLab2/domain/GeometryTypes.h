#pragma once

#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

enum class GeometryKind {
	Ellipse,
	Polygon
};

// Represents a point in two-dimensional drawing coordinates.
struct Point2D {
	float x = 0.0f;
	float y = 0.0f;
};

// Defines the common polymorphic contract for drawable geometry values.
struct IGeometry {
	virtual ~IGeometry() = default;
	virtual GeometryKind GetKind() const = 0;
	virtual std::unique_ptr<IGeometry> Clone() const = 0;
	virtual void Resize(float scaleX, float scaleY) = 0;
};

// Represents an ellipse using a center point and horizontal/vertical radii.
struct EllipseGeometry final : IGeometry {
	float cx = 0.0f;
	float cy = 0.0f;
	float rx = 0.0f;
	float ry = 0.0f;

	EllipseGeometry(float centerX, float centerY, float radiusX, float radiusY)
		: cx(centerX), cy(centerY), rx(radiusX), ry(radiusY) {
		if (radiusX <= 0.0f || radiusY <= 0.0f) {
			throw std::invalid_argument("Ellipse radii must be positive");
		}
	}

	GeometryKind GetKind() const override {
		return GeometryKind::Ellipse;
	}

	std::unique_ptr<IGeometry> Clone() const override {
		return std::make_unique<EllipseGeometry>(*this);
	}

	void Resize(float scaleX, float scaleY) override {
		rx *= scaleX;
		ry *= scaleY;
	}
};

// Represents a polygon as an ordered sequence of drawing points.
struct PolygonGeometry final : IGeometry {
	std::vector<Point2D> points;

	PolygonGeometry() = default;

	explicit PolygonGeometry(std::vector<Point2D> polygonPoints)
		: points(std::move(polygonPoints)) {
	}

	GeometryKind GetKind() const override {
		return GeometryKind::Polygon;
	}

	std::unique_ptr<IGeometry> Clone() const override {
		return std::make_unique<PolygonGeometry>(*this);
	}

	void Resize(float scaleX, float scaleY) override {
		// Scale every vertex relative to the origin while preserving point order.
		for (auto& point : points) {
			point.x *= scaleX;
			point.y *= scaleY;
		}
	}
};



