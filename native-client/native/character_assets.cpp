#include "characters.hpp"
#include "audio_archive.hpp"
#include "tpl.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include <nw4r/lyt/picture.h>
#include <nw4r/lyt/material.h>
#include <nw4r/ut/Font.h>
#include "io.hpp"
#include <aurora/dvd.h>
#include <SDL3/SDL_iostream.h>
#include <JSystem/JAudio2/JASBank.hpp>
#include <JSystem/JAudio2/JASBankTable.hpp>
#include <JSystem/JAudio2/JASWSParser.hpp>
#include <JSystem/JAudio2/JASWaveArcLoader.hpp>
#include <JSystem/JAudio2/JASWaveInfo.hpp>
#include <JSystem/JAudio2/JAUSectionHeap.hpp>
#include <JSystem/JKernel/JKRHeap.hpp>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <fstream>
#include <array>

namespace galaxy {
namespace {
struct Overlay { std::filesystem::path physical; std::string disc; };
std::vector<std::unique_ptr<Overlay>> overlays;
std::filesystem::path characterRoot;
JASWaveBank* originalVoices = nullptr;
JASWaveBank* customVoices = nullptr;
JASWaveBank* originalMenuVoices = nullptr;
JASWaveBank* customMenuVoices = nullptr;
JAUSectionHeap* voiceHeap = nullptr;
std::vector<u8> lifeIconData;
std::array<TplImage, 4> lifeIcons;
}
void initializeCharacterOverlays(const std::filesystem::path& root) {
    lifeIconData.clear();
    const auto icons = root / "characters/life-icons.tpl";
    if (std::filesystem::is_regular_file(icons)) {
        if (std::filesystem::file_size(icons) > 1024 * 1024) throw std::runtime_error("Character life icons are too large");
        std::ifstream file(icons, std::ios::binary);
        lifeIconData.assign(std::istreambuf_iterator<char>(file), {});
        for (unsigned i = 0; i < lifeIcons.size(); ++i) {
            lifeIcons[i] = readTpl(lifeIconData, i);
            const auto& t = lifeIcons[i].texture;
            if (t.width != 128 || t.height != 128 || t.format != GX_TF_RGBA8 || t.maxLOD || lifeIcons[i].hasPalette)
                throw std::runtime_error("Invalid character life icon");
        }
    }
    if (!customCharactersAvailable()) return;
    characterRoot = std::filesystem::weakly_canonical(root / "characters/peach-daisy");
    overlays.clear();
    for (const char* directory : {"ObjectData", "AudioRes/Waves"}) {
        for (const auto& file : std::filesystem::directory_iterator(characterRoot / directory)) {
            if (!file.is_regular_file()) continue;
            const auto relative = std::filesystem::weakly_canonical(file.path()).lexically_relative(characterRoot);
            if (relative.empty() || relative.native().starts_with(L"..")) throw std::runtime_error("Character asset escapes its mod folder");
            const auto size = file.file_size();
            if (!size || size > 64 * 1024 * 1024) throw std::runtime_error("Invalid character asset size");
            auto overlay = std::make_unique<Overlay>();
            overlay->physical = file.path(); overlay->disc = std::string("/") + directory + "/PD_" + file.path().filename().string();
            overlays.push_back(std::move(overlay));
        }
    }
    const AuroraOverlayCallbacks callbacks{
        .open = [](void* data) -> void* { return aurora::io::open_file(static_cast<Overlay*>(data)->physical, "rb").release(); },
        .close = [](void* handle) { SDL_CloseIO(static_cast<SDL_IOStream*>(handle)); },
        .read = [](void* handle, uint8_t* data, size_t size) -> int64_t {
            auto* stream = static_cast<SDL_IOStream*>(handle);
            const auto count = SDL_ReadIO(stream, data, size);
            return !count && SDL_GetIOStatus(stream) == SDL_IO_STATUS_ERROR ? -1 : count;
        },
        .seek = [](void* handle, int64_t offset, int32_t whence) -> int64_t {
            return whence < 0 || whence > 2 ? -1 : SDL_SeekIO(static_cast<SDL_IOStream*>(handle), offset, static_cast<SDL_IOWhence>(whence));
        }};
    aurora_dvd_overlay_callbacks(&callbacks);
    std::vector<AuroraOverlayFile> files;
    for (const auto& overlay : overlays) files.push_back({overlay->disc.c_str(), overlay.get(), std::filesystem::file_size(overlay->physical)});
    std::vector<s32> entries(files.size(), -1);
    aurora_dvd_overlay_files(files.data(), files.size(), entries.data());
    for (const auto entry : entries) if (entry < 0) throw std::runtime_error("Could not register character asset");
}
bool characterLifeGlyph(nw4r::ut::Glyph* glyph) {
    if (lifeIconData.empty()) return false;
    const auto& t = lifeIcons[static_cast<unsigned>(activeCharacter())].texture;
    *glyph = {t.data, {0, 128, 0}, 128, GX_TF_RGBA8, 128, 128, 0, 0};
    return true;
}
void updateCharacterLifeIcon(LayoutActor* layout) {
    nw4r::ut::Glyph glyph;
    if (!characterLifeGlyph(&glyph)) return;
    nw4r::lyt::TexMap texture;
    texture.SetImage(glyph.pTexture); texture.SetSize(128, 128); texture.SetTexelFormat(GX_TF_RGBA8);
    const nw4r::math::VEC2 coordinates[4] = {{0,0}, {1,0}, {0,1}, {1,1}};
    for (const char* name : {"PicMario", "ShaMario", "PicMarioC", "ShaMarioC"}) {
        auto* picture = static_cast<nw4r::lyt::Picture*>(MR::getPane(layout, name));
        auto* material = picture->GetMaterial();
        material->SetTexture(0, texture);
        if (material->GetTexSRTCap()) material->SetTexSRT(0, {{0,0}, 0, {1,1}});
        picture->mTexCoordAry.Copy(coordinates, 1);
    }
}
void prepareCharacterVoices(JAUSectionHeap* heap) {
    if (!heap) return;
    auto& table = heap->getWaveBankTable();
    if (voiceHeap != heap) {
        originalVoices = table.getWaveBank(34); customVoices = nullptr; voiceHeap = heap;
        originalMenuVoices = JASDefaultBankTable::getInstance()->getBank(98)->getWaveBank();
        customMenuVoices = nullptr;
    }
    if (!originalVoices) throw std::runtime_error("Player voice bank is missing");
    if (customCharactersAvailable() && !customVoices) {
        struct ScratchHeap {
            JKRHeap* previous = JKRHeap::sCurrentHeap;
            ScratchHeap() { JKRHeap::sCurrentHeap = nullptr; }
            ~ScratchHeap() { JKRHeap::sCurrentHeap = previous; }
        } scratch;
        for (const char* filename : {"player-voices.wsys", "menu-voices.wsys"}) {
            const bool menu = std::strcmp(filename, "menu-voices.wsys") == 0;
            const auto source = aurora::io::read_file(characterRoot / "AudioRes" / filename);
            if (!source || source->size() > 4 * 1024 * 1024) throw std::runtime_error("Custom player voice bank is missing or too large");
            auto data = importWaveBank(*source);
            u32 offset; std::memcpy(&offset, data.data() + 16, 4);
            u32 count; std::memcpy(&count, data.data() + offset + 4, 4);
            if (count != (menu ? 1 : 3)) throw std::runtime_error("Invalid custom voice archive count");
            for (unsigned i = 0; i < count; ++i) {
                u32 archive; std::memcpy(&archive, data.data() + offset + 8 + i * 4, 4);
                const std::string name(reinterpret_cast<char*>(data.data() + archive));
                const auto expected = menu ? "B98kawa_0.aw" : "B70kawa_" + std::to_string(i) + ".aw";
                if (name != expected) throw std::runtime_error("Unexpected custom voice archive");
                std::memset(data.data() + archive, 0, 112);
                std::memcpy(data.data() + archive, ("PD_" + name).c_str(), name.size() + 4);
            }
            auto* bank = JASWSParser::createWaveBank(data.data(), heap->getHeap());
            if (!bank) throw std::runtime_error("Could not load custom player voices");
            if (menu) {
                customMenuVoices = bank;
                if (!bank->getWaveArc(0)->load(nullptr)) throw std::runtime_error("Could not preload custom menu voices");
            } else customVoices = bank;
        }
    }
}
bool updateCharacterVoices(JAUSectionHeap* heap) {
    if (!heap) return false;
    if (voiceHeap != heap) prepareCharacterVoices(heap);
    JASDefaultBankTable::getInstance()->getBank(98)->assignWaveBank(originalMenuVoices);
    auto& table = heap->getWaveBankTable();
    auto* wanted = customCharacterResources() ? customVoices : originalVoices;
    auto* previous = table.getWaveBank(34);
    if (wanted == previous) return false;
    // Erase the old clips before replacing their wave IDs and lengths.
    heap->eraseWaveArc(34, 2); heap->eraseWaveArc(34, 4);
    auto* banks = JASDefaultBankTable::getInstance();
    for (unsigned i = 0; banks && i < 256; ++i) {
        auto* bank = banks->getBank(i);
        if (bank && bank->getWaveBank() == previous) bank->assignWaveBank(wanted);
    }
    table.registWaveBank(34, wanted);
    return true;
}
void selectCharacterMenuVoices(Character character) {
    auto* bank = static_cast<unsigned>(character) >= 2 ? customMenuVoices : originalMenuVoices;
    if (!bank || bank->getWaveArc(0)->getStatus() != 2)
        throw std::runtime_error("Character menu voices are not ready");
    JASDefaultBankTable::getInstance()->getBank(98)->assignWaveBank(bank);
}
}
