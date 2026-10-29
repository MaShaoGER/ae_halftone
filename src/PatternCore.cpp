#include "PatternCore.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pattern {
namespace {
constexpr float pi = 3.14159265358979323846f;
float clamp01(float v) { return std::max(0.f, std::min(1.f, v)); }
float safe(float v, float fallback) { return std::isfinite(v) ? v : fallback; }
bool valid(ImageView v) { return v.width > 0 && v.height > 0 && (v.pixelAt || (v.pixels && v.rowFloats >= v.width * 4)); }
Color sample(ImageView v, float u, float w) {
    if (!valid(v) || u < 0 || u > 1 || w < 0 || w > 1) return {0, 0, 0, 0};
    float x = u * (v.width - 1), y = w * (v.height - 1);
    int x0 = int(x), y0 = int(y), x1 = std::min(x0 + 1, v.width - 1), y1 = std::min(y0 + 1, v.height - 1);
    float tx = x - x0, ty = y - y0, c[4];
    auto at = [&](int xx,int yy) { if(v.pixelAt) return v.pixelAt(v.context,xx,yy); const float* p=v.pixels+yy*v.rowFloats+xx*4; return Color{p[0],p[1],p[2],p[3]}; };
    Color p00=at(x0,y0),p10=at(x1,y0),p01=at(x0,y1),p11=at(x1,y1);
    float v00[]={p00.r,p00.g,p00.b,p00.a},v10[]={p10.r,p10.g,p10.b,p10.a},v01[]={p01.r,p01.g,p01.b,p01.a},v11[]={p11.r,p11.g,p11.b,p11.a};
    for (int k = 0; k < 4; ++k) {
        c[k] = (v00[k]*(1-tx)+v10[k]*tx)*(1-ty)+(v01[k]*(1-tx)+v11[k]*tx)*ty;
    }
    return {c[0], c[1], c[2], c[3]};
}
}
std::vector<Stamp> layout(const Settings& s) {
    std::vector<Stamp> result;
    if (s.width <= 0 || s.height <= 0) return result;
    const float dense = std::clamp(safe(s.spacingDense, 20), 2.f, 4096.f), sparse = std::clamp(safe(s.spacingSparse, 60), 2.f, 4096.f);
    const float size = std::clamp(safe(s.size, 10), 0.f, 4096.f);
    if (size <= 0) return result;
    const float a = safe(s.angle, 0) * pi / 180, ca = std::cos(a), sa = std::sin(a);
    const float radius = std::hypot(float(s.width), float(s.height)) * .5f;
    const float cx = s.width*.5f + safe(s.offsetX,0), cy = s.height*.5f + safe(s.offsetY,0);
    const float start = safe(s.gradientStart,0), end = safe(s.gradientEnd,1);
    auto spacing = [&](float x) {
        float t = (x + radius) / (2 * radius);
        if (valid(s.gradientMap)) { auto c = sample(s.gradientMap, clamp01((cx + ca*x)/s.width), clamp01((cy + sa*x)/s.height)); t = c.a > 1e-6f ? clamp01((.2126f*c.r+.7152f*c.g+.0722f*c.b)/c.a) : 0; }
        else t = std::abs(end-start) < 1e-6f ? (t >= end ? 1.f : 0.f) : clamp01((t-start)/(end-start));
        return dense+(sparse-dense)*t;
    };
    // Transform the canvas into lattice coordinates and pad for rotated stamps.
    float minU=INFINITY,maxU=-INFINITY,minV=INFINITY,maxV=-INFINITY;
    for(float px : {0.f,float(s.width)}) for(float py : {0.f,float(s.height)}) {
        float dx=px-cx,dy=py-cy,u=ca*dx+sa*dy,v=-sa*dx+ca*dy;
        minU=std::min(minU,u); maxU=std::max(maxU,u); minV=std::min(minV,v); maxV=std::max(maxV,v);
    }
    const float pad=size*.7072f;
    minU-=pad; maxU+=pad; minV-=pad; maxV+=pad;
    const float rowSpacing = std::clamp(safe(s.rowSpacing,40), 2.f, 4096.f);
    constexpr int maxStamps=250000, maxSteps=1000000;
    const double firstRow=std::ceil((double(minV)+radius)/rowSpacing), lastRow=std::floor((double(maxV)+radius)/rowSpacing);
    if (!std::isfinite(firstRow) || !std::isfinite(lastRow) || firstRow < -maxSteps || lastRow > maxSteps ||
        (lastRow-firstRow+1)*(std::ceil((maxU-minU)/std::min(dense,sparse))+2)>maxStamps)
        throw std::length_error("pattern lattice exceeds candidate limit");
    std::vector<float> columns[2];
    for(int parity=0; parity<2; ++parity) {
        float anchor=-radius+(parity ? std::clamp(safe(s.stagger,0),-1.f,1.f)*spacing(-radius) : 0.f);
        int steps=0;
        for(float x=anchor; x<=maxU; x+=spacing(x)) {
            if(++steps>maxSteps) throw std::length_error("pattern lattice exceeds step limit");
            if(x>=minU) columns[parity].push_back(x);
        }
        std::vector<float> backwards;
        for(float x=anchor-spacing(anchor); x>=minU; x-=spacing(x)) {
            if(++steps>maxSteps) throw std::length_error("pattern lattice exceeds step limit");
            if(x<=maxU) backwards.push_back(x);
        }
        std::reverse(backwards.begin(),backwards.end());
        columns[parity].insert(columns[parity].begin(),backwards.begin(),backwards.end());
    }
    for(int row=int(firstRow); row<=int(lastRow); ++row) {
        float y=-radius+row*rowSpacing;
        for(float x : columns[(row%2+2)%2]) {
            float px=cx+ca*x-sa*y, py=cy+sa*x+ca*y;
            if(px>=-pad && px<=s.width+pad && py>=-pad && py<=s.height+pad) result.push_back({px,py,size});
        }
    }
    return result;
}
RenderPlan makePlan(const Settings& s) { return {s,layout(s)}; }
void renderBand(const RenderPlan& plan, float* out, int w, int h, int stride, float sx, float sy, int originY, RenderControl control) {
    if (!out || w <= 0 || h <= 0 || stride < w*4 || !std::isfinite(sx) || !std::isfinite(sy) || sx <= 0 || sy <= 0) return;
    if(control.checkCancel) control.checkCancel(control.user);
    for (int y=0; y<h; ++y) std::fill(out+y*stride, out+y*stride+w*4, 0.f);
    const auto& s=plan.settings;
    float a = safe(s.shapeRotation,0)*pi/180, ca=std::cos(a), sa=std::sin(a);
    Color fill{clamp01(safe(s.fillColor.r,0)),clamp01(safe(s.fillColor.g,0)),clamp01(safe(s.fillColor.b,0)),clamp01(safe(s.fillColor.a,0))};
    size_t stampIndex = 0;
    for (const auto& p : plan.stamps) {
        float rad = p.size*.5f, bound = rad*1.415f;
        int x0=std::max(0,int(std::floor((p.x-bound)*sx))), x1=std::min(w-1,int(std::ceil((p.x+bound)*sx)));
        int y0=std::max(0,int(std::floor((p.y-bound)*sy))-originY), y1=std::min(h-1,int(std::ceil((p.y+bound)*sy))-originY);
        if(x0>x1 || y0>y1) continue;
        if ((stampIndex++ % 16)==0 && control.checkCancel) control.checkCancel(control.user);
        for(int y=y0;y<=y1;++y) { if (((y-y0)%64)==0 && control.checkCancel) control.checkCancel(control.user); for(int x=x0;x<=x1;++x) {
            float accum[4]={};
            for(int j=0;j<2;++j) for(int i=0;i<2;++i) {
                float dx=(x+(i+.5f)/2)/sx-p.x, dy=(y+originY+(j+.5f)/2)/sy-p.y;
                float u=(ca*dx+sa*dy)/rad, v=(-sa*dx+ca*dy)/rad;
                Color c{}; c={0,0,0,0};
                if(s.shape==Shape::Custom) { if (valid(s.customStamp)) { float aspect=float(s.customStamp.width)/s.customStamp.height; float ux=u, vy=v; if(aspect>1) vy*=aspect; else ux/=aspect; c=sample(s.customStamp,(ux+1)*.5f,(vy+1)*.5f); c.r*=fill.r*fill.a; c.g*=fill.g*fill.a; c.b*=fill.b*fill.a; c.a*=fill.a; } }
                else { bool inside=s.shape==Shape::Circle ? u*u+v*v<=1 : s.shape==Shape::Diamond ? std::abs(u)+std::abs(v)<=1 : std::abs(u)<=1 && std::abs(v)<=1;
                    if(inside) c={fill.r*fill.a,fill.g*fill.a,fill.b*fill.a,fill.a}; }
                accum[0]+=c.r*.25f; accum[1]+=c.g*.25f; accum[2]+=c.b*.25f; accum[3]+=c.a*.25f;
            }
            float* dst=out+y*stride+x*4; float inv=1-clamp01(accum[3]);
            for(int k=0;k<4;++k) dst[k]=accum[k]+dst[k]*inv;
        } }
    }
}
void render(const Settings& s, float* out, int w, int h, int stride, float sx, float sy, RenderControl control) {
    renderBand(makePlan(s),out,w,h,stride,sx,sy,0,control);
}
}
