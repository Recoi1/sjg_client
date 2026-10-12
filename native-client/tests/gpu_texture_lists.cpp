// Keep the original Wii SDK types separate from Aurora's native internal headers.
#include "JSystem/J3DGraphBase/J3DGD.hpp"
#include "JSystem/J3DGraphBase/J3DFifo.hpp"
#include <revolution/mtx.h>
#include <revolution/gx/GXRegs.h>
#include <cassert>
extern "C" void verifyGalaxyGpuStorage(u32 textureSize,u32 paletteSize,u32 textureAlignment,u32 paletteAlignment) {
    assert(textureSize == sizeof(GXTexObj) && paletteSize == sizeof(GXTlutObj));
    assert(textureAlignment == alignof(GXTexObj) && paletteAlignment == alignof(GXTlutObj));
    struct { GXTexObj object{}; u64 canary = 0xabcdef1234567890; } texture;
    struct { GXTlutObj object{}; u64 canary = 0xabcdef1234567890; } palette;
    GXInitTexObj(&texture.object,nullptr,4,4,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);
    GXInitTlutObj(&palette.object,nullptr,GX_TL_RGB565,16);
    assert(texture.canary == 0xabcdef1234567890 && palette.canary == 0xabcdef1234567890);
    void* data = nullptr;
    u16 width = 0, height = 0;
    GXTexFmt format;
    GXTexWrapMode wrapS, wrapT;
    GXBool mipmap;
    GXGetTexObjAll(&texture.object, &data, &width, &height, &format, &wrapS, &wrapT, &mipmap);
    assert(data == nullptr && width == 4 && height == 4 && format == GX_TF_RGBA8);
    assert(wrapS == GX_CLAMP && wrapT == GX_CLAMP && !mipmap);
    for (u32 filter = 0; filter < 6; ++filter) {
        GXInitTexObjLOD(&texture.object, static_cast<GXTexFilter>(filter), GX_LINEAR,
                       1.5f, 4.25f, -1.5f, GX_TRUE, GX_FALSE, GX_ANISO_4);
        GXTexFilter min, mag;
        f32 minLod, maxLod, bias;
        GXBool clamp, edge;
        GXAnisotropy anisotropy;
        GXGetTexObjLODAll(&texture.object, &min, &mag, &minLod, &maxLod, &bias, &clamp, &edge, &anisotropy);
        assert(min == filter && mag == GX_LINEAR && minLod == 1.5f && maxLod == 4.25f);
        assert(bias == -1.5f && clamp && !edge && anisotropy == GX_ANISO_4);
    }
}
extern "C" void writeGalaxyMatrices() {
    Mtx matrix = {{1,2,3,4},{5,6,7,8},{9,10,11,12}};
    J3DFifoLoadPosMtxImm(matrix,0);
    GX_WRITE_U8(0x10);
    GX_WRITE_U32((11u << 16) | 12u);
    for (const auto& row : matrix) for (const auto value : row) GX_WRITE_F32(value);
}
extern "C" void writeGalaxyIndexedMatrices() {
    static Mtx matrices[3] = {
        {{1,0,0,0},{0,1,0,0},{0,0,1,0}},
        {{10,20,30,40},{50,60,70,80},{90,100,110,120}},
        {{2,4,6,8},{10,12,14,16},{18,20,22,24}}
    };
    GXSetArraySized(GX_TEX_MTX_ARRAY, matrices, sizeof(matrices), sizeof(Mtx));
    GXLoadTexMtxIndx(1, GX_TEXMTX0, GX_MTX3x4);
    GXLoadTexMtxIndx(2, GX_TEXMTX1, GX_MTX2x4);
    GXLoadTexMtxIndx(2, GX_PTTEXMTX0, GX_MTX3x4);
}
extern "C" void writeGalaxyImagePointer(void* image) {
    J3DGDSetTexImgPtr(GX_TEXMAP0,image);
}
extern "C" void writeGalaxyRgbaAttributes() {
    J3DGDSetTexImgAttr(GX_TEXMAP0,4,4,GX_TF_RGBA8);
    J3DGDSetTexLookupMode(GX_TEXMAP0,GX_CLAMP,GX_CLAMP,GX_LINEAR,GX_LINEAR,0,0,0,0,1,GX_ANISO_1);
}
extern "C" void writeGalaxyPaletteList(void* image,void* palette) {
    J3DGDSetTexImgPtr(GX_TEXMAP7,image);
    J3DGDSetTexImgAttr(GX_TEXMAP7,8,8,static_cast<GXTexFmt>(GX_TF_C4));
    J3DGDSetTexLookupMode(GX_TEXMAP7,GX_CLAMP,GX_CLAMP,GX_NEAR,GX_NEAR,0,0,0,0,1,GX_ANISO_1);
    J3DGDLoadTlut(palette,0xfe000,GX_TLUT_16);
    J3DGDSetTexTlut(GX_TEXMAP7,0xfe000,GX_TL_RGB565);
}
extern "C" void writeGalaxyMipList(void* image) {
    J3DGDSetTexImgPtr(GX_TEXMAP7,image);
    J3DGDSetTexLookupMode(GX_TEXMAP7,GX_CLAMP,GX_CLAMP,GX_LIN_MIP_LIN,GX_LINEAR,0,2,0,0,1,GX_ANISO_1);
}
