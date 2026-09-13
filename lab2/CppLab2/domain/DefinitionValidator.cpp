#include "DefinitionValidator.h"
#include <stdexcept>
#include <unordered_set>

void DefinitionValidator::Validate(const DrawingDefinition& definition) const {
	std::unordered_set<std::string> ids;
	for (const auto& shape : definition.shapes) {
		if (shape.id.empty() || !ids.insert(shape.id).second) {
			throw std::invalid_argument("Shape IDs must be non-empty and unique");
		}
		if (!shape.geometry) {
			throw std::invalid_argument("Shape geometry cannot be null");
		}
	}
	for (const auto& relationship : definition.relationships) {
		if (relationship.targetId.empty() || !ids.contains(relationship.targetId)) {
			throw std::invalid_argument("Relationship target does not exist");
		}
	}
}

