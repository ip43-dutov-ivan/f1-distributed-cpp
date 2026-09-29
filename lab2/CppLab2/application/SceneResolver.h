#pragma once

#include "../domain/DefinitionValidator.h"
#include "../domain/ResolvedRelationships.h"
#include "../geometry/IAroundEllipseGeometryGenerator.h"

// Converts authored relationships into generated geometry for rendering.
class SceneResolver {
public:
	SceneResolver(
		const DefinitionValidator& validationEngine,
		const IAroundEllipseGeometryGenerator& positioningEngine);

	ResolvedRelationships Resolve(const DrawingDefinition& definition) const;

private:
	const DefinitionValidator& validationEngine_;
	const IAroundEllipseGeometryGenerator& positioningEngine_;
};

