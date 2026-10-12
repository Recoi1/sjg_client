#include <dolphin/gd.h>
#include "crash.hpp"
#include <aurora/aurora.h>
#include <aurora/main.h>
#include <dolphin/gx/GXAurora.h>
#include <gx/gx.hpp>
#include <gx/texture.hpp>
#include <gfx/depth_peek.hpp>
#include <gfx/recording.hpp>
#include <gfx/render_worker.hpp>
#include <gfx/texture.hpp>
#include <webgpu/gpu.hpp>
#include <gx/pipeline.hpp>
#include <aurora/webgpu.hpp>
#include <aurora/gfx.h>
#include <dolphin/gx/__gx.h>
#include <SDL3/SDL.h>
#include <array>
#include <cassert>
#include <cstdint>
#include <atomic>
#include <thread>
#include <aurora/texture.hpp>

extern "C" void writeGalaxyImagePointer(void*);
extern "C" void writeGalaxyRgbaAttributes();
extern "C" void writeGalaxyPaletteList(void*, void*);
extern "C" void writeGalaxyMipList(void*);
extern "C" void writeGalaxyMatrices();
extern "C" void writeGalaxyIndexedMatrices();
extern "C" void verifyGalaxyGpuStorage(u32,u32,u32,u32);
extern "C" void verifyGalaxyText();

int main(int argc, char** argv) {
    galaxy::installCrashHandler();
    AuroraConfig config{};
    config.appName = "Galaxy texture check";
    config.msaa = argc>1 ? 4 : 1;
    config.desiredBackend = argc>2 ? BACKEND_VULKAN : BACKEND_D3D12;
    config.userPath = config.cachePath = "./";
    const auto info = aurora_initialize(argc, argv, &config);
    SDL_SetWindowOpacity(info.window, 0.0f);
    std::array modes{wgpu::PresentMode::Fifo,wgpu::PresentMode::Mailbox,wgpu::PresentMode::Immediate};
    WGPUSurfaceCapabilities raw{}; raw.presentModeCount=modes.size(); raw.presentModes=reinterpret_cast<WGPUPresentMode*>(modes.data());
    const auto& capabilities=reinterpret_cast<const wgpu::SurfaceCapabilities&>(raw);
    aurora_enable_vsync(false); assert(aurora::webgpu::select_present_mode(capabilities)==wgpu::PresentMode::Immediate);
    raw.presentModeCount=2; assert(aurora::webgpu::select_present_mode(capabilities)==wgpu::PresentMode::Mailbox);
    raw.presentModeCount=1; assert(aurora::webgpu::select_present_mode(capabilities)==wgpu::PresentMode::Fifo);
    aurora_enable_vsync(true); raw.presentModeCount=3; assert(aurora::webgpu::select_present_mode(capabilities)==wgpu::PresentMode::Fifo);
    aurora_enable_vsync(false);
    GXInit(nullptr, 0);
    const aurora::gfx::TextureBind sample;
    for(unsigned mode=1;mode<=3;++mode) {
        AuroraSetTextureFiltering(mode); const auto descriptor=sample.get_descriptor();
        assert(descriptor.minFilter==(mode==1 ? wgpu::FilterMode::Nearest : wgpu::FilterMode::Linear));
        assert(descriptor.magFilter==descriptor.minFilter);
        assert(descriptor.mipmapFilter==(mode==3 ? wgpu::MipmapFilterMode::Linear : wgpu::MipmapFilterMode::Nearest));
        assert(descriptor.maxAnisotropy==1 && descriptor.lodMaxClamp==0);
    }
    AuroraSetTextureFiltering(0);
    GXTexRegion region{};
    GXInitTexCacheRegion(&region, GX_TRUE, 0x80000, GX_TEXCACHE_32K, 0, GX_TEXCACHE_128K);
    std::array<u32, 4> regionWords{};
    std::memcpy(regionWords.data(), &region, sizeof(region));
    assert(regionWords[0] == (0x4000 | 3 << 15 | 3 << 18));
    assert(regionWords[1] == (4 << 15 | 4 << 18));
    assert(reinterpret_cast<const u8*>(&region)[12] == 1 && reinterpret_cast<const u8*>(&region)[13] == 1);
    GXInitTexCacheRegion(&region, GX_FALSE, 0, GX_TEXCACHE_512K, 0, GX_TEXCACHE_NONE);
    std::memcpy(regionWords.data(), &region, sizeof(region));
    assert(regionWords[0] == (5 << 15 | 5 << 18) && regionWords[1] == 0);
    std::array<u8,64> preloadSource{}; preloadSource.fill(0x71);
    const auto originalPreload=preloadSource;
    const aurora::texture::RawTextureReplacement preload{.bytes=preloadSource,.width=4,.height=4,.mipCount=1,.gxFormat=GX_TF_RGBA8};
    auto unsupported=preload; unsupported.gxFormat=GX_TF_C4;
    assert(!aurora::texture::queue_texture_preload(unsupported));
    auto tooManyMips=preload; tooManyMips.mipCount=12;
    assert(!aurora::texture::queue_texture_preload(tooManyMips));
    tooManyMips.mipCount=4;
    assert(!aurora::texture::queue_texture_preload(tooManyMips));
    auto truncated=preload; truncated.bytes=std::span<const u8>(preloadSource).first(63);
    assert(!aurora::texture::queue_texture_preload(truncated));
    std::thread producer([&]{assert(aurora::texture::queue_texture_preload(preload));assert(aurora::texture::queue_texture_preload(preload));});
    producer.join(); assert(aurora::texture::texture_preloads_pending());
    preloadSource.fill(0);
    for(unsigned i=0;aurora::texture::texture_preloads_pending() && i<30;++i) {
        assert(aurora_begin_frame()); aurora_end_frame();
    }
    assert(!aurora::texture::texture_preloads_pending());
    assert(aurora::gx::texture_stats().contentCacheEntries==1);
    assert(aurora_begin_frame());
    GXTexObj_ preloaded{}; preloaded.data=originalPreload.data(); preloaded.mWidth=preloaded.mHeight=4; preloaded.mFormat=GX_TF_RGBA8;
    const auto misses=aurora::gx::texture_stats().misses;
    assert(aurora::gx::texture::resolve_static_texture(preloaded));
    assert(aurora::gx::texture_stats().misses==misses && aurora::gx::texture_stats().contentHits==1);
    assert(aurora::texture::queue_texture_preload(preload));
    aurora::gx::texture::shutdown();
    assert(!aurora::texture::texture_preloads_pending() && !aurora::gx::texture_stats().contentCacheEntries);
    std::array<u8,96> mipPreload{}; mipPreload.fill(0x55);
    assert(aurora::texture::queue_texture_preload({.bytes=mipPreload,.width=8,.height=8,.mipCount=3,.gxFormat=GX_TF_CMPR}));
    aurora::gx::texture::preload_frame();
    GXTexObj_ mipObject{}; mipObject.data=mipPreload.data(); mipObject.mWidth=mipObject.mHeight=8;
    mipObject.mFormat=GX_TF_CMPR; mipObject.flags=1; mipObject.mode1=32<<8;
    const auto mipMisses=aurora::gx::texture_stats().misses;
    assert(aurora::gx::texture::resolve_static_texture(mipObject)->mipCount==3);
    assert(aurora::gx::texture_stats().misses==mipMisses);


    verifyGalaxyGpuStorage(sizeof(GXTexObj),sizeof(GXTlutObj),alignof(GXTexObj),alignof(GXTlutObj));
    writeGalaxyMatrices();
    GXDrawDone();
    const aurora::Mat3x4<float> expected{{1,2,3,4},{5,6,7,8},{9,10,11,12}};
    assert(aurora::gx::g_gxState.pnMtx[0].pos == expected && aurora::gx::g_gxState.pnMtx[1].pos == expected);
    writeGalaxyIndexedMatrices();
    GXDrawDone();
    const aurora::Mat3x4<float> textureMatrix{{10,20,30,40},{50,60,70,80},{90,100,110,120}};
    const aurora::Mat3x4<float> postMatrix{{2,4,6,8},{10,12,14,16},{18,20,22,24}};
    assert(aurora::gx::g_gxState.texMtxs[0] == textureMatrix);
    assert(aurora::gx::g_gxState.ptTexMtxs[0] == postMatrix);
    const auto& twoRowMatrix = aurora::gx::g_gxState.texMtxs[1];
    assert(twoRowMatrix.m0 == postMatrix.m0 && twoRowMatrix.m1 == postMatrix.m1);
    const auto& matrixArray = aurora::gx::g_gxState.arrays[GX_TEX_MTX_ARRAY];
    assert(matrixArray.size == 144 && matrixArray.stride == 48 && matrixArray.le);
    assert(GXGetNumXfbLines(456, 1.0f) == 456);
    assert(GXGetNumXfbLines(240, 2.0f) == 479);
    assert(GXGetNumXfbLines(480, 1.5f) == 722);
    assert(GXGetYScaleFactor(456, 456) == 1.0f);
    assert(GXGetYScaleFactor(480, 720) == 718.0f / 480.0f);
    GXSetMisc(GX_MT_XF_FLUSH, 8);
    assert(__gx->vNum == 8 && (__gx->dirtyState & 8));
    GXSetMisc(GX_MT_XF_FLUSH, 0);
    assert(__gx->vNum == 0);
    alignas(32) std::array<u8, 512> contextList{};
    GXSetMisc(GX_MT_DL_SAVE_CONTEXT, 1);
    GXBeginDisplayList(contextList.data(), contextList.size());
    GXSetCullMode(GX_CULL_FRONT);
    GXEndDisplayList();
    assert(((__gx->genMode >> 14) & 3) == GX_CULL_FRONT);
    GXSetMisc(GX_MT_DL_SAVE_CONTEXT, 0);
    GXBeginDisplayList(contextList.data(), contextList.size());
    GXSetCullMode(GX_CULL_FRONT);
    GXEndDisplayList();
    assert(((__gx->genMode >> 14) & 3) == GX_CULL_BACK);
    GXSetMisc(GX_MT_DL_SAVE_CONTEXT, 1);
    GXSetCullMode(GX_CULL_BACK);
    alignas(32) std::array<u8, 64> image{};
    image.fill(255);
    alignas(32) std::array<u8, 32> palette{};
    for (size_t i = 0; i < palette.size(); i += 2) palette[i] = 0xf8;
    const auto handle = AuroraRegisterTexturePointer(image.data());
    assert(handle == AuroraRegisterTexturePointer(image.data()));
    assert(AuroraResolveTexturePointer(handle) == image.data());
    assert(AuroraResolveTexturePointer(AuroraRegisterTexturePointer(nullptr)) == nullptr);
    const auto* high = reinterpret_cast<const void*>(uintptr_t{0x1234567800000020});
    assert(AuroraResolveTexturePointer(AuroraRegisterTexturePointer(high)) == high);

    alignas(32) std::array<u8, 32> moviePixels{};
    GXTexObj moviePlane{};
    GXInitTexObjStreaming(&moviePlane, moviePixels.data(), 8, 4, GX_TF_I8);
    GXLoadTexObj(&moviePlane, GX_TEXMAP0);
    GXDrawDone();
    assert(aurora::gx::g_gxState.loadedTextures[0].is_streaming());
    const auto movieTexture = aurora::gx::texture::resolve_static_texture(aurora::gx::g_gxState.loadedTextures[0]);
    assert(movieTexture->size.width == 8);

    alignas(32) std::array<u8, 512> list{};
    GDLObj dl;
    GDInitGDLObj(&dl, list.data(), list.size());
    GDSetCurrent(&dl);
    writeGalaxyImagePointer(image.data());
    assert(GDGetCurrOffset() == 5);
    writeGalaxyRgbaAttributes();
    assert(GDGetCurrOffset() == 20);
    assert(aurora::gx::g_gxState.loadedTextures[0].data != image.data());
    GXCallDisplayList(list.data(), GDGetCurrOffset());
    GXDrawDone();
    auto& texture = aurora::gx::g_gxState.loadedTextures[0];
    assert(texture.data == image.data() && texture.width() == 4 && texture.height() == 4 && texture.format() == GX_TF_RGBA8);
    assert(!texture.has_mips() && texture.texObjId == 0 && !texture.is_streaming());
    const auto modelTexture = aurora::gx::texture::resolve_static_texture(texture);
    assert(modelTexture && modelTexture != movieTexture && modelTexture->size.width == 4);

    GDSetCurrOffset(0);
    writeGalaxyPaletteList(image.data(),palette.data());
    assert(GDGetCurrOffset() == 55);
    GXCallDisplayList(list.data(), GDGetCurrOffset());
    GXDrawDone();
    auto& indexed = aurora::gx::g_gxState.loadedTextures[7];
    auto& tlut = aurora::gx::g_gxState.loadedTluts[indexed.tlut];
    assert(indexed.data == image.data() && tlut.data == palette.data());
    assert(tlut.numEntries == 16 && tlut.format == GX_TL_RGB565);
    assert(aurora::gx::texture::resolve_static_palette_texture(indexed,tlut));

    GDSetCurrOffset(0);
    GDSetTexImgPtr(GX_TEXMAP4, palette.data());
    const auto commandEnd = GDGetCurrOffset();
    assert(commandEnd == 5);
    GDSetCurrOffset(2);
    GDPatchTexImgPtr(image.data());
    assert(GDGetCurrOffset() == commandEnd);
    GXCallDisplayList(list.data(), commandEnd);
    GXDrawDone();
    assert(aurora::gx::g_gxState.loadedTextures[4].data == image.data());

    GXTexObj object{};
    GXTlutObj objectPalette{};
    GXInitTexObjCI(&object, image.data(), 8, 8, GX_TF_C4, GX_CLAMP, GX_CLAMP, GX_FALSE, GX_TLUT2);
    GXInitTlutObj(&objectPalette, palette.data(), GX_TL_RGB565, 16);
    GXLoadTexObj(&object, GX_TEXMAP7);
    GXLoadTlut(&objectPalette, GX_TLUT2);
    GXDrawDone();
    assert(indexed.data == image.data() && indexed.tlut == GX_TLUT2 && indexed.texObjId != 0);
    assert(aurora::gx::g_gxState.loadedTluts[2].data == palette.data());
    assert(aurora::gx::g_gxState.loadedTluts[2].numEntries == 16);
    GXLoadTexObj(&object,GX_TEXMAP2);
    GXDrawDone();
    assert(aurora::gx::g_gxState.loadedTluts[2].data == palette.data());
    GDSetCurrOffset(0);
    writeGalaxyPaletteList(image.data(),palette.data());
    GXCallDisplayList(list.data(),GDGetCurrOffset());
    GXDrawDone();
    assert(indexed.tlut == aurora::gx::MaxTluts + 7);
    assert(aurora::gx::g_gxState.loadedTluts[2].data == palette.data());
    alignas(32) std::array<u8,512> largePalette{};
    GDSetCurrOffset(0);
    GDLoadTlut(largePalette.data(),0xf0000,GX_TLUT_256);
    GDSetTexTlut(GX_TEXMAP7,0xf0000,GX_TL_IA8);
    GXCallDisplayList(list.data(),GDGetCurrOffset());
    GXDrawDone();
    assert(aurora::gx::g_gxState.loadedTluts[indexed.tlut].numEntries == 256);
    assert(aurora::gx::g_gxState.loadedTluts[indexed.tlut].data == largePalette.data());
    GDSetCurrOffset(0);
    writeGalaxyMipList(image.data());
    GXCallDisplayList(list.data(),GDGetCurrOffset());
    GXDrawDone();
    assert(indexed.texObjId == 0 && indexed.has_mips() && indexed.mip_count() == 3);
    GDSetCurrent(nullptr);
    verifyGalaxyText();
    // A newly requested pipeline must be usable on its first visible frame.
    aurora::gx::PipelineConfig fresh{};
    aurora::gx::populate_pipeline_config(fresh,GX_TRIANGLES,GX_VTXFMT0);
    fresh.polygonOffsetBits=std::bit_cast<u32>(0.1234567f);
    const auto ref=aurora::gfx::pipeline_ref(fresh);
    aurora::gfx::CompiledPipeline ready;
    assert(aurora::gfx::get_pipeline(ref,ready) && ready.main);
    GXSetScissor(0, 0, 640, 480);
    GXSetViewport(0, 0, 640, 480, 0, 1);
    GXSetScissorBoxOffset(0, 1584);
    GXDrawDone();
    auto& state = aurora::gx::g_gxState;
    assert(state.scissorOffsetY == 1584 && state.renderViewport.top > 0);
    const auto expectedScissor = aurora::gx::map_logical_scissor({0, 464, 640, 480});
    assert(state.renderScissor.y == expectedScissor.y && state.renderScissor.height == expectedScissor.height);
    GXSetScissorBoxOffset(0, 0);
    GXSetZScaleOffset(1, 0.00001f);
    GXDrawDone();
    assert(state.renderViewport.zfar > 1.f && state.renderViewport.znear > 0.f);
    verifyGalaxyText();
    GXSetZScaleOffset(1, 0);
    GXSetCopyClamp(static_cast<GXFBClamp>(GX_CLAMP_TOP | GX_CLAMP_BOTTOM));
    assert((__gx->cpTex & 3) == 3);
    aurora::gfx::depth_peek::testing::set_latest_color(2, 1, {0x80123456, 0xffabcdef});
    u32 pixel;
    GXPokeAlphaRead(GX_READ_NONE); GXPeekARGB(0, 0, &pixel); assert(pixel == 0x80123456);
    GXPokeAlphaRead(GX_READ_00); GXPeekARGB(0, 0, &pixel); assert(pixel == 0x00123456);
    GXPokeAlphaRead(GX_READ_FF); GXPeekARGB(0, 0, &pixel); assert(pixel == 0xff123456);
    GXPokeAlphaRead(GX_READ_NONE);
    aurora::gfx::depth_peek::testing::reset();
    alignas(32) std::array<u8,256> clearCopy{}, depth8{}, depth16{};
    GXSetPixelFmt(GX_PF_RGB8_Z24,GX_ZC_LINEAR);
    GXSetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);
    GXSetCopyClear({0,0,0,255},0x123456);
    GXSetTexCopySrc(3,5,8,8);
    GXSetTexCopyDst(8,8,GX_TF_RGBA8,GX_FALSE);
    GXCopyTex(clearCopy.data(),GX_TRUE);
    GXSetTexCopyDst(4,4,GX_TF_Z8,GX_FALSE);
    GXCopyTex(depth8.data(),GX_FALSE);
    GXSetTexCopyDst(8,8,GX_TF_Z16,GX_FALSE);
    GXCopyTex(depth16.data(),GX_FALSE);
    GXDrawDone();
    const auto copied8=state.copyTextures.at(depth8.data()).handle;
    const auto copied16=state.copyTextures.at(depth16.data()).handle;
    aurora::gfx::depth_peek::request_snapshot();
    aurora_end_frame();
    bool captured=false;
    for(unsigned frame=0;frame<120 && !captured;++frame) {
        aurora_update(); assert(aurora_begin_frame());
        aurora::gfx::depth_peek::request_snapshot(); aurora_end_frame(); SDL_Delay(2);
        u32 depth=0, color=0;
        captured=aurora::gfx::depth_peek::read_latest(0,0,depth) && aurora::gfx::depth_peek::read_latest_color(0,0,color);
        if(captured) assert(depth<=0xffffff);
    }
    assert(captured);
    // The direct queue readback must follow every queued renderer submission.
    aurora::gfx::render_worker::synchronize();
    auto& device=aurora::webgpu::g_device;
    const wgpu::BufferDescriptor readbackDesc{.usage=wgpu::BufferUsage::CopyDst|wgpu::BufferUsage::MapRead,.size=512};
    auto readback=device.CreateBuffer(&readbackDesc);
    auto encoder=device.CreateCommandEncoder();
    unsigned offset=0;
    for(const auto& texture:{copied8,copied16}) {
        const wgpu::TexelCopyTextureInfo src{.texture=texture->texture};
        const wgpu::TexelCopyBufferInfo dst{.layout={.offset=offset,.bytesPerRow=256},.buffer=readback};
        const wgpu::Extent3D size{1,1,1};
        encoder.CopyTextureToBuffer(&src,&dst,&size); offset+=256;
    }
    const auto commands=encoder.Finish(); aurora::webgpu::g_queue.Submit(1,&commands);
    std::atomic<bool> mapped=false;
    readback.MapAsync(wgpu::MapMode::Read,0,512,wgpu::CallbackMode::AllowSpontaneous,
        [&](wgpu::MapAsyncStatus status,wgpu::StringView){ assert(status==wgpu::MapAsyncStatus::Success); mapped=true; });
    for(unsigned i=0;i<1000 && !mapped;++i) { aurora::webgpu::g_instance.ProcessEvents(); SDL_Delay(2); }
    assert(mapped);
    const auto* pixels=static_cast<const u8*>(readback.GetConstMappedRange(0,512));
    std::fprintf(stderr,"Depth copies Z8=%02x %02x %02x %02x Z16=%02x %02x %02x %02x\n",
        pixels[0],pixels[1],pixels[2],pixels[3],pixels[256],pixels[257],pixels[258],pixels[259]);
    for(unsigned i=0;i<4;++i) assert(pixels[i]==0x12);
    assert(pixels[256]==0x12 && pixels[257]==0x12 && pixels[258]==0x12 && pixels[259]==0x34);
    readback.Unmap();
    aurora_shutdown();
    return 0;
}
