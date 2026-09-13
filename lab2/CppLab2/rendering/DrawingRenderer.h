#pragma once

#include "../domain/DrawingDefinition.h"
#include "../domain/ResolvedRelationships.h"
#include <windows.h>

// Describes a native window's client-area dimensions.
struct WindowGeometry {
	int width;
	int height;
};

// Renders authored and resolved geometry using the Win32 GDI backend.
class DrawingRenderer {
public:
	void Draw(const DrawingDefinition& definition, const ResolvedRelationships& resolvedRelationships, HDC hdc) const;

private:
	void DrawShape(HDC hdc, const ShapeDefinition& shape) const;
	void DrawGeometry(HDC hdc, const IGeometry& geometry, DrawingColor color) const;
	static void DrawEllipse(HDC hdc, const EllipseGeometry& geometry, DrawingColor color);
	static void DrawPolygon(HDC hdc, const PolygonGeometry& geometry, DrawingColor color);
	static void DrawPolygon(HDC hdc, const std::vector<Point2D>& points, DrawingColor color);
};

