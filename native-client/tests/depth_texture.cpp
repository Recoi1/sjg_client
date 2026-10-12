#include "../aurora/lib/gx/regs.hpp"
#include "../aurora/lib/gx/shader_info.hpp"
#include "../aurora/lib/gx/texture.hpp"
#include "../aurora/lib/gfx/texture_convert.hpp"
#include <array>
#include <cassert>
int main() {
    using namespace aurora;
    gx::fifo::handle_bp(0xF4123456); gx::fifo::handle_bp(0xF500000A);
    assert(gx::g_gxState.zTextureBias == 0x123456 && gx::g_gxState.zTexture == 10);
    std::array<u8, 64> tile{};
    tile[1] = 0x12; tile[32] = 0x34; tile[33] = 0x56;
    const auto rgba = gfx::convert_texture(GX_TF_Z24X8, 4, 4, 1, tile);
    assert(rgba.data.size() == 64 && rgba.data.data()[0] == 0x12 && rgba.data.data()[1] == 0x34 && rgba.data.data()[2] == 0x56 && rgba.data.data()[3] == 0);
    tile[0] = 0x12; tile[1] = 0x34;
    const auto ia = gfx::convert_texture(GX_TF_Z16, 4, 4, 1, tile);
    assert(ia.data.data()[0] == 0x34 && ia.data.data()[3] == 0x12);
    const auto intensity = gfx::convert_texture(GX_TF_Z8, 4, 4, 1, tile);
    assert(intensity.data.data()[0] == 0x12 && intensity.data.data()[3] == 0x12);
    assert(gx::texture::texture_source_size(GX_TF_Z8, 4, 4, 1) == 32);
    assert(gx::texture::texture_source_size(GX_TF_Z16, 4, 4, 1) == 32);
    assert(gx::texture::texture_source_size(GX_TF_Z24X8, 4, 4, 1) == 64);
    gx::ShaderConfig config{};
    config.attrs[GX_VA_POS] = {.attrType=GX_DIRECT, .cnt=3, .compType=GX_F32};
    config.attrs[GX_VA_TEX0] = {.attrType=GX_DIRECT, .cnt=2, .compType=GX_F32}; config.tcgs[0].src = GX_TG_TEX0;
    config.tevStageCount = 1; config.tevStages[0].texCoordId = GX_TEXCOORD0; config.tevStages[0].texMapId = GX_TEXMAP0;
    for (unsigned type = 0; type < 3; ++type) for (unsigned op : {unsigned(GX_ZT_ADD), unsigned(GX_ZT_REPLACE)}) {
        config.zTexture = type | op << 2; config.zTextureBias = 25;
        const auto info = gx::build_shader_info(config);
        assert(info.sampledTextures.test(0) && info.sampledTexCoords.test(0));
        const auto shader = gx::build_shader_source(config, gx::DstAlphaMode::None);
        assert(shader.find("@builtin(frag_depth)") != shader.npos && shader.find("textureSampleBias") != shader.npos);
        assert(shader.find("& 0xFFFFFFu") != shader.npos && shader.find("+ 25u") != shader.npos);
        config.zTexture |= 16;
        assert(gx::build_shader_source(config, gx::DstAlphaMode::None).find("@builtin(frag_depth)") == shader.npos);
    }
    config.zTexture = 0;
    assert(!gx::build_shader_info(config).sampledTextures.test(0));
    assert(gx::build_shader_source(config, gx::DstAlphaMode::None).find("z_texture_depth") == std::string::npos);
    config.tcgs[0] = {};
    config.zTexture = GX_ZT_REPLACE << 2;
    const auto disabledGenerator = gx::build_shader_source(config, gx::DstAlphaMode::None);
    assert(disabledGenerator.find("var tc0 = vec4f(0.0, 0.0, 1.0, 1.0)") != std::string::npos);
}

