#pragma once
// Scalar-only layout: the same bytes are consumed by C++, Metal and future HLSL adapters.
struct RasterStamp {
    float x, y, inverseRadius, reserved;
    int left, top, right, bottom;
};
struct RasterUniforms {
    int width, height, rowPixels, tileColumns;
    int imageWidth, imageHeight, sourceWidth, sourceHeight;
    int sourceX, sourceY, shape, tileSize;
    int outputFormat, outputRowBytes;
    float scaleX, scaleY, cosine, sine;
    float aspectX, aspectY, red, green, blue, alpha;
};
#ifndef __METAL_VERSION__
static_assert(sizeof(RasterStamp)==32, "GPU stamp layout changed");
static_assert(sizeof(RasterUniforms)==96, "GPU uniform layout changed");
#endif
