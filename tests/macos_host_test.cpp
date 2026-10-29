// Exercise the actual built bundle through SDK-shaped callbacks. This is not a real AE acceptance test.
#define PF_DEEP_COLOR_AWARE 1
#include "AEConfig.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "../src/PatternCore.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <future>
#include <vector>

using Entry=PF_Err(*)(PF_Cmd,PF_InData*,PF_OutData*,PF_ParamDef*[],PF_LayerDef*,void*);
struct Host {
    PF_LayerDef image{},map{};
    int checkouts=0,checkins=0,aborts=0,cancelAt=0;
};
static PF_Err checkout(PF_ProgPtr ref,PF_ParamIndex index,A_long,A_long,A_u_long,PF_ParamDef* out) {
    auto& h=*reinterpret_cast<Host*>(ref);out->u.ld=index==2?h.image:h.map;++h.checkouts;return PF_Err_NONE;
}
static PF_Err checkin(PF_ProgPtr ref,PF_ParamDef*) {++reinterpret_cast<Host*>(ref)->checkins;return PF_Err_NONE;}
static PF_Err abortRender(PF_ProgPtr ref) {auto& h=*reinterpret_cast<Host*>(ref);return (++h.aborts==h.cancelAt)?PF_Interrupt_CANCEL:PF_Err_NONE;}
static PF_Err pixel8(PF_EffectWorld* w,PF_PixelPtr,PF_Pixel8** out) {*out=PF_WORLD_IS_DEEP(w)?nullptr:reinterpret_cast<PF_Pixel8*>(w->data);return PF_Err_NONE;}
static PF_Err pixel16(PF_EffectWorld* w,PF_PixelPtr,PF_Pixel16** out) {*out=PF_WORLD_IS_DEEP(w)?reinterpret_cast<PF_Pixel16*>(w->data):nullptr;return PF_Err_NONE;}

template<class Pixel,int Max> std::vector<unsigned char> exercise(Entry entry,int shape,float scale,int cancelAt=0) {
    constexpr int width=96,height=72,iw=9,ih=7;
    const int w=int(width*scale),h=int(height*scale),padding=32;
    std::vector<Pixel> image(iw*ih),map(4);
    std::vector<float> rgba(iw*ih*4),mapRGBA(16);
    for(int y=0;y<ih;y++)for(int x=0;x<iw;x++) {
        auto& p=image[y*iw+x];bool inside=x>1 && x<7 && y>0 && y<6;
        p.alpha=inside?Max:0;p.red=inside?Max/2:0;p.green=inside?Max/3:0;p.blue=inside?Max/4:0;
        float* f=&rgba[(y*iw+x)*4];f[0]=p.red/float(Max);f[1]=p.green/float(Max);f[2]=p.blue/float(Max);f[3]=p.alpha/float(Max);
    }
    for(int i=0;i<4;i++) {auto& p=map[i];p.alpha=Max;p.red=p.green=p.blue=i*(Max/3);float* f=&mapRGBA[i*4];f[0]=f[1]=f[2]=p.red/float(Max);f[3]=1;}
    Host host;host.cancelAt=cancelAt;
    host.image.width=iw;host.image.height=ih;host.image.rowbytes=iw*sizeof(Pixel);host.image.data=reinterpret_cast<PF_PixelPtr>(image.data());
    host.map.width=host.map.height=2;host.map.rowbytes=2*sizeof(Pixel);host.map.data=reinterpret_cast<PF_PixelPtr>(map.data());
    if(sizeof(Pixel)==8)host.image.world_flags=host.map.world_flags=PF_WorldFlag_DEEP;
    PF_UtilCallbacks utils{};utils.get_pixel_data8=pixel8;utils.get_pixel_data16=pixel16;
    PF_InData in{};in.width=width;in.height=height;in.downsample_x.num=in.downsample_y.num=1;
    in.downsample_x.den=in.downsample_y.den=int(1/scale);in.time_scale=24;in.time_step=1;
    in.effect_ref=reinterpret_cast<PF_ProgPtr>(&host);in.utils=&utils;
    in.inter.checkout_param=checkout;in.inter.checkin_param=checkin;in.inter.abort=abortRender;
    PF_ParamDef params[18]{};PF_ParamDef* pointers[18];for(int i=0;i<18;i++)pointers[i]=&params[i];
    params[1].u.pd.value=shape;
    auto val=[&](int i,float v){params[i].u.fs_d.value=v;};
    val(4,8);val(5,21);val(6,9);val(7,21.7f);val(8,0);val(9,100);val(10,17);val(11,31);
    val(12,2.13f);val(13,-.37f);val(14,50);val(16,43);val(17,79);
    params[15].u.cd.value.red=173;params[15].u.cd.value.green=91;params[15].u.cd.value.blue=255;
    PF_LayerDef output{};output.width=w;output.height=h;output.rowbytes=w*sizeof(Pixel)+padding;
    if(sizeof(Pixel)==8)output.world_flags=PF_WorldFlag_DEEP;
    std::vector<unsigned char> bytes(size_t(output.rowbytes)*h,0xcd);output.data=reinterpret_cast<PF_PixelPtr>(bytes.data());
    PF_OutData out{};PF_Err err=entry(PF_Cmd_RENDER,&in,&out,pointers,&output,nullptr);
    assert(host.checkins==host.checkouts);
    if(cancelAt) {assert(err==PF_Interrupt_CANCEL);return {};}
    assert(err==PF_Err_NONE);
    pattern::Settings s;s.width=width;s.height=height;s.spacingDense=8;s.spacingSparse=21;s.rowSpacing=9;s.size=21.7f;
    s.angle=17;s.shapeRotation=31;s.offsetX=2.13f;s.offsetY=-.37f;s.stagger=.5f;s.gradientStrength=.79f;
    s.shape=static_cast<pattern::Shape>(shape-1);s.fillColor={173/255.f,91/255.f,1,.43f};
    s.customStamp={rgba.data(),iw,ih,iw*4};s.gradientMap={mapRGBA.data(),2,2,8};
    std::vector<float> reference(size_t(w)*h*4);pattern::render(s,reference.data(),w,h,w*4,scale,scale);
    for(int y=0;y<h;y++) {
        auto* row=reinterpret_cast<const Pixel*>(bytes.data()+size_t(y)*output.rowbytes);
        for(int x=0;x<w;x++) {
            const float* f=&reference[(size_t(y)*w+x)*4];int actual[]={int(row[x].red),int(row[x].green),int(row[x].blue),int(row[x].alpha)};
            for(int k=0;k<4;k++)assert(std::abs(actual[k]-int(std::clamp(f[k],0.f,1.f)*Max+.5f))<=1);
        }
        for(int p=w*sizeof(Pixel);p<output.rowbytes;p++)assert(bytes[size_t(y)*output.rowbytes+p]==0xcd);
    }
    return bytes;
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    void* module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!module){std::puts(dlerror());return 1;}
    auto entry=reinterpret_cast<Entry>(dlsym(module,"EffectMain"));assert(entry);
    PF_InData in{};PF_OutData out{};assert(entry(PF_Cmd_GLOBAL_SETUP,&in,&out,nullptr,nullptr,nullptr)==PF_Err_NONE);
    assert(out.my_version==PF_VERSION(2,0,0,PF_Stage_DEVELOP,0));
    for(int shape:{1,4})for(float scale:{1.f,.5f}) {
        exercise<PF_Pixel8,PF_MAX_CHAN8>(entry,shape,scale);
        exercise<PF_Pixel16,PF_MAX_CHAN16>(entry,shape,scale);
    }
    exercise<PF_Pixel16,PF_MAX_CHAN16>(entry,4,1,3);
    auto expected=exercise<PF_Pixel16,PF_MAX_CHAN16>(entry,4,1);
    std::vector<std::future<std::vector<unsigned char>>> jobs;
    for(int i=0;i<4;i++)jobs.push_back(std::async(std::launch::async,[entry]{return exercise<PF_Pixel16,PF_MAX_CHAN16>(entry,4,1);}));
    for(auto& job:jobs)assert(job.get()==expected);
    std::puts("Built bundle entry: 8/16 bpc, full/half resolution, padded rows, checkout cleanup, cancellation and 4 concurrent calls passed (simulated host).");
    // Keep the bundle loaded until process teardown so Objective-C/Metal completion handlers cannot outlive it.
}
