#pragma once

#include "GeometryTypes.h"
#include "DrawingDefinition.h"
#include <memory>
#include <string>
#include <vector>

// Stores geometry generated for one relationship in a drawing definition.
struct ResolvedRelationship {
	std::string targetId;
	std::unique_ptr<IGeometry> geometry;
	DrawingColor color = 0x00000000;

	ResolvedRelationship() = default;
	ResolvedRelationship(const ResolvedRelationship&) = delete;
	ResolvedRelationship& operator=(const ResolvedRelationship&) = delete;
	ResolvedRelationship(ResolvedRelationship&&) noexcept = default;
	ResolvedRelationship& operator=(ResolvedRelationship&&) noexcept = default;
};

// Collects generated relationship geometry without duplicating authored shapes.
struct ResolvedRelationships {
	std::vector<ResolvedRelationship> items;
};

