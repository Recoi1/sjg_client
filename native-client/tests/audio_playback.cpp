#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudSceneMgr.hpp"
#include "Game/AudioLib/AudSoundId.hpp"
#include "Game/RhythmLib/AudRhythmMeSystem.hpp"
#include "Game/RhythmLib/AudMePlayer.hpp"
#include "Game/RhythmLib/AudMeSeqReader.hpp"
#include "JSystem/JAudio2/JASCriticalSection.hpp"
#include "Game/AudioLib/AudAnmSoundObject.hpp"
#include "JSystem/JAudio2/JASAiCtrl.hpp"
#include "JSystem/JAudio2/JASDSPInterface.hpp"
#include "JSystem/JKernel/JKRAram.hpp"
#include "JSystem/JKernel/JKRExpHeap.hpp"
#include "JSystem/JKernel/JKRSolidHeap.hpp"
#include "JSystem/JKernel/JKRMemArchive.hpp"
#include "JSystem/JAudio2/JAISound.hpp"
#include "JSystem/JAudio2/JASTrack.hpp"
#include "JSystem/JAudio2/JASBank.hpp"
#include "JSystem/JAudio2/JASBankTable.hpp"
#include "JSystem/JAudio2/JASBasicInst.hpp"
#include "JSystem/JAudio2/JASWaveInfo.hpp"
#include "audio_device.hpp"
#include "os_services.hpp"
#include "crash.hpp"
#include "yaz0.hpp"
#include "characters.hpp"
#include <aurora/aurora.h>
#include <aurora/dvd.h>
#include <SDL3/SDL.h>
#include <atomic>
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstdio>
#include <cstring>

namespace {
std::atomic<unsigned> nonzero{};
std::atomic<unsigned> callbacks{};
void output(s16* samples, u32 frames) {
    ++callbacks;
    unsigned count = 0;
    for (unsigned i = 0; i < frames * 2; ++i) count += samples[i] != 0;
    nonzero += count;
}

}
int main(int argc, char** argv) {
    assert(argc == 3); galaxy::installCrashHandler();
    AuroraConfig config{}; config.appName = "Galaxy audio regression";
    config.mem2Size = 64 * 1024 * 1024;
    const auto platform = aurora_initialize(argc, argv, &config);
    SDL_SetWindowOpacity(platform.window, 0);
    assert(aurora_dvd_open(argv[1]));
    auto modRoot = std::filesystem::path(argv[2]);
    for (unsigned i = 0; i < 4; ++i) modRoot = modRoot.parent_path();
    galaxy::initializeCharacters(modRoot / "mods");
    galaxy::initializeCharacterOverlays(modRoot / "mods");
    JKRMemArchive::setNativeResourceRoot(argv[2]);
    auto* root = JKRExpHeap::createRoot(1, false); assert(root);
    auto* systemHeap = JKRExpHeap::create(8 * 1024 * 1024, root, false); assert(systemHeap);
    JKRAram::create(0xe00000, 0xffffffff, 8, 7, 3);
    auto* heap = JKRSolidHeap::create(24 * 1024 * 1024, root, false); assert(heap);
    std::ifstream file(std::string(argv[2]) + "/AudioRes/SMR.szs", std::ios::binary); assert(file);
    const std::vector<u8> packed{std::istreambuf_iterator<char>(file), {}};
    auto resource = galaxy::decodeYaz0(packed);
    auto* baa = root->alloc(resource.size(), 32); assert(baa);
    std::memcpy(baa, resource.data(), resource.size());
    auto mount = [&](const char* path) { return JKRArchive::mount(path, JKRArchive::MOUNT_MODE_MEM, nullptr, JKRArchive::MOUNT_DIRECTION_1); };
    auto* seq = mount("/AudioRes/Seqs/JaiSeq.arc"); assert(seq);
    auto* chord = mount("/AudioRes/Info/JaiChord.arc"); assert(chord);
    auto* me = mount("/AudioRes/Info/JaiMe.arc"); assert(me);
    auto* remix = mount("/AudioRes/Info/JaiRemixSeq.arc"); assert(remix);
    JASDriver::dacCallbackFunc = output;
    root->becomeCurrentHeap();
    JKRHeap::setSystemHeap(systemHeap);
    auto* system = AudNewAudSystem(heap, baa, seq, chord, me, remix); assert(system);
    assert(system->mRhythmMeSystem->mMeMgr->mMeSeq->mNumEntries == 40);
    {
        JASCriticalSection critical;
        auto* manager=system->mRhythmMeSystem->mMeMgr;
        std::vector<u8> bytes{0,0,0,2, 0,0,0,12, 255,255,255,255, 0x12,0x34,0x56,0x78};
        const auto original=bytes;
        manager->setMeSeq(bytes.data(),bytes.size());
        assert(bytes==original && manager->mMeSeq->mNumEntries==2);
        assert(manager->getSeqStartPos(0)==12 && manager->getSeqStartPos(1)==-1 && manager->getSeqStartPos(2)==-1);
        AudMeSeqReader reader; reader.init(manager->mMeSeq);
        assert(reader.get16(13)==0x3456 && reader.get24(12)==0x123456 && reader.get32(12)==0x12345678);
        reader.jump(12); assert(reader.read24()==0x123456 && reader.read8()==0x78);
        reader.jump(12); assert(reader.read16()==0x1234);
        reader.jump(12); assert(reader.call(13) && reader.read8()==0x34 && reader.ret() && reader.read8()==0x12);
        auto rejected=[&](std::vector<u8>& data, size_t size) {
            const auto* previous=manager->mMeSeq; bool failed=false;
            try { manager->setMeSeq(data.data(),size); } catch(const std::runtime_error&) { failed=true; }
            assert(failed && manager->mMeSeq==previous && manager->getSeqStartPos(0)==12);
        };
        for(size_t size=0;size<=12;++size) rejected(bytes,size);
        for(u32 offset:{0u,11u,16u,0x7fffffffu}) {
            auto bad=original; for(unsigned i=0;i<4;++i) bad[4+i]=u8(offset>>(24-i*8)); rejected(bad,bad.size());
        }
        auto bad=original; bad[0]=0x7f; rejected(bad,bad.size());
        std::vector<u8>().swap(bytes); assert(reader.get32(12)==0x12345678);
        manager->setMeSeq(me->getResource(u16(0)));
        assert(manager->mMeSeq->mNumEntries==40);
        for(unsigned i=0;i<40;++i) assert(manager->getSeqStartPos(i)==-1 || size_t(manager->getSeqStartPos(i))<manager->mNativeSeqBytes.size());
        std::fprintf(stderr,"Music-event offset table, bytecode endian, bounds and ownership checks passed\n");
    }
    system->mSceneMgr->loadStaticResource();
    for (unsigned i = 0; (!system->mSceneMgr->isLoadDoneStaticResource() || !system->mSceneMgr->isLoadDoneSystemInit()) && i < 600; ++i) SDL_Delay(10);
    assert(system->mSceneMgr->isLoadDoneStaticResource() && system->mSceneMgr->isLoadDoneSystemInit());
    {
        system->mSceneMgr->setPlayerModeMario();
        system->mSceneMgr->loadStageResource("Game", "FileSelect");
        for (unsigned i = 0; !system->mSceneMgr->isLoadDoneStageResource() && i < 600; ++i) SDL_Delay(10);
        assert(system->mSceneMgr->isLoadDoneStageResource());
        auto* bank = JASDefaultBankTable::getInstance()->getBank(98);
        auto* original = bank->getWaveBank();
        for (auto character : {galaxy::Character::Peach, galaxy::Character::Daisy, galaxy::Character::Mario, galaxy::Character::Luigi}) {
            galaxy::selectCharacterMenuVoices(character);
            assert((bank->getWaveBank() != original) == (static_cast<unsigned>(character) >= 2));
            JASInstParam instrument;
            assert(bank->getInstParam(0, galaxy::marioCharacter(character) ? 36 : 60, 127, &instrument));
            auto* wave = bank->getWaveBank()->getWaveHandle(instrument._18);
            assert(wave && wave->getWavePtr() && wave->getWaveInfo()->mSampleCount > 1000);
            JAISoundHandle selection;
            const auto before = nonzero.load();
            assert(system->startSound(galaxy::marioCharacter(character) ? SE_SY_FILE_SEL_SW_MARIO : SE_SY_FILE_SEL_SW_LUIGI, &selection, nullptr));
            for (unsigned i=0; i<60; ++i) { system->frameWork(); galaxy::checkAudioDevice(); SDL_Delay(16); }
            assert(nonzero.load() > before + 100);
            if (selection.isSoundAttached()) selection->stop(0);
            std::fprintf(stderr, "%s menu confirmation produced %u nonzero PCM samples\n", galaxy::characterName(character), nonzero.load()-before);
        }
    }
    system->mSceneMgr->setPlayerModeMario();
    system->mSceneMgr->loadStageResource("Game", "HeavensDoorGalaxy");
    for (unsigned i = 0; !system->mSceneMgr->isLoadDoneStageResource() && i < 600; ++i) SDL_Delay(10);
    assert(system->mSceneMgr->isLoadDoneStageResource());
    JAISoundHandle handle;
    assert(system->startSound(MBGM_GALAXY_24, &handle, nullptr));
    const auto before = nonzero.load();
    for (unsigned i = 0; i < 180; ++i) { system->frameWork(); galaxy::checkAudioDevice(); SDL_Delay(16); }
    std::fprintf(stderr, "Mission music produced %u nonzero PCM samples, %u callbacks, %u DSP subframes\n", nonzero.load() - before, callbacks.load(), JASDriver::sSubFrameCounter);
    assert(nonzero.load() > before + 1000);
    handle->stop(0); handle.releaseSound();
    for (unsigned i = 0; i < 30; ++i) { system->frameWork(); SDL_Delay(16); }
    assert(system->mSceneMgr->isPlayerResourceLoaded());
    TVec3f position(0, 0, 0); JAISoundHandle effect;
    const auto effectBefore = nonzero.load();
    assert(system->startSound(SE_PM_JUMP, &effect, &position));
    for (unsigned i = 0; i < 45; ++i) { system->frameWork(); galaxy::checkAudioDevice(); SDL_Delay(16); }
    assert(nonzero.load() > effectBefore + 100);
    std::fprintf(stderr, "Positional jump sound produced %u nonzero PCM samples\n", nonzero.load() - effectBefore);
    effect.releaseSound();
    {
        AudAnmSoundObject animationSound(&position, 1, root);
        animationSound.addToSoundObjHolder();
        JAUSoundAnimationSound peachCall{};
        peachCall.mSoundID = SE_SV_KPVS3_PEACH_CALL_MARIO;
        for (auto character : {galaxy::Character::Mario, galaxy::Character::Luigi, galaxy::Character::Peach, galaxy::Character::Daisy, galaxy::Character::Mario}) {
            galaxy::activateCharacter(character);
            const bool luigi = !galaxy::marioCharacter(character);
            if (luigi) system->mSceneMgr->setPlayerModeLuigi();
            else system->mSceneMgr->setPlayerModeMario();
            system->mSceneMgr->loadStageResource("Game", "HeavensDoorGalaxy");
            for (unsigned i = 0; !system->mSceneMgr->isLoadDoneStageResource() && i < 600; ++i) SDL_Delay(10);
            assert(system->mSceneMgr->isLoadDoneStageResource() && system->mSceneMgr->isPlayerResourceLoaded());
            assert(animationSound.getSoundID(&peachCall, position, 1) ==
                   (luigi ? SE_SV_KPVS3_PEACH_CALL_LUIGI : SE_SV_KPVS3_PEACH_CALL_MARIO));
            for (u32 id : {SE_PV_JUMP_S, SE_PV_JUMP_LONG}) {
                JAISoundHandle voice;
                const auto voiceBefore = nonzero.load();
                assert(system->startSound(id, &voice, &position) && voice.isSoundAttached());
                auto* track = voice->getTrack();
                assert(track && track->readPortSelf(12) == (luigi ? 20 : 0));
                for (unsigned i = 0; i < 45; ++i) { system->frameWork(); galaxy::checkAudioDevice(); SDL_Delay(16); }
                assert(nonzero.load() > voiceBefore + 100);
                std::fprintf(stderr, "%s voice %08x produced %u nonzero PCM samples\n", galaxy::characterName(character), id, nonzero.load() - voiceBefore);
                if (voice.isSoundAttached()) voice->stop(0);
            }
        }
    }
    galaxy::shutdownAlarms(); galaxy::shutdownAudioDevice();
    aurora_dvd_close();
    galaxy::shutdownOsServices();
    aurora_shutdown();
    return 0;
}
