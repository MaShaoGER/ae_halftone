// macOS validation utility: compare decoded image pixels, ignoring PNG metadata.
#include <ImageIO/ImageIO.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CommonCrypto/CommonDigest.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

struct Pixels {
    std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> hash{};
    size_t width=0, height=0, depth=0, bytes=0;
};
Pixels decode(const std::filesystem::path& path) {
    const auto name=path.string();
    CFURLRef url=CFURLCreateFromFileSystemRepresentation(nullptr,
        reinterpret_cast<const UInt8*>(name.data()),name.size(),false);
    CGImageSourceRef source=CGImageSourceCreateWithURL(url,nullptr);
    CFRelease(url);
    if(!source) throw std::runtime_error("Cannot open " + name);
    CGImageRef image=CGImageSourceCreateImageAtIndex(source,0,nullptr);
    CFRelease(source);
    if(!image) throw std::runtime_error("Cannot decode " + name);
    CFDataRef data=CGDataProviderCopyData(CGImageGetDataProvider(image));
    if(!data) { CGImageRelease(image); throw std::runtime_error("Missing pixels " + name); }
    Pixels p; p.width=CGImageGetWidth(image); p.height=CGImageGetHeight(image);
    p.depth=CGImageGetBitsPerComponent(image); p.bytes=CFDataGetLength(data);
    CC_SHA256(CFDataGetBytePtr(data),static_cast<CC_LONG>(p.bytes),p.hash.data());
    CFRelease(data); CGImageRelease(image); return p;
}
int main(int argc,char** argv) {
    try {
        if(argc!=4) throw std::runtime_error("Usage: compare_render_frames OFF_DIR ON_DIR EXPECTED_FRAMES");
        std::vector<std::filesystem::path> left,right;
        for(auto& e:std::filesystem::directory_iterator(argv[1])) if(e.path().extension()==".png") left.push_back(e.path());
        for(auto& e:std::filesystem::directory_iterator(argv[2])) if(e.path().extension()==".png") right.push_back(e.path());
        std::sort(left.begin(),left.end()); std::sort(right.begin(),right.end());
        const size_t expected=std::stoul(argv[3]);
        if(left.size()!=expected || right.size()!=expected) throw std::runtime_error("Unexpected frame count");
        size_t bad=0,depth=0;
        for(size_t i=0;i<left.size();++i) {
            if(left[i].filename()!=right[i].filename()) throw std::runtime_error("Frame names differ");
            const auto a=decode(left[i]),b=decode(right[i]); depth=a.depth;
            if(a.width!=b.width || a.height!=b.height || a.depth!=b.depth || a.bytes!=b.bytes || a.hash!=b.hash) ++bad;
        }
        std::cout << "{\"frames\":" << left.size() << ",\"decodedBitDepth\":" << depth
                  << ",\"mismatchedFrames\":" << bad << "}\n";
        return bad ? 1:0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
