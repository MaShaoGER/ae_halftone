#include "RasterCore.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace pattern {
namespace {
constexpr float pi = 3.14159265358979323846f;
constexpr size_t maxIndexEntries = 32 * 1024 * 1024; // 128 MiB per frame, fail rather than omit candidates.
float finite(float v, float fallback = 0) { return std::isfinite(v) ? v : fallback; }
float unit(float v) { return std::clamp(finite(v), 0.f, 1.f); }
void cancel(RenderControl c) { if(c.checkCancel) c.checkCancel(c.user); }
bool valid(ImageView v) { return v.width > 0 && v.height > 0 &&
    (v.pixelAt || (v.pixels && v.rowFloats >= v.width * 4)); }
Color pixel(ImageView v, int x, int y) {
    if(v.pixelAt) return v.pixelAt(v.context,x,y);
    const float* p=v.pixels+size_t(y)*v.rowFloats+x*4;
    return {p[0],p[1],p[2],p[3]};
}
Color imageSample(const RasterFrame& f, float u, float v) {
    const auto& q=f.uniforms;
    if(q.imageWidth==0 || u<0 || u>1 || v<0 || v>1) return {0,0,0,0};
    const float xx=u*(q.sourceWidth-1), yy=v*(q.sourceHeight-1);
    const int x0=int(xx), y0=int(yy), x1=std::min(x0+1,q.sourceWidth-1), y1=std::min(y0+1,q.sourceHeight-1);
    auto at=[&](int x,int y,int channel) {
        x-=q.sourceX; y-=q.sourceY;
        if(x<0 || y<0 || x>=q.imageWidth || y>=q.imageHeight) return 0.f;
        return f.image[(size_t(y)*q.imageWidth+x)*4+channel];
    };
    const float tx=xx-x0,ty=yy-y0;
    float c[4];
    for(int k=0;k<4;k++) c[k]=(at(x0,y0,k)*(1-tx)+at(x1,y0,k)*tx)*(1-ty)+(at(x0,y1,k)*(1-tx)+at(x1,y1,k)*tx)*ty;
    return {c[0],c[1],c[2],c[3]};
}
}

RasterFrame prepareRaster(const Settings& s, int width, int height, float sx, float sy, RenderControl control) {
    if(width<=0 || height<=0 || !std::isfinite(sx) || !std::isfinite(sy) || sx<=0 || sy<=0)
        throw std::invalid_argument("invalid raster dimensions");
    RasterFrame f;
    auto& q=f.uniforms;
    q.width=width; q.height=height; q.rowPixels=width; q.tileSize=16; q.tileColumns=(width+15)/16;
    q.scaleX=sx; q.scaleY=sy; q.shape=int(s.shape);
    q.cosine=std::cos(finite(s.shapeRotation)*pi/180); q.sine=std::sin(finite(s.shapeRotation)*pi/180);
    q.red=unit(s.fillColor.r); q.green=unit(s.fillColor.g); q.blue=unit(s.fillColor.b); q.alpha=unit(s.fillColor.a);
    q.aspectX=q.aspectY=1;
    float minU=-1,maxU=1,minV=-1,maxV=1;
    bool empty=q.alpha==0 || finite(s.size)<=0;
    cancel(control);
    if(s.shape==Shape::Custom) {
        const auto v=s.customStamp;
        empty=empty || !valid(v);
        if(!empty) {
            q.sourceWidth=v.width; q.sourceHeight=v.height;
            const float longest=float(std::max(v.width,v.height));
            q.aspectX=longest/v.width; q.aspectY=longest/v.height;
            int left=v.width,top=v.height,right=-1,bottom=-1;
            // Convert each source pixel once; RGBA (not just alpha) defines empty regions.
            std::vector<float> pixels(size_t(v.width)*v.height*4);
            for(int y=0;y<v.height;y++) {
                if((y%32)==0) cancel(control);
                for(int x=0;x<v.width;x++) {
                    Color c=pixel(v,x,y); float* p=&pixels[(size_t(y)*v.width+x)*4];
                    p[0]=finite(c.r); p[1]=finite(c.g); p[2]=finite(c.b); p[3]=unit(c.a);
                    if(p[0]!=0 || p[1]!=0 || p[2]!=0 || p[3]!=0) {
                        left=std::min(left,x); right=std::max(right,x); top=std::min(top,y); bottom=std::max(bottom,y);
                    }
                }
            }
            empty=right<left;
            if(!empty) {
                q.sourceX=left; q.sourceY=top; q.imageWidth=right-left+1; q.imageHeight=bottom-top+1;
                f.image.resize(size_t(q.imageWidth)*q.imageHeight*4);
                for(int y=0;y<q.imageHeight;y++) std::copy_n(pixels.data()+(size_t(top+y)*v.width+left)*4,
                    size_t(q.imageWidth)*4,f.image.data()+size_t(y)*q.imageWidth*4);
                auto low=[](int lo,int n) { return n>1 ? 2*float(std::max(0,lo-1))/(n-1)-1 : -1.f; };
                auto high=[](int hi,int n) { return n>1 ? 2*float(std::min(n-1,hi+1))/(n-1)-1 : 1.f; };
                minU=low(left,v.width)/q.aspectX; maxU=high(right,v.width)/q.aspectX;
                minV=low(top,v.height)/q.aspectY; maxV=high(bottom,v.height)/q.aspectY;
            }
        }
    }
    const size_t tiles=size_t(q.tileColumns)*((height+15)/16);
    f.tileOffsets.assign(tiles+1,0);
    if(empty) return f;
    const auto stamps=layout(s);
    float localLeft=INFINITY,localRight=-INFINITY,localTop=INFINITY,localBottom=-INFINITY;
    for(float u:{minU,maxU}) for(float v:{minV,maxV}) {
        const float x=q.cosine*u-q.sine*v,y=q.sine*u+q.cosine*v;
        localLeft=std::min(localLeft,x); localRight=std::max(localRight,x);
        localTop=std::min(localTop,y); localBottom=std::max(localBottom,y);
    }
    // Rotation-independent tight bounds for a circle.
    if(s.shape==Shape::Circle) { localLeft=localTop=-1; localRight=localBottom=1; }
    size_t entries=0;
    for(const auto& p:stamps) {
        if((f.stamps.size()%256)==0) cancel(control);
        const float r=p.size*.5f;
        // One-pixel conservative guard also covers floating-point edge rounding.
        int x0=std::max(0,int(std::floor((p.x+r*localLeft)*sx))-1),x1=std::min(width-1,int(std::ceil((p.x+r*localRight)*sx))+1);
        int y0=std::max(0,int(std::floor((p.y+r*localTop)*sy))-1),y1=std::min(height-1,int(std::ceil((p.y+r*localBottom)*sy))+1);
        if(x0>x1 || y0>y1) continue;
        const size_t count=size_t(x1/16-x0/16+1)*(y1/16-y0/16+1);
        if(count>maxIndexEntries-entries) throw std::length_error("raster index exceeds memory budget");
        entries+=count;
        f.stamps.push_back({p.x,p.y,1/r,0,x0,y0,x1,y1});
        for(int ty=y0/16;ty<=y1/16;ty++) for(int tx=x0/16;tx<=x1/16;tx++) ++f.tileOffsets[size_t(ty)*q.tileColumns+tx+1];
    }
    for(size_t i=1;i<f.tileOffsets.size();i++) f.tileOffsets[i]+=f.tileOffsets[i-1];
    f.tileIndices.resize(entries);
    auto cursor=f.tileOffsets;
    for(uint32_t i=0;i<f.stamps.size();i++) {
        const auto& p=f.stamps[i];
        for(int ty=p.top/16;ty<=p.bottom/16;ty++) for(int tx=p.left/16;tx<=p.right/16;tx++)
            f.tileIndices[cursor[size_t(ty)*q.tileColumns+tx]++]=i;
    }
    cancel(control);
    return f;
}

void renderRasterCPU(const RasterFrame& f, float* out, int stride, int originY, int height, RenderControl control) {
    const auto& q=f.uniforms;
    if(!out || stride<q.width*4 || originY<0 || height<0 || originY>q.height-height)
        throw std::invalid_argument("invalid raster output");
    for(int y=originY;y<originY+height;y++) {
        if((y%8)==0) cancel(control);
        for(int x=0;x<q.width;x++) {
            const size_t tile=size_t(y/16)*q.tileColumns+x/16;
            float rr=0,gg=0,bb=0,aa=0,t=1;
            for(uint32_t n=f.tileOffsets[tile+1];n>f.tileOffsets[tile];) {
                const auto& p=f.stamps[f.tileIndices[--n]];
                if(x<p.left || x>p.right || y<p.top || y>p.bottom) continue;
                Color c{0,0,0,0};
                for(int j=0;j<2;j++) for(int i=0;i<2;i++) {
                    const float dx=(x+(i+.5f)*.5f)/q.scaleX-p.x,dy=(y+(j+.5f)*.5f)/q.scaleY-p.y;
                    const float u=(q.cosine*dx+q.sine*dy)*p.inverseRadius,v=(-q.sine*dx+q.cosine*dy)*p.inverseRadius;
                    Color a{0,0,0,0};
                    if(q.shape==int(Shape::Custom)) a=imageSample(f,(u*q.aspectX+1)*.5f,(v*q.aspectY+1)*.5f);
                    else {
                        const bool inside=q.shape==int(Shape::Circle) ? u*u+v*v<=1 :
                            q.shape==int(Shape::Diamond) ? std::abs(u)+std::abs(v)<=1 : std::abs(u)<=1 && std::abs(v)<=1;
                        if(inside) a={1,1,1,1};
                    }
                    c.r+=a.r*.25f; c.g+=a.g*.25f; c.b+=a.b*.25f; c.a+=a.a*.25f;
                }
                const float alpha=c.a*q.alpha;
                rr+=t*c.r*q.red*q.alpha; gg+=t*c.g*q.green*q.alpha; bb+=t*c.b*q.blue*q.alpha; aa+=t*alpha;
                t*=1-std::clamp(alpha,0.f,1.f);
                if(t==0) break; // Exact opacity only; never discard a long translucent tail.
            }
            float* dst=out+size_t(y-originY)*stride+x*4;
            dst[0]=rr; dst[1]=gg; dst[2]=bb; dst[3]=aa;
        }
    }
    cancel(control);
}
}
