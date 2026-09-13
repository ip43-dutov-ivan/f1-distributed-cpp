#pragma once

#include "GeometryTypes.h"
#include "../geometry/AroundRequest.h"
#include <memory>
#include <string>
#include <vector>

using DrawingColor = unsigned int;

// Defines an authored drawable shape and its stable scene identifier.
struct ShapeDefinition {
	std::string id;
	std::unique_ptr<IGeometry> geometry;
	DrawingColor color = 0x000000FF;

	ShapeDefinition() = default;
	ShapeDefinition(const ShapeDefinition& other)
		: id(other.id),
		  geometry(other.geometry ? other.geometry->Clone() : nullptr),
		  color(other.color) {
	}
	ShapeDefinition& operator=(const ShapeDefinition& other) {
		if (this != &other) {
			id = other.id;
			geometry = other.geometry ? other.geometry->Clone() : nullptr;
			color = other.color;
		}
		return *this;
	}
	ShapeDefinition(ShapeDefinition&&) noexcept = default;
	ShapeDefinition& operator=(ShapeDefinition&&) noexcept = default;
};

// Defines an authored polygon that is stored directly in the scene.
struct PolygonDefinition {
	std::vector<Point2D> points;
	DrawingColor color = 0x00000000;
};

enum class RelationshipKind {
	Around
};

// Describes geometry that must be generated relative to an authored shape.
struct RelationshipDefinition {
	RelationshipKind kind;
	std::string targetId;
	AroundRequest request;
	DrawingColor color = 0x00000000;
};

// Contains the authored shapes, polygons, and relationships of a scene.
struct DrawingDefinition {
	std::vector<ShapeDefinition> shapes;
	std::vector<PolygonDefinition> polygons;
	std::vector<RelationshipDefinition> relationships;
};

