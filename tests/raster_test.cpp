#include "../src/RasterCore.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <future>
#include <stdexcept>
#include <vector>

static float difference(const std::vector<float>& a,const std::vector<float>& b) {
    assert(a.size()==b.size()); float error=0;
    for(size_t i=0;i<a.size();i++) { assert(std::isfinite(b[i])); error=std::max(error,std::abs(a[i]-b[i])); }
    return error;
}
int main(int argc,char**) {
    const bool requireGPU=argc>1;
    std::vector<float> source(37*23*4,0),map(7*5*4);
    for(int y=3;y<20;y++) for(int x=6;x<30;x++) {
        float* p=&source[(y*37+x)*4]; float a=((x+y)%4==0) ? .37f : 1.f;
        p[0]=float(x)/37*a; p[1]=float(y)/23*a; p[2]=.2f*a; p[3]=a;
    }
    for(int y=0;y<5;y++) for(int x=0;x<7;x++) {
        float* p=&map[(y*7+x)*4]; p[0]=p[1]=p[2]=float((x*3+y*5)%17)/16; p[3]=1;
    }
    float maxCPU=0,maxGPU=0;
    int cases=0,gpuCases=0;
    for(auto shape:{pattern::Shape::Circle,pattern::Shape::Square,pattern::Shape::Diamond,pattern::Shape::Custom})
    for(float rotation:{0.f,31.f,90.f}) for(float opacity:{1.f,.43f}) for(float scale:{1.f,.5f}) {
        pattern::Settings s; s.width=128; s.height=96; s.size=23.7f; s.spacingDense=8; s.spacingSparse=23; s.rowSpacing=11;
        s.shape=shape; s.shapeRotation=rotation; s.angle=17; s.offsetX=2.13f; s.offsetY=-.37f; s.stagger=.5f;
        s.fillColor={.7f,.3f,1.f,opacity}; s.customStamp={source.data(),37,23,37*4}; s.gradientMap={map.data(),7,5,28};
        const int w=int(128*scale),h=int(96*scale);
        std::vector<float> reference(w*h*4),cpu(reference.size()),gpu(reference.size(),-1),bands(reference.size());
        pattern::render(s,reference.data(),w,h,w*4,scale,scale);
        const auto f=pattern::prepareRaster(s,w,h,scale,scale);
        pattern::renderRasterCPU(f,cpu.data(),w*4,0,h);
        for(int y=0;y<h;y+=7) pattern::renderRasterCPU(f,bands.data()+size_t(y)*w*4,w*4,y,std::min(7,h-y));
        assert(cpu==bands);
        float error=difference(reference,cpu); maxCPU=std::max(maxCPU,error);
        if(error>2e-5f) { std::printf("CPU mismatch shape=%d rotation=%g opacity=%g scale=%g error=%g\n",int(shape),rotation,opacity,scale,error); return 1; }
        bool accelerated=pattern::renderRasterGPU(f,gpu.data(),w*4);
        if(requireGPU && !accelerated) { std::puts("Metal was required but unavailable"); return 1; }
        if(accelerated) {
            error=difference(cpu,gpu); maxGPU=std::max(maxGPU,error);
            if(error>3e-5f) { std::printf("GPU mismatch shape=%d rotation=%g error=%g\n",int(shape),rotation,error); return 1; }
            ++gpuCases;
        }
        ++cases;
    }
    // A >500-item translucent list must retain its whole tail (no game-style truncation or alpha threshold).
    pattern::RasterFrame tail;
    auto& q=tail.uniforms; q.width=q.height=q.rowPixels=q.tileColumns=1; q.tileSize=16;
    q.scaleX=q.scaleY=q.cosine=q.aspectX=q.aspectY=q.alpha=q.red=q.green=q.blue=1;
    q.sourceWidth=q.sourceHeight=q.imageWidth=q.imageHeight=1; q.shape=3;
    tail.image={.001f,.0005f,0,.001f}; tail.stamps.assign(700,{.5f,.5f,.01f,0,0,0,0,0});
    tail.tileOffsets={0,700}; for(unsigned i=0;i<700;i++) tail.tileIndices.push_back(i);
    std::vector<float> out(4),gpu(4);
    pattern::renderRasterCPU(tail,out.data(),4,0,1);
    assert(std::abs(out[3]-(1-std::pow(.999f,700)))<1e-5f && out[3]<.6f);
    if(requireGPU) { assert(pattern::renderRasterGPU(tail,gpu.data(),4)); assert(difference(out,gpu)<1e-5f); }
    // Singleton textures, invisible RGBA content, anisotropic scale, output padding and cancellation.
    for(int iw:{1,9}) for(int ih:{1,7}) {
        std::vector<float> image(iw*ih*4,0); image[0]=.25f; image[3]=.5f;
        pattern::Settings s; s.width=80; s.height=70; s.shape=pattern::Shape::Custom; s.size=31; s.shapeRotation=43;
        s.customStamp={image.data(),iw,ih,iw*4};
        auto f=pattern::prepareRaster(s,40,70,.5f,1);
        std::vector<float> old(40*70*4),a(old.size()),b(old.size());
        pattern::render(s,old.data(),40,70,160,.5f,1); pattern::renderRasterCPU(f,a.data(),160,0,70);
        assert(difference(old,a)<2e-5f);
        if(requireGPU) { assert(pattern::renderRasterGPU(f,b.data(),160)); assert(difference(a,b)<3e-5f); }
    }
    pattern::Settings empty; empty.width=33;empty.height=17;empty.shape=pattern::Shape::Custom;
    auto blank=pattern::prepareRaster(empty,33,17); std::vector<float> padded(17*136,-7);
    pattern::renderRasterCPU(blank,padded.data(),136,0,17);
    for(int y=0;y<17;y++) { for(int x=0;x<132;x++) assert(padded[y*136+x]==0); for(int x=132;x<136;x++) assert(padded[y*136+x]==-7); }
    if(requireGPU) { assert(pattern::renderRasterGPU(blank,padded.data(),136)); for(int y=0;y<17;y++) assert(padded[y*136+135]==-7); }
    int calls=0;bool cancelled=false;
    try { pattern::prepareRaster(empty,33,17,1,1,{&calls,[](void* u){++*static_cast<int*>(u);throw 7;}}); }
    catch(int e) {cancelled=e==7;} assert(cancelled && calls==1);
    // Share an immutable plan across concurrent frames; each Metal call must own its working buffers.
    std::vector<std::future<std::vector<float>>> jobs;
    for(int i=0;i<4;i++) jobs.push_back(std::async(std::launch::async,[&tail,requireGPU]{std::vector<float> a(4);if(requireGPU)assert(pattern::renderRasterGPU(tail,a.data(),4));else pattern::renderRasterCPU(tail,a.data(),4,0,1);return a;}));
    for(auto& job:jobs) assert(difference(out,job.get())<1e-5f);
    std::printf("Raster cases=%d Metal cases=%d max CPU/reference error=%g max Metal/CPU error=%g; tail, singleton, bands, cancellation and concurrency passed\n",cases,gpuCases,maxCPU,maxGPU);
}
