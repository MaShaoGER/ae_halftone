#include "RasterCore.h"
namespace pattern {
bool renderRasterGPUBytes(const RasterFrame&, void*, int, RasterOutput, RenderControl, GPUStats*) { return false; }
bool renderRasterGPU(const RasterFrame&, float*, int, RenderControl, GPUStats*) { return false; }
}
