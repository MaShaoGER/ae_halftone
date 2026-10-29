#include <metal_stdlib>
using namespace metal;
#include "RasterShared.h"

float4 texel(device const float4* image, constant RasterUniforms& q, int x, int y) {
    x-=q.sourceX; y-=q.sourceY;
    if(x<0 || y<0 || x>=q.imageWidth || y>=q.imageHeight) return float4(0);
    return image[y*q.imageWidth+x];
}
float4 sourceSample(device const float4* image, constant RasterUniforms& q, float u, float v) {
    if(q.imageWidth==0 || u<0 || u>1 || v<0 || v>1) return float4(0);
    float x=u*(q.sourceWidth-1),y=v*(q.sourceHeight-1);
    int x0=int(x),y0=int(y),x1=min(x0+1,q.sourceWidth-1),y1=min(y0+1,q.sourceHeight-1);
    float tx=x-x0,ty=y-y0;
    return (texel(image,q,x0,y0)*(1-tx)+texel(image,q,x1,y0)*tx)*(1-ty)+
           (texel(image,q,x0,y1)*(1-tx)+texel(image,q,x1,y1)*tx)*ty;
}
kernel void orderedRaster(constant RasterUniforms& q [[buffer(0)]],
                          device const RasterStamp* stamps [[buffer(1)]],
                          device const uint* offsets [[buffer(2)]],
                          device const uint* indices [[buffer(3)]],
                          device const float4* image [[buffer(4)]],
                          device uchar* output [[buffer(5)]],
                          uint2 gid [[thread_position_in_grid]]) {
    int x=int(gid.x),y=int(gid.y);
    if(x>=q.width || y>=q.height) return;
    uint tile=(y/q.tileSize)*q.tileColumns+x/q.tileSize;
    float4 result=0;
    float remaining=1;
    for(uint n=offsets[tile+1];n>offsets[tile];) {
        RasterStamp p=stamps[indices[--n]];
        if(x<p.left || x>p.right || y<p.top || y>p.bottom) continue;
        float4 c=0;
        for(int j=0;j<2;j++) for(int i=0;i<2;i++) {
            float dx=(x+(i+.5f)*.5f)/q.scaleX-p.x,dy=(y+(j+.5f)*.5f)/q.scaleY-p.y;
            float u=(q.cosine*dx+q.sine*dy)*p.inverseRadius,v=(-q.sine*dx+q.cosine*dy)*p.inverseRadius;
            float4 a=0;
            if(q.shape==3) a=sourceSample(image,q,(u*q.aspectX+1)*.5f,(v*q.aspectY+1)*.5f);
            else {
                bool inside=q.shape==0 ? u*u+v*v<=1 : q.shape==2 ? abs(u)+abs(v)<=1 : abs(u)<=1 && abs(v)<=1;
                if(inside) a=float4(1);
            }
            c+=a*.25f;
        }
        c*=float4(q.red*q.alpha,q.green*q.alpha,q.blue*q.alpha,q.alpha);
        result+=remaining*c;
        remaining*=1-clamp(c.a,0.f,1.f);
        if(remaining==0) break;
    }
    device uchar* row=output+ulong(y)*q.outputRowBytes;
    if(q.outputFormat==0) reinterpret_cast<device float4*>(row)[x]=result;
    else {
        float4 argb=clamp(result.wxyz,0.f,1.f);
        if(q.outputFormat==1) reinterpret_cast<device uchar4*>(row)[x]=uchar4(argb*255.f+.5f);
        else reinterpret_cast<device ushort4*>(row)[x]=ushort4(argb*32768.f+.5f);
    }
}
