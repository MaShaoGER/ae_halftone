#pragma once
#include <vector>

namespace pattern {
struct Color { float r = 1, g = 1, b = 1, a = 1; }; // Straight RGBA.
struct ImageView { const float* pixels = nullptr; int width = 0, height = 0, rowFloats = 0; Color (*pixelAt)(const void*, int, int) = nullptr; const void* context = nullptr; }; // Premultiplied RGBA.
enum class Shape { Circle, Square, Diamond, Custom };
struct Settings {
    int width = 1920, height = 1080; // Full-resolution layer coordinates.
    float spacingDense = 20, spacingSparse = 60, rowSpacing = 40;
    float gradientStart = 0, gradientEnd = 1; // Normalized position along the rotated pattern's horizontal axis.
    float gradientStrength = 1; // 0 = uniform spacingDense, 1 = full gradient influence.
    float angle = 0, offsetX = 0, offsetY = 0, stagger = 0; // Degrees, full-resolution pixels, and row fraction.
    float size = 10, shapeRotation = 0; // Full-resolution diameter and degrees.
    Shape shape = Shape::Circle;
    Color fillColor{};
    ImageView customStamp{}, gradientMap{};
};
struct Stamp { float x, y, size; };
struct RenderControl { void* user = nullptr; void (*checkCancel)(void*) = nullptr; };
struct RenderPlan { Settings settings; std::vector<Stamp> stamps; };
// Gradient-map luminance is sampled at each position in output-layer space.
// Each row varies its horizontal spacing independently; row spacing and stamp
// size stay fixed. Dark maps to spacingDense, light to spacingSparse at full
// gradientStrength. At zero strength, all spacing is spacingDense.
// Backward extension of variable-spacing columns uses backward Euler steps;
// it is not the exact inverse of forward stepping. Throws std::length_error
// if candidate or stepping limits are exceeded.
std::vector<Stamp> layout(const Settings& settings);
// ImageView data and callback contexts must remain valid for the plan's lifetime.
RenderPlan makePlan(const Settings& settings);
// originY is the band's absolute output row. Storage begins at the band itself.
void renderBand(const RenderPlan& plan, float* output, int outputWidth, int bandHeight,
                int rowFloats, float scaleX, float scaleY, int originY, RenderControl control = {});
// Output is premultiplied RGBA. scaleX/Y are output pixels per full-res pixel;
// rowFloats is the output row stride in floats. Clears the output first.
void render(const Settings& settings, float* output, int outputWidth, int outputHeight,
            int rowFloats, float scaleX = 1, float scaleY = 1, RenderControl control = {});
}
