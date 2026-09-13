#include "SceneController.h"

void SceneController::SetClientSize(int width, int height) {
	if (width <= 0 || height <= 0) {
		return;
	}

	if (referenceWidth_ == 0 || referenceHeight_ == 0) {
		referenceWidth_ = width;
		referenceHeight_ = height;
	}

	clientWidth_ = width;
	clientHeight_ = height;
	RebuildDefinition();
}

void SceneController::RebuildDefinition() {
	const float scaleX = static_cast<float>(clientWidth_) / referenceWidth_;
	const float scaleY = static_cast<float>(clientHeight_) / referenceHeight_;

	DrawingDefinition definition;
	ShapeDefinition mainShape;
	mainShape.id = "mainEllipse";
	mainShape.geometry = std::make_unique<EllipseGeometry>(
		static_cast<float>(clientWidth_) / 2.0f,
		static_cast<float>(clientHeight_) / 2.0f,
		125.0f * scaleX,
		50.0f * scaleY);
	mainShape.color = 0x000000FF;
	definition.shapes.push_back(std::move(mainShape));
	definition.relationships.push_back({
		RelationshipKind::Around,
		"mainEllipse",
		{ Kind::Pentagon, With::TangentMidpoints },
		0x00000000
	});
	definition_ = std::move(definition);
}

