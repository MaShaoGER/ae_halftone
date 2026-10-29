#include "RasterCore.h"
#include "RasterMetalSource.h" // Generated at build time; no offline Metal compiler is needed.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <algorithm>
#include <cstring>

namespace pattern {
namespace {
struct MetalState {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLComputePipelineState> pipeline;
    MetalState() {
        @autoreleasepool {
            device=MTLCreateSystemDefaultDevice();
            if(!device) return;
            NSError* error=nil;
            MTLCompileOptions* options=[[MTLCompileOptions alloc] init];
            if (@available(macOS 15.0, *)) options.mathMode=MTLMathModeSafe;
            else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                options.fastMathEnabled=NO;
#pragma clang diagnostic pop
            }
            id<MTLLibrary> library=[device newLibraryWithSource:[NSString stringWithUTF8String:rasterMetalSource]
                                                       options:options error:&error];
            if(!library) { NSLog(@"Ordered Patterns Metal compilation: %@",error); return; }
            id<MTLFunction> function=[library newFunctionWithName:@"orderedRaster"];
            if(!function) return;
            pipeline=[device newComputePipelineStateWithFunction:function error:&error];
            queue=[device newCommandQueue];
        }
    }
};
}

bool renderRasterGPUBytes(const RasterFrame& frame, void* output, int rowBytes, RasterOutput format, RenderControl control, GPUStats* stats) {
    const int pixelBytes=format==RasterOutput::RGBAFloat32?16:format==RasterOutput::ARGB16?8:4;
    if(!output || rowBytes<frame.uniforms.width*pixelBytes || rowBytes%pixelBytes) return false;
    if(control.checkCancel) control.checkCancel(control.user);
    @autoreleasepool {
        static MetalState state; // C++ initialization is synchronized; all published state is read-only.
        if(!state.pipeline || !state.queue) return false;
        auto q=frame.uniforms;
        q.outputFormat=int(format);q.outputRowBytes=rowBytes;
        // Independent per-frame resources preserve multi-frame safety. No borrowed AE pixels are cached.
        auto buffer=[&](const void* bytes,size_t size)->id<MTLBuffer> {
            if(size>state.device.maxBufferLength) return nil;
            if(!size) { uint32_t zero=0; return [state.device newBufferWithBytes:&zero length:4 options:MTLResourceStorageModeShared]; }
            return [state.device newBufferWithBytes:bytes length:size options:MTLResourceStorageModeShared];
        };
        id<MTLBuffer> stamps=buffer(frame.stamps.data(),frame.stamps.size()*sizeof(RasterStamp));
        id<MTLBuffer> offsets=buffer(frame.tileOffsets.data(),frame.tileOffsets.size()*sizeof(uint32_t));
        id<MTLBuffer> indices=buffer(frame.tileIndices.data(),frame.tileIndices.size()*sizeof(uint32_t));
        id<MTLBuffer> image=buffer(frame.image.data(),frame.image.size()*sizeof(float));
        const size_t bytes=size_t(rowBytes)*q.height;
        if(bytes>state.device.maxBufferLength) return false;
        id<MTLBuffer> result=[state.device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
        if(!stamps || !offsets || !indices || !image || !result) return false;
        id<MTLCommandBuffer> command=[state.queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder=[command computeCommandEncoder];
        if(!command || !encoder) return false;
        [encoder setComputePipelineState:state.pipeline];
        [encoder setBytes:&q length:sizeof(q) atIndex:0];
        [encoder setBuffer:stamps offset:0 atIndex:1]; [encoder setBuffer:offsets offset:0 atIndex:2];
        [encoder setBuffer:indices offset:0 atIndex:3]; [encoder setBuffer:image offset:0 atIndex:4];
        [encoder setBuffer:result offset:0 atIndex:5];
        const NSUInteger tw=state.pipeline.threadExecutionWidth;
        const NSUInteger th=std::min<NSUInteger>(8,state.pipeline.maxTotalThreadsPerThreadgroup/tw);
        [encoder dispatchThreads:MTLSizeMake(q.width,q.height,1) threadsPerThreadgroup:MTLSizeMake(tw,th,1)];
        [encoder endEncoding];
        [command commit];
        // AE's CPU render entry requires completion before returning its output.
        [command waitUntilCompleted];
        if(command.status!=MTLCommandBufferStatusCompleted) return false;
        if(control.checkCancel) control.checkCancel(control.user);
        if(stats) stats->milliseconds=(command.GPUEndTime-command.GPUStartTime)*1000;
        for(int y=0;y<q.height;y++) {
            if((y%64)==0 && control.checkCancel) control.checkCancel(control.user);
            std::memcpy(static_cast<char*>(output)+size_t(y)*rowBytes,
                static_cast<const char*>(result.contents)+size_t(y)*rowBytes,size_t(q.width)*pixelBytes);
        }
        return true;
    }
}
bool renderRasterGPU(const RasterFrame& frame, float* output, int stride, RenderControl control, GPUStats* stats) {
    return renderRasterGPUBytes(frame,output,stride*int(sizeof(float)),RasterOutput::RGBAFloat32,control,stats);
}
}
