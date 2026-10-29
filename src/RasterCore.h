#pragma once
#include "PatternCore.h"
#include "RasterShared.h"
#include <cstdint>

namespace pattern {
struct RasterFrame {
    RasterUniforms uniforms{};
    std::vector<float> image; // Cropped premultiplied RGBA; coordinates retain the original canvas.
    std::vector<RasterStamp> stamps;
    std::vector<uint32_t> tileOffsets, tileIndices; // Stable back-to-front order, no per-tile truncation.
};
RasterFrame prepareRaster(const Settings& settings, int width, int height,
                          float scaleX = 1, float scaleY = 1, RenderControl control = {});
void renderRasterCPU(const RasterFrame& frame, float* output, int rowFloats,
                     int originY, int height, RenderControl control = {});

struct GPUStats { double milliseconds = 0; };
enum class RasterOutput { RGBAFloat32, ARGB8, ARGB16 };
bool renderRasterGPUBytes(const RasterFrame& frame, void* output, int rowBytes, RasterOutput format,
                          RenderControl control = {}, GPUStats* stats = nullptr);
// Mac implementation uses a separately owned Metal device from the CPU render entry point.
// This is a hybrid path: AE input/output remain CPU worlds. No SmartFX GPU capability is advertised.
bool renderRasterGPU(const RasterFrame& frame, float* output, int rowFloats,
                     RenderControl control = {}, GPUStats* stats = nullptr);
}
