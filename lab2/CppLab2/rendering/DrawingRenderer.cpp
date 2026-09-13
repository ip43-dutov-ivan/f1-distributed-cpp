#include "DrawingRenderer.h"
#include <cmath>
#include <stdexcept>
#include <vector>

namespace {
// Owns a GDI handle and releases it when the wrapper leaves scope.
class GdiObject final {
public:
	explicit GdiObject(HGDIOBJ handle) : handle_(handle) {}
	~GdiObject() { if (handle_) DeleteObject(handle_); }
	GdiObject(const GdiObject&) = delete;
	GdiObject& operator=(const GdiObject&) = delete;
	operator HGDIOBJ() const { return handle_; }
private:
	HGDIOBJ handle_;
};

// Selects a GDI object temporarily and restores the previous object on exit.
class SelectedObject final {
public:
	SelectedObject(HDC hdc, HGDIOBJ object) : hdc_(hdc), old_(SelectObject(hdc, object)) {
		if (!old_ || old_ == HGDI_ERROR) throw std::runtime_error("Unable to select GDI object");
	}
	~SelectedObject() { SelectObject(hdc_, old_); }
	SelectedObject(const SelectedObject&) = delete;
	SelectedObject& operator=(const SelectedObject&) = delete;
private:
	HDC hdc_;
	HGDIOBJ old_;
};
}

void DrawingRenderer::Draw(
	const DrawingDefinition& definition,
	const ResolvedRelationships& resolvedRelationships,
	HDC hdc) const {
	for (const auto& shape : definition.shapes) {
		DrawShape(hdc, shape);
	}

	for (const auto& polygon : definition.polygons) {
		DrawPolygon(hdc, polygon.points, polygon.color);
	}

	for (const auto& relationship : resolvedRelationships.items) {
		if (relationship.geometry) {
			DrawGeometry(hdc, *relationship.geometry, relationship.color);
		}
	}
}

void DrawingRenderer::DrawShape(HDC hdc, const ShapeDefinition& shape) const {
	if (!shape.geometry) {
		return;
	}

	DrawGeometry(hdc, *shape.geometry, shape.color);
}

void DrawingRenderer::DrawGeometry(HDC hdc, const IGeometry& geometry, DrawingColor color) const {
	switch (geometry.GetKind()) {
	case GeometryKind::Ellipse:
		DrawEllipse(hdc, static_cast<const EllipseGeometry&>(geometry), color);
		break;
	case GeometryKind::Polygon:
		DrawPolygon(hdc, static_cast<const PolygonGeometry&>(geometry), color);
		break;
	default:
		throw std::invalid_argument("Unsupported geometry kind");
	}
}

void DrawingRenderer::DrawEllipse(HDC hdc, const EllipseGeometry& geometry, DrawingColor color) {
	GdiObject hBrush(CreateSolidBrush(static_cast<COLORREF>(color)));
	if (!hBrush) throw std::runtime_error("Unable to create ellipse brush");
	SelectedObject selectedBrush(hdc, hBrush);
	GdiObject hPen(CreatePen(PS_SOLID, 1, static_cast<COLORREF>(color)));
	if (!hPen) throw std::runtime_error("Unable to create ellipse pen");
	SelectedObject selectedPen(hdc, hPen);
	Ellipse(hdc,
		LONG(geometry.cx - geometry.rx), LONG(geometry.cy - geometry.ry),
		LONG(geometry.cx + geometry.rx), LONG(geometry.cy + geometry.ry));
}

void DrawingRenderer::DrawPolygon(HDC hdc, const PolygonGeometry& geometry, DrawingColor color) {
	DrawPolygon(hdc, geometry.points, color);
}

void DrawingRenderer::DrawPolygon(HDC hdc, const std::vector<Point2D>& points, DrawingColor color) {
	if (points.empty()) {
		return;
	}

	GdiObject hPen(CreatePen(PS_SOLID, 2, static_cast<COLORREF>(color)));
	if (!hPen) throw std::runtime_error("Unable to create polygon pen");
	SelectedObject selectedPen(hdc, hPen);
	std::vector<POINT> nativePoints;
	nativePoints.reserve(points.size());
	for (const auto& point : points) {
		nativePoints.push_back({ LONG(std::lround(point.x)), LONG(std::lround(point.y)) });
	}
	Polyline(hdc, nativePoints.data(), (int)nativePoints.size());
	if (points.size() > 1) {
		MoveToEx(hdc, nativePoints.back().x, nativePoints.back().y, NULL);
		LineTo(hdc, nativePoints.front().x, nativePoints.front().y);
	}
}

