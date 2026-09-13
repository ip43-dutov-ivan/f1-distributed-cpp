#include "SceneResolver.h"
#include <stdexcept>
#include <utility>

SceneResolver::SceneResolver(
	const DefinitionValidator& validationEngine,
	const IAroundEllipseGeometryGenerator& positioningEngine)
	: validationEngine_(validationEngine),
	  positioningEngine_(positioningEngine) {
}

ResolvedRelationships SceneResolver::Resolve(const DrawingDefinition& definition) const {
	validationEngine_.Validate(definition);

	ResolvedRelationships resolved;

	for (const auto& relationship : definition.relationships) {
		if (relationship.kind != RelationshipKind::Around) {
			throw std::invalid_argument("Unsupported relationship kind");
		}

		const ShapeDefinition* target = nullptr;
		for (const auto& shape : definition.shapes) {
			if (shape.id == relationship.targetId) {
				target = &shape;
				break;
			}
		}

		if (!target || !target->geometry) {
			throw std::invalid_argument("Relationship target geometry is unavailable");
		}
		if (target->geometry->GetKind() != GeometryKind::Ellipse) {
			throw std::invalid_argument("Around relationship requires ellipse geometry");
		}

		const auto& ellipse = static_cast<const EllipseGeometry&>(*target->geometry);
		ResolvedRelationship resolvedRelationship;
		resolvedRelationship.targetId = relationship.targetId;
		resolvedRelationship.geometry = std::make_unique<PolygonGeometry>(
			positioningEngine_.CalculateAround(ellipse, relationship.request));
		resolvedRelationship.color = relationship.color;
		resolved.items.push_back(std::move(resolvedRelationship));
	}

	return resolved;
}

