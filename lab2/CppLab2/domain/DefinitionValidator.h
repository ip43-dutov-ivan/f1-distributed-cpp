#pragma once

#include "DrawingDefinition.h"

// Checks invariants that must hold before a drawing definition is resolved.
class DefinitionValidator {
public:
	void Validate(const DrawingDefinition& definition) const;
};

