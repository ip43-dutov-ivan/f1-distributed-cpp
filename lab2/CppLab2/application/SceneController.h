#pragma once

#include "../domain/DrawingDefinition.h"

// Maintains the current viewport size and builds the authored scene definition.
class SceneController {
public:
	void SetClientSize(int width, int height);
	const DrawingDefinition& Definition() const { return definition_; }

private:
	void RebuildDefinition();

	int clientWidth_ = 0;
	int clientHeight_ = 0;
	int referenceWidth_ = 0;
	int referenceHeight_ = 0;
	DrawingDefinition definition_;
};

