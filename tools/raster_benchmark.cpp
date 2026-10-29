#include "../src/RasterCore.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

using Clock=std::chrono::steady_clock;
static double ms(Clock::time_point t) { return std::chrono::duration<double,std::milli>(Clock::now()-t).count(); }
static double median(std::vector<double> a) { std::sort(a.begin(),a.end());return a[a.size()/2]; }
int main() {
    pattern::Settings warm; warm.width=warm.height=16;
    auto wf=pattern::prepareRaster(warm,16,16);std::vector<float> wo(16*16*4);
    auto start=Clock::now();bool available=pattern::renderRasterGPU(wf,wo.data(),64);
    std::printf("{\"metalAvailable\":%s,\"firstMetalCallMs\":%.3f}\n",available?"true":"false",ms(start));
    if(!available) return 1;
    // Same 1080p source dimensions and parameter values as the earlier synthetic benchmark.
    std::vector<float> source(1920*1080*4,0),map(201*121*4);
    for(int y=0;y<1080;y++) for(int x=0;x<1920;x++) if(std::abs(x-960)+std::abs(y-540)<250) {
        float* p=&source[(size_t(y)*1920+x)*4];p[0]=p[1]=p[2]=p[3]=1;
    }
    for(int y=0;y<121;y++) for(int x=0;x<201;x++) {
        const float t=std::min(1.f,std::hypot(float(x-70),float(y-40))/50);
        float* p=&map[(y*201+x)*4];p[0]=p[1]=p[2]=t;p[3]=1;
    }
    for(float scale:{.5f,1.f}) for(float alpha:{1.f,.35f}) {
        pattern::Settings s;s.width=1920;s.height=1080;s.spacingDense=92;s.spacingSparse=2;s.rowSpacing=40;
        s.size=330;s.gradientStrength=.79f;s.shape=pattern::Shape::Custom;s.fillColor.a=alpha;
        s.customStamp={source.data(),1920,1080,1920*4};s.gradientMap={map.data(),201,121,804};
        const int w=int(1920*scale),h=int(1080*scale);
        std::vector<float> old(size_t(w)*h*4),cpu(old.size()),gpu(old.size());
        start=Clock::now();pattern::render(s,old.data(),w,h,w*4,scale,scale);double oldMs=ms(start);
        start=Clock::now();auto f=pattern::prepareRaster(s,w,h,scale,scale);double prepareMs=ms(start);
        start=Clock::now();pattern::renderRasterCPU(f,cpu.data(),w*4,0,h);double cpuMs=ms(start)+prepareMs;
        std::vector<double> totals,kernels;
        for(int trial=0;trial<5;trial++) {
            start=Clock::now();auto frame=pattern::prepareRaster(s,w,h,scale,scale);pattern::GPUStats stats;
            if(!pattern::renderRasterGPU(frame,gpu.data(),w*4,{},&stats)) return 2;
            totals.push_back(ms(start));kernels.push_back(stats.milliseconds);
        }
        float maxCPU=0,maxGPU=0,maxBackend=0;
        for(size_t i=0;i<old.size();i++) {maxCPU=std::max(maxCPU,std::abs(old[i]-cpu[i]));maxGPU=std::max(maxGPU,std::abs(old[i]-gpu[i]));maxBackend=std::max(maxBackend,std::abs(cpu[i]-gpu[i]));}
        std::printf("{\"scale\":%.2f,\"opacity\":%.2f,\"stamps\":%zu,\"indexEntries\":%zu,\"materialBytes\":%zu,\"referenceMs\":%.3f,\"prepareMs\":%.3f,\"cpuTotalMs\":%.3f,\"metalTotalMedianMs\":%.3f,\"metalKernelMedianMs\":%.3f,\"speedup\":%.2f,\"maxCPUError\":%.9f,\"maxMetalError\":%.9f}\n",
            scale,alpha,f.stamps.size(),f.tileIndices.size(),f.image.size()*sizeof(float),oldMs,prepareMs,cpuMs,median(totals),median(kernels),oldMs/median(totals),maxCPU,maxGPU);
        std::fflush(stdout);
        if(maxCPU>1e-4f || maxGPU>1e-4f || maxBackend>3e-5f) return 3;
    }
}
