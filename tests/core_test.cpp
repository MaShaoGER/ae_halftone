#include "../src/PatternCore.h"
#include <cassert>
#include <cmath>
#include <vector>
#include <stdexcept>
#include <future>
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
    float map[16]={0,0,0,1, .3f,.3f,.3f,1, .7f,.7f,.7f,1, 1,1,1,1};
    pattern::Settings band; band.width=100; band.height=60; band.size=12; band.rowSpacing=15;
    band.shape=pattern::Shape::Custom; band.customStamp={stamp,2,2,8}; band.gradientMap={map,4,1,16};
    auto frame=[](const pattern::Settings& cfg) { std::vector<float> out(100*60*4); pattern::render(cfg,out.data(),100,60,400); return out; };
    auto full=frame(band); auto plan=pattern::makePlan(band); std::vector<float> tiled(full.size());
    for(int y=0;y<60;y+=7) pattern::renderBand(plan,tiled.data()+y*400,100,std::min(7,60-y),400,1,1,y);
    assert(full==tiled);
    auto fetch=[](const void* ctx,int x,int y) { auto p=static_cast<const float*>(ctx)+(y*2+x)*4; return pattern::Color{p[0],p[1],p[2],p[3]}; };
    band.customStamp={nullptr,2,2,0,fetch,stamp}; assert(frame(band)==full);
    std::vector<std::future<std::vector<float>>> futures; std::vector<std::vector<float>> expected;
    for(int i=0;i<8;++i) { auto cfg=band; cfg.angle=float(i*11); cfg.shapeRotation=float(i*7); cfg.offsetX=float(i); if(i%2) cfg.shape=pattern::Shape::Diamond; expected.push_back(frame(cfg)); futures.push_back(std::async(std::launch::async,[cfg,frame]{return frame(cfg);})); }
    for(int i=0;i<8;++i) assert(futures[i].get()==expected[i]);
}
