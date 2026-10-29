#include "../src/PatternCore.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <vector>

int main() {
    constexpr int w=360, h=240, gap=12, total=w*3+gap*2;
    std::vector<float> image(w*h*4), star(48*32*4,0);
    // A five-point star in a wide source image makes aspect preservation visible.
    constexpr float pi=3.14159265358979323846f;
    float vx[10],vy[10];
    for(int i=0;i<10;++i) { float a=-pi/2+i*pi/5, r=i%2?6.f:14.f; vx[i]=24+std::cos(a)*r; vy[i]=16+std::sin(a)*r; }
    for(int y=0;y<32;++y) for(int x=0;x<48;++x) {
        bool inside=false;
        for(int i=0,j=9;i<10;j=i++) if(((vy[i]>y+.5f)!=(vy[j]>y+.5f)) && x+.5f<(vx[j]-vx[i])*(y+.5f-vy[i])/(vy[j]-vy[i])+vx[i]) inside=!inside;
        if(inside) for(int c=0;c<4;++c) star[(y*48+x)*4+c]=1;
    }
    std::ofstream out("build/pattern-preview.ppm",std::ios::binary);
    if(!out) return 1;
    out << "P6\n" << total << ' ' << h << "\n255\n";
    std::vector<unsigned char> rgb(total*h*3,0);
    for(int panel=0;panel<3;++panel) {
        pattern::Settings s; s.width=w; s.height=h; s.spacingDense=18; s.spacingSparse=48; s.rowSpacing=30; s.size=12;
        s.fillColor={.35f,.8f,1.f,.95f};
        if(panel==1) { s.shape=pattern::Shape::Diamond; s.angle=20; s.shapeRotation=20; s.stagger=.5f; s.fillColor={.75f,.55f,1.f,.95f}; }
        if(panel==2) { s.shape=pattern::Shape::Custom; s.customStamp={star.data(),48,32,48*4}; s.size=23; s.rowSpacing=34; s.fillColor={1.f,.75f,.35f,.95f}; }
        pattern::render(s,image.data(),w,h,w*4);
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            const float* p=&image[(y*w+x)*4]; float bg=((x/12+y/12)%2 ? .085f : .065f);
            for(int c=0;c<3;++c) rgb[(y*total+panel*(w+gap)+x)*3+c]=static_cast<unsigned char>(std::clamp(p[c]+bg*(1-p[3]),0.f,1.f)*255+.5f);
        }
    }
    out.write(reinterpret_cast<const char*>(rgb.data()),rgb.size());
    return out ? 0 : 1;
}
