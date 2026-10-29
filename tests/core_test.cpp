#include "../src/PatternCore.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <vector>
#include <stdexcept>
#include <future>
#include <limits>
int main() {
    pattern::Settings s; s.width=100; s.height=60; s.spacingDense=8; s.spacingSparse=20; s.size=4;
    auto a=pattern::layout(s), b=pattern::layout(s); assert(!a.empty() && a.size()==b.size());
    for(size_t i=0;i<a.size();++i) { assert(a[i].x==b[i].x && a[i].y==b[i].y); assert(a[i].size==4); }
    std::vector<float> row; for(auto p:a) if(p.y==a[0].y) row.push_back(p.x);
    assert(row.size()>=4); for(size_t i=2;i<row.size();++i) assert(row[i]-row[i-1] >= row[i-1]-row[i-2]-.001f);
    s.fillColor={1,.5f,0,.5f}; std::vector<float> pixels(100*60*4), low(50*30*4);
    pattern::render(s,pixels.data(),100,60,400); pattern::render(s,low.data(),50,30,200,.5f,.5f);
    bool visible=false; for(size_t i=0;i<pixels.size();i+=4) { assert(pixels[i+3]>=0 && pixels[i+3]<=1); assert(pixels[i]<=pixels[i+3]+.0001f); visible |= pixels[i+3]>0; } assert(visible);
    // Raster sampling differs at different resolutions, while geometry stays in full-res coordinates.
    auto again=pattern::layout(s); assert(again.size()==a.size()); for(size_t i=0;i<a.size();++i) assert(again[i].x==a[i].x && again[i].y==a[i].y);
    bool lowVisible=false; for(size_t i=3;i<low.size();i+=4) lowVisible |= low[i]>0; assert(lowVisible);
    int calls=0; bool cancelled=false;
    try { pattern::render(s,pixels.data(),100,60,400,1,1,{&calls,[](void* user) { if(++*static_cast<int*>(user)==3) throw 7; }}); }
    catch(int code) { cancelled=(code==7); } assert(cancelled && calls==3);
    s.size=8; auto larger=pattern::layout(s); for (auto p:a) { bool found=false; for(auto q:larger) if(p.x==q.x && p.y==q.y) found=true; assert(found); }
    s.shape=pattern::Shape::Custom; pattern::render(s,pixels.data(),100,60,400); for(float v:pixels) assert(v==0);
    float image[8]={1,1,1,1,1,1,1,1}; s.customStamp={image,2,1,8};
    s.width=20; s.height=20; s.spacingDense=s.spacingSparse=s.rowSpacing=40; s.size=12; s.offsetX=s.offsetY=std::hypot(20.f,20.f)*.5f;
    auto centers=pattern::layout(s); assert(!centers.empty());
    std::vector<float> custom(20*20*4); pattern::render(s,custom.data(),20,20,80);
    // At a stamp's horizontal and vertical midpoints, the 2:1 image is wider than tall.
    auto p=centers[0]; int cx=int(p.x), cy=int(p.y); assert(cx==10 && cy==10); assert(custom[(cy*20+cx+4)*4+3]>0); assert(custom[((cy+4)*20+cx)*4+3]==0);
    s.width=s.height=32768; s.spacingDense=s.spacingSparse=s.rowSpacing=2;
    bool threw=false; try { (void)pattern::layout(s); } catch(const std::length_error&) { threw=true; } assert(threw);
    pattern::Settings phase; phase.width=100; phase.height=80; phase.spacingDense=phase.spacingSparse=phase.rowSpacing=20; phase.size=4;
    auto original=pattern::layout(phase); phase.offsetX=2000; phase.offsetY=2000; auto shifted=pattern::layout(phase);
    assert(original.size()==shifted.size() && !shifted.empty());
    for(size_t i=0;i<original.size();++i) { assert(std::abs(original[i].x-shifted[i].x)<.001f); assert(std::abs(original[i].y-shifted[i].y)<.001f); }
    pattern::Settings edge; edge.width=edge.height=20; edge.spacingDense=edge.spacingSparse=edge.rowSpacing=100; edge.shape=pattern::Shape::Square; edge.size=30;
    edge.offsetX=std::hypot(20.f,20.f)*.5f-20; edge.offsetY=std::hypot(20.f,20.f)*.5f;
    std::vector<float> overlap(20*20*4); pattern::render(edge,overlap.data(),20,20,80);
    assert(overlap[(10*20+1)*4+3]>0);
    float stamp[16]={1,0,0,1, 0,1,0,1, 0,0,1,1, 1,1,1,1};
    float map[32]={0,0,0,1, .3f,.3f,.3f,1, .7f,.7f,.7f,1, 1,1,1,1,
                   1,1,1,1, .7f,.7f,.7f,1, .3f,.3f,.3f,1, 0,0,0,1};
    pattern::Settings band; band.width=100; band.height=60; band.size=12; band.rowSpacing=15;
    band.shape=pattern::Shape::Custom; band.customStamp={stamp,2,2,8}; band.gradientMap={map,4,2,16};
    auto frame=[](const pattern::Settings& cfg) { std::vector<float> out(100*60*4); pattern::render(cfg,out.data(),100,60,400); return out; };
    auto full=frame(band); auto plan=pattern::makePlan(band); std::vector<float> tiled(full.size());
    for(int y=0;y<60;y+=7) pattern::renderBand(plan,tiled.data()+y*400,100,std::min(7,60-y),400,1,1,y);
    assert(full==tiled);
    auto fetch=[](const void* ctx,int x,int y) { auto p=static_cast<const float*>(ctx)+(y*2+x)*4; return pattern::Color{p[0],p[1],p[2],p[3]}; };
    band.customStamp={nullptr,2,2,0,fetch,stamp}; assert(frame(band)==full);
    std::vector<std::future<std::vector<float>>> futures; std::vector<std::vector<float>> expected;
    for(int i=0;i<8;++i) { auto cfg=band; cfg.angle=float(i*11); cfg.shapeRotation=float(i*7); cfg.offsetX=float(i); if(i%2) cfg.shape=pattern::Shape::Diamond; expected.push_back(frame(cfg)); futures.push_back(std::async(std::launch::async,[cfg,frame]{return frame(cfg);})); }
    for(int i=0;i<8;++i) assert(futures[i].get()==expected[i]);
    // A vertical map must vary between rows, rather than repeat its centerline.
    float verticalMap[16]={0,0,0,1, 0,0,0,1, 1,1,1,1, 1,1,1,1};
    pattern::Settings vertical; vertical.width=200; vertical.height=120;
    vertical.spacingDense=8; vertical.spacingSparse=28; vertical.rowSpacing=20; vertical.size=4;
    const float radius=std::hypot(200.f,120.f)*.5f;
    vertical.offsetX=radius-100; vertical.offsetY=radius-60;
    vertical.gradientMap={verticalMap,2,2,8};
    auto verticalStamps=pattern::layout(vertical);
    for(float y : {0.f,40.f,80.f,120.f}) {
        std::vector<float> xs;
        for(auto p:verticalStamps) if(std::abs(p.y-y)<.001f) xs.push_back(p.x);
        assert(xs.size()>2);
        const float gap=8+20*y/120;
        assert(std::abs(xs[1]-xs[0]-gap)<.001f);
    }
    // Strength scales spacing influence for maps and built-in gradients,
    // including reversed endpoints; row placement and stamp size stay fixed.
    auto sameLayout=[](const std::vector<pattern::Stamp>& lhs,const std::vector<pattern::Stamp>& rhs) {
        assert(lhs.size()==rhs.size());
        for(size_t i=0;i<lhs.size();++i) assert(lhs[i].x==rhs[i].x && lhs[i].y==rhs[i].y && lhs[i].size==rhs[i].size);
    };
    for(bool useMap : {false,true}) for(bool reversed : {false,true}) for(float strength : {0.f,.5f,1.f}) {
        auto cfg=vertical; cfg.gradientStrength=strength;
        if(!useMap) cfg.gradientMap={};
        if(reversed) { std::swap(cfg.spacingDense,cfg.spacingSparse); cfg.gradientStart=.8f; cfg.gradientEnd=.2f; }
        auto stamps=pattern::layout(cfg);
        int checked=0;
        for(size_t i=1;i<stamps.size();++i) {
            auto p=stamps[i-1], q=stamps[i];
            assert(p.size==vertical.size);
            assert(std::abs(p.y/vertical.rowSpacing-std::round(p.y/vertical.rowSpacing))<.001f);
            if(p.y!=q.y || p.x<0 || p.y<0 || p.y>120) continue;
            float t=useMap ? p.y/120 : std::clamp((p.x/(2*radius)-cfg.gradientStart)/(cfg.gradientEnd-cfg.gradientStart),0.f,1.f);
            float gap=cfg.spacingDense+(cfg.spacingSparse-cfg.spacingDense)*t*strength;
            assert(std::abs(q.x-p.x-gap)<.002f); ++checked;
        }
        assert(checked>20);
        if(strength==0) { auto uniform=cfg; uniform.spacingSparse=uniform.spacingDense; uniform.gradientMap={}; sameLayout(stamps,pattern::layout(uniform)); }
    }
    auto strengthBounds=vertical;
    strengthBounds.gradientStrength=2; sameLayout(verticalStamps,pattern::layout(strengthBounds));
    strengthBounds.gradientStrength=std::numeric_limits<float>::quiet_NaN(); sameLayout(verticalStamps,pattern::layout(strengthBounds));
    strengthBounds.gradientStrength=0; auto zeroStrength=pattern::layout(strengthBounds);
    strengthBounds.gradientStrength=-1; sameLayout(zeroStrength,pattern::layout(strengthBounds));
    strengthBounds.gradientStrength=0;
    strengthBounds.gradientMap={nullptr,2,2,0,[](const void*,int,int)->pattern::Color { throw 42; },nullptr};
    sameLayout(zeroStrength,pattern::layout(strengthBounds)); // No map sampling at zero.
    // Rotated/offset rows sample the actual image location, not lattice y.
    vertical.angle=31; vertical.offsetX=13; vertical.offsetY=-17;
    auto rotated=pattern::layout(vertical);
    const float angle=31*3.14159265358979323846f/180;
    int checkedGaps=0;
    for(size_t i=1;i<rotated.size();++i) {
        auto p=rotated[i-1], q=rotated[i];
        const float dx=q.x-p.x, dy=q.y-p.y;
        if(std::abs(-std::sin(angle)*dx+std::cos(angle)*dy)>.001f || dx<=0) continue;
        if(p.x<0 || p.x>200 || p.y<0 || p.y>120) continue;
        assert(std::abs(std::hypot(dx,dy)-(8+20*p.y/120))<.002f);
        ++checkedGaps;
    }
    assert(checkedGaps>20);
    // Screenshot regression: an off-center radial map stays localized in y.
    std::vector<float> radialPixels(201*121*4);
    for(int y=0;y<121;++y) for(int x=0;x<201;++x) {
        float t=std::min(1.f,std::hypot(float(x-70),float(y-40))/35);
        auto* p=&radialPixels[(y*201+x)*4]; p[0]=p[1]=p[2]=t; p[3]=1;
    }
    auto radial=vertical; radial.angle=0; radial.offsetX=radius-100; radial.offsetY=radius-60;
    radial.spacingDense=28; radial.spacingSparse=4; radial.gradientMap={radialPixels.data(),201,121,804};
    auto radialStamps=pattern::layout(radial);
    std::vector<float> topXs,centerXs,bottomXs;
    for(auto p:radialStamps) {
        if(std::abs(p.y)<.001f) topXs.push_back(p.x);
        if(std::abs(p.y-40)<.001f) centerXs.push_back(p.x);
        if(std::abs(p.y-120)<.001f) bottomXs.push_back(p.x);
    }
    assert(topXs==bottomXs && centerXs.size()+5<topXs.size());
}
