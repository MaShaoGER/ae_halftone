#define PF_DEEP_COLOR_AWARE 1
#include "AEConfig.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "Param_Utils.h"
#include "PatternCore.h"
#include "RasterCore.h"
#include "PluginConfig.h"
#include "RenderTrace.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <new>
#include <stdexcept>
#include <vector>

namespace {
// Shared parameter definitions for the Mac and future Windows host adapters.
enum { INPUT, SHAPE, CUSTOM_LAYER, MAP_LAYER, DENSE, SPARSE, ROW, SIZE, START, END, ANGLE, ROTATION, OFFSET_X, OFFSET_Y, STAGGER, COLOR, OPACITY, GRADIENT_STRENGTH, COUNT };
static_assert(PATTERN_VERSION == PF_VERSION(2,0,0,PF_Stage_DEVELOP,0));
static_assert(PATTERN_OUT_FLAGS == PF_OutFlag_DEEP_COLOR_AWARE);
static_assert(PATTERN_OUT_FLAGS2 == (PF_OutFlag2_REVEALS_ZERO_ALPHA | PF_OutFlag2_SUPPORTS_THREADED_RENDERING));

PF_Err setup(PF_InData* in_data, PF_OutData* out_data) {
    PF_Err err=PF_Err_NONE; PF_ParamDef def;
#define CLEAR() std::memset(&def, 0, sizeof(def))
#define SLIDER(name, lo, hi, defval, id) do { CLEAR(); PF_ADD_FLOAT_SLIDERX(name, lo, hi, lo, hi, defval, PF_Precision_TENTHS, 0, 0, id); } while(0)
    CLEAR(); PF_ADD_POPUP("图形",4,1,"圆形|方形|菱形|自定义",SHAPE);
    CLEAR(); PF_ADD_LAYER("自定义图形图层",PF_LayerDefault_NONE,CUSTOM_LAYER);
    CLEAR(); PF_ADD_LAYER("渐变轮廓图层",PF_LayerDefault_NONE,MAP_LAYER);
    SLIDER("密集端间距",2,4096,20,DENSE); SLIDER("稀疏端间距",2,4096,60,SPARSE);
    SLIDER("行间距",2,4096,40,ROW); SLIDER("图形大小",0,4096,10,SIZE);
    SLIDER("渐变起点 (%)",0,100,0,START); SLIDER("渐变终点 (%)",0,100,100,END);
    SLIDER("排列角度",-360,360,0,ANGLE); SLIDER("图形旋转",-360,360,0,ROTATION);
    SLIDER("水平偏移",-16384,16384,0,OFFSET_X); SLIDER("垂直偏移",-16384,16384,0,OFFSET_Y);
    SLIDER("交错程度 (%)",0,100,0,STAGGER);
    CLEAR(); PF_ADD_COLOR("图形颜色",255,255,255,COLOR);
    SLIDER("不透明度 (%)",0,100,100,OPACITY);
    SLIDER("渐变读取强度 (%)",0,100,100,GRADIENT_STRENGTH);
    out_data->num_params=COUNT;
    return err;
#undef SLIDER
#undef CLEAR
}
struct Checkout {
    PF_InData* in_data; PF_ParamDef value{}; bool checked=false;
    explicit Checkout(PF_InData* p):in_data(p) {}
    PF_Err get(int index) { PF_Err e=PF_CHECKOUT_PARAM(in_data,index,in_data->current_time,in_data->time_step,in_data->time_scale,&value); checked=!e; return e; }
    ~Checkout() { if(checked) PF_CHECKIN_PARAM(in_data,&value); }
};
struct PixelSource { const void* base=nullptr; A_long rowbytes=0; };
template<class P, int Max> pattern::Color nativePixel(const void* context, int x, int y) {
    const auto& source=*static_cast<const PixelSource*>(context);
    const auto* row=reinterpret_cast<const P*>(static_cast<const char*>(source.base)+std::ptrdiff_t(y)*source.rowbytes);
    const auto& p=row[x];
    return {p.red/float(Max),p.green/float(Max),p.blue/float(Max),p.alpha/float(Max)};
}
PF_Err readLayer(PF_InData* in_data, PF_LayerDef& w, PixelSource& storage, pattern::ImageView& view) {
    if(!w.data || w.width<=0 || w.height<=0) return PF_Err_NONE;
    storage.rowbytes=w.rowbytes;
    view.width=w.width; view.height=w.height; view.context=&storage;
    if(PF_WORLD_IS_DEEP(&w)) { PF_Pixel16* p=nullptr; PF_Err e=PF_GET_PIXEL_DATA16(&w,nullptr,&p); if(e) return e; if(!p) return PF_Err_INTERNAL_STRUCT_DAMAGED; storage.base=p; view.pixelAt=nativePixel<PF_Pixel16,PF_MAX_CHAN16>; }
    else { PF_Pixel8* p=nullptr; PF_Err e=PF_GET_PIXEL_DATA8(&w,nullptr,&p); if(e) return e; if(!p) return PF_Err_INTERNAL_STRUCT_DAMAGED; storage.base=p; view.pixelAt=nativePixel<PF_Pixel8,PF_MAX_CHAN8>; }
    return PF_Err_NONE;
}
template<class P> void copyOut(P* base, PF_LayerDef* world, float max, const std::vector<float>& image, int originY, int height) {
    for(int y=0;y<height;++y) { auto row=reinterpret_cast<P*>(reinterpret_cast<char*>(base)+std::ptrdiff_t(originY+y)*world->rowbytes);
        for(int x=0;x<world->width;++x) { auto& p=row[x]; const float* s=&image[(size_t(y)*world->width+x)*4]; auto v=[&](int c){return std::clamp(s[c],0.f,1.f)*max+.5f;}; p.red=decltype(p.red)(v(0)); p.green=decltype(p.green)(v(1)); p.blue=decltype(p.blue)(v(2)); p.alpha=decltype(p.alpha)(v(3)); } }
}
PF_Err render(PF_InData* in_data, PF_ParamDef* params[], PF_LayerDef* output) {
    RenderTrace trace(in_data);
    PF_Err err=PF_ABORT(in_data); if(err) return err;
    Checkout custom(in_data), map(in_data);
    pattern::Settings s; s.width=in_data->width; s.height=in_data->height;
    s.shape=static_cast<pattern::Shape>(std::clamp(int(params[SHAPE]->u.pd.value)-1,0,3));
    auto val=[&](int i){return float(params[i]->u.fs_d.value);};
    s.spacingDense=val(DENSE); s.spacingSparse=val(SPARSE); s.rowSpacing=val(ROW); s.size=val(SIZE);
    s.gradientStart=val(START)/100; s.gradientEnd=val(END)/100; s.angle=val(ANGLE); s.shapeRotation=val(ROTATION);
    s.gradientStrength=val(GRADIENT_STRENGTH)/100;
    s.offsetX=val(OFFSET_X); s.offsetY=val(OFFSET_Y); s.stagger=val(STAGGER)/100;
    auto c=params[COLOR]->u.cd.value; s.fillColor={c.red/255.f,c.green/255.f,c.blue/255.f,val(OPACITY)/100};
    PixelSource customSource, mapSource;
    if(s.shape==pattern::Shape::Custom) {
        if((err=custom.get(CUSTOM_LAYER))) return err;
        if((err=readLayer(in_data,custom.value.u.ld,customSource,s.customStamp))) return err;
    }
    if(s.gradientStrength!=0) {
        if((err=map.get(MAP_LAYER))) return err;
        if((err=readLayer(in_data,map.value.u.ld,mapSource,s.gradientMap))) return err;
    }
    if(output->width<=0 || output->height<=0) return PF_Err_NONE;
    const float sx=float(in_data->downsample_x.num)/in_data->downsample_x.den, sy=float(in_data->downsample_y.num)/in_data->downsample_y.den;
    pattern::RenderControl control{in_data, [](void* user) { auto* in_data=static_cast<PF_InData*>(user); PF_Err error=PF_ABORT(in_data); if(error) throw error; }};
    const auto frame=pattern::prepareRaster(s,output->width,output->height,sx,sy,control);
    PF_Pixel16* deep=nullptr; PF_Pixel8* shallow=nullptr;
    if(PF_WORLD_IS_DEEP(output)) { if((err=PF_GET_PIXEL_DATA16(output,nullptr,&deep))) return err; if(!deep) return PF_Err_INTERNAL_STRUCT_DAMAGED; }
    else { if((err=PF_GET_PIXEL_DATA8(output,nullptr,&shallow))) return err; if(!shallow) return PF_Err_INTERNAL_STRUCT_DAMAGED; }
    // Prefer the tested Metal path. CPU fallback uses the same ordered tile index and cropped material.
    pattern::GPUStats gpuStats;
    if(pattern::renderRasterGPUBytes(frame,deep?static_cast<void*>(deep):static_cast<void*>(shallow),output->rowbytes,
        deep?pattern::RasterOutput::ARGB16:pattern::RasterOutput::ARGB8,control,&gpuStats)) {
        trace.recordGPU(gpuStats.milliseconds);
        return PF_Err_NONE;
    }
    constexpr int bandRows=64;
    std::vector<float> image(size_t(output->width)*std::min<int>(bandRows,output->height)*4);
    for(int y=0;y<output->height;y+=bandRows) {
        const int height=std::min<int>(bandRows,output->height-y);
        pattern::renderRasterCPU(frame,image.data(),output->width*4,y,height,control);
        if(deep) copyOut(deep,output,PF_MAX_CHAN16,image,y,height);
        else copyOut(shallow,output,PF_MAX_CHAN8,image,y,height);
    }
    return PF_Err_NONE;
}
}
#ifdef _WIN32
#define PATTERN_EXPORT __declspec(dllexport)
#else
#define PATTERN_EXPORT __attribute__((visibility("default")))
#endif
extern "C" PATTERN_EXPORT PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output, void*) {
    try {
        switch(cmd) {
        case PF_Cmd_ABOUT: std::snprintf(out_data->return_msg,sizeof(out_data->return_msg),"有序图案 2.0 开发版\r分块有序合成、素材裁剪与 Metal 计算加速；8/16 bpc。"); break;
        case PF_Cmd_GLOBAL_SETUP: out_data->my_version=PATTERN_VERSION; out_data->out_flags=PATTERN_OUT_FLAGS; out_data->out_flags2=PATTERN_OUT_FLAGS2; break;
        case PF_Cmd_PARAMS_SETUP: return setup(in_data,out_data);
        case PF_Cmd_RENDER: return render(in_data,params,output);
        default: break;
        }
        return PF_Err_NONE;
    } catch(PF_Err error) { return error; }
      catch(const std::bad_alloc&) { return PF_Err_OUT_OF_MEMORY; }
      catch(const std::length_error&) { return PF_Err_OUT_OF_MEMORY; }
      catch(...) { return PF_Err_INTERNAL_STRUCT_DAMAGED; }
}
