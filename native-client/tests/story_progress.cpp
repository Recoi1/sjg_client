#include "Game/System/GameSystem.hpp"
#include "Game/System/MainLoopFramework.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "runtime_options.hpp"
#include "io.hpp"
#include "Game/System/GameSystemObjHolder.hpp"
#include "Game/System/MessageHolder.hpp"
#include "Game/System/Language.hpp"
#include "Game/NPC/TalkNodeCtrl.hpp"
#include "frame_timing.hpp"
#include "input_bindings.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/System/GameDataPlayerStatus.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/System/StorySequenceExecutor.hpp"
#include "Game/System/GalaxyMoveArgument.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util/FurDrawer.hpp"
#include "JSystem/JUtility/JUTTexture.hpp"
#include "Game/Player/J3DModelX.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Animation/XanimeCore.hpp"
#include "Game/AudioLib/AudAnmSoundObject.hpp"
#include "Game/AudioLib/AudSoundId.hpp"
#include "JSystem/JAudio2/JAISound.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Animation/XanimePlayer.hpp"
#include "Game/System/UserFile.hpp"
#include "Game/System/ConfigDataHolder.hpp"
#include "Game/System/HeapMemoryWatcher.hpp"
#include "Game/Map/FileSelector.hpp"
#include "Game/Map/FileSelectModel.hpp"
#include "Game/Map/FileSelectItem.hpp"
#include "Game/NameObj/NameObjHolder.hpp"
#include "Game/NameObj/NameObjRegister.hpp"
#include "Game/NameObj/NameObjExecuteHolder.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"
#include "Game/Screen/FileSelectInfo.hpp"
#include "Game/Screen/PlayerLeft.hpp"
#include "Game/Screen/ReplaceTagProcessor.hpp"
#include "Game/Screen/MessageTagSkipTagProcessor.hpp"
#include "Game/Screen/InformationMessage.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Screen/PlayerMissLeft.hpp"
#include "Game/Screen/PauseMenu.hpp"
#include "Game/Screen/TitleSequenceProduct.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/ButtonPaneController.hpp"
#include "Game/Util/GamePadUtil.hpp"
#include "Game/Screen/GamePauseSequence.hpp"
#include "Game/Scene/GameScene.hpp"
#include "Game/Scene/ScenarioSelectScene.hpp"
#include "Game/Screen/ScenarioSelectLayout.hpp"
#include "Game/Map/ScenarioSelectStar.hpp"
#include "Game/Scene/GameScenePauseControl.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "JSystem/J3DGraphBase/J3DPacket.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/Animation/BtpPlayer.hpp"
#include "JSystem/J3DGraphAnimator/J3DModelData.hpp"
#include "JSystem/J3DGraphBase/J3DMaterial.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Camera/CameraViewInterpolator.hpp"
#include "Game/LiveActor/Binder.hpp"
#include "Game/Screen/THPSimplePlayerWrapper.hpp"
#include <algorithm>
#include "Game/Util/SingletonHolder.hpp"
#include "JSystem/JKernel/JKRMemArchive.hpp"
#include "JSystem/JKernel/JKRHeap.hpp"
#include "audio_device.hpp"
#include "os_services.hpp"
#include "nand.hpp"
#include "crash.hpp"
#include "hd_assets.hpp"
#include "characters.hpp"
#include "character_select.hpp"
#include "home_menu.hpp"
#include "pause_settings.hpp"
#include "mouse_controls.hpp"
#include "../aurora/lib/input/sdl_input.hpp"
#include "pad_snapshot.hpp"
#include "multiplayer_menu.hpp"
#include "Game/Screen/HomeButtonLayout.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <nw4r/lyt/pane.h>
#include <nw4r/lyt/material.h>
#include <nw4r/lyt/picture.h>
#include <nw4r/ut/Font.h>
#include <nw4r/ut/TextWriterBase.h>
#include <dolphin/pad.h>
#include <filesystem>
#include <aurora/aurora.h>
#include <aurora/dvd.h>
#include <aurora/main.h>
#include <revolution/nand.h>
#include <SDL3/SDL.h>
#include <array>
#include <cassert>
#include <stdexcept>
#include <cstdio>
#include <chrono>
#include <cmath>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <revolution/thp.h>

int main(int argc, char** argv) {
    assert(argc == 4); galaxy::installCrashHandler();
    AuroraConfig config{}; config.appName = "Galaxy story regression";
    config.userPath=config.cachePath=argv[3];
    std::filesystem::create_directories(argv[3]);
    for(const char* file:{"galaxy_options.dat","galaxy_bindings.dat","galaxy_controller.dat","galaxy_hd.dat"})
        std::filesystem::remove(std::filesystem::path(argv[3])/file);
    galaxy::initializeRuntimeOptions(argv[3]);
    config.mem2Size = 64 * 1024 * 1024;
    const auto platform = aurora_initialize(argc, argv, &config);
    SDL_SetWindowOpacity(platform.window, 0);
    assert(galaxy::initializeDisplaySettings(platform.window, argv[3]));
    assert(galaxy::setFpsCounterEnabled(true));
    galaxy::drawFpsCounter(); // No scene or fonts exist yet.
    assert(aurora_dvd_open(argv[1]));
    JKRMemArchive::setNativeResourceRoot(argv[2]);
    galaxy::setNandRoot(argv[3]); assert(NANDInit() == NAND_RESULT_OK);
    NANDDelete("Characters.dat");
    auto hdRoot = std::filesystem::path(argv[2]);
    for (unsigned parent = 0; parent < 4; ++parent) hdRoot = hdRoot.parent_path();
    galaxy::initializeHdAssets(hdRoot / "mods", argv[3]);
    galaxy::initializeCharacters(hdRoot / "mods");
    galaxy::initializeCharacterOverlays(hdRoot / "mods");
    initializeGame();
    std::fprintf(stderr, "Story check: initialized\n");
    auto* game = SingletonHolder<GameSystem>::get();
    // Renderer allocations must outlive game heaps, matching the native main loop.
    const auto beginHostFrame = [] {
        auto* heap=JKRHeap::sCurrentHeap; JKRHeap::sCurrentHeap=nullptr;
        const auto began=aurora_begin_frame(); JKRHeap::sCurrentHeap=heap; return began;
    };
    const auto endHostFrame = [] {
        auto* heap=JKRHeap::sCurrentHeap; JKRHeap::sCurrentHeap=nullptr;
        aurora_end_frame(); JKRHeap::sCurrentHeap=heap;
    };
    for (unsigned i = 0; !game->mSceneController->mScenarioParser && i < 1200; ++i) {
        aurora_update();
        if (beginHostFrame()) { game->frameLoop(); galaxy::drawFpsCounter(); endHostFrame(); }
        SDL_Delay(2);
    }
    assert(game->mSceneController->mScenarioParser);
    std::fprintf(stderr, "Story check: parser ready\n");
    {
        FurDrawer fur(3, nullptr, nullptr);
        for (auto& density : fur.mDensity) density = 0;
        fur.createFurMap();
        const auto* pixels = static_cast<const u8*>(fur.mFurTexture->mImage);
        for (unsigned pixel = 0; pixel < 32 * 32; ++pixel)
            assert(pixels[2 * pixel] == 0 && pixels[2 * pixel + 1] == 255);
        fur.mDensity[0] = 0.8f; fur.createFurMap();
        unsigned strands = 0;
        for (unsigned pixel = 0; pixel < 32 * 32; ++pixel) {
            const auto alpha = pixels[2 * pixel], intensity = pixels[2 * pixel + 1];
            assert((alpha == 0 && intensity == 255) || (alpha == 131 && intensity == 229));
            strands += alpha != 0;
        }
        assert(strands > 0);
    }
    auto checkPlayerNames = [&] {
        const auto previous = galaxy::activeCharacter();
        for (unsigned i=0; i<4; ++i) {
            const auto character = static_cast<galaxy::Character>(i);
            galaxy::activateCharacter(character);
            wchar_t selected[16]{}; std::swprintf(selected,16,L"%hs",galaxy::characterName(character));
            for (unsigned form=0; form<3; ++form) {
                char id[64]; std::snprintf(id,sizeof(id),"System_PlayerName%d%02u",galaxy::marioCharacter(character) ? 0 : 1,form);
                const auto* localized = MR::getGameMessageDirect(id); assert(localized);
                const auto expectedLength = i>=2 ? 5 : MR::getStringLengthWithMessageTag(localized);
                const auto* expected = i>=2 ? selected : localized;
                wchar_t output[256]{};
                const auto length=ReplaceTagProcessor::exeLocalizeGroupPlayerName(output,form);
                assert(length==expectedLength && !std::memcmp(output,expected,(length+1)*sizeof(wchar_t)));
            }
            const auto* source=MR::getLayoutMessageDirect("InformationObserverOneUp");
            bool hasPlayerName=false;
            for (auto* p=source; *p;) {
                if (*p==0x1a) {
                    const MessageEditorMessageTag tag(p+1);
                    hasPlayerName |= tag.isGroupTagId(5,0);
                    p+=tag.getSkipLength()+1;
                } else ++p;
            }
            wchar_t message[512]{};
            const auto length=ReplaceTagProcessor::Replace(message,source);
            // Some localizations omit the character-name tag in this message.
            if (hasPlayerName) assert(std::search(message,message+length,selected,selected+5)!=message+length);
        }
        galaxy::activateCharacter(previous);
        std::fprintf(stderr,"Story check: all four 1-Up names and localized name forms passed (%s)\n",MR::getCurrentLanguagePrefix());
    };
    checkPlayerNames();

    GameDataHolder fresh(nullptr);
    assert(fresh.isDataMario() && fresh.mPlayerStatus->mStoryProgress == 0);
    assert(!fresh.isPassedStoryEvent("クッパ襲来後"));
    assert(!fresh.isPassedStoryEvent("ピーチ城浮上後"));
    assert(!fresh.isPassedStoryEvent("スピン権利"));
    fresh.followStoryEventByName("クッパ襲来後");
    assert(fresh.isPassedStoryEvent("クッパ襲来後") && !fresh.isPassedStoryEvent("ピーチ城浮上後"));
    fresh.followStoryEventByName("ピーチ城浮上後");
    assert(fresh.isPassedStoryEvent("ピーチ城浮上後") && !fresh.isPassedStoryEvent("スピン権利"));
    std::array<u8, 4096> binary{};
    const auto size = fresh.makeFileBinary(binary.data(), binary.size());
    std::fprintf(stderr, "Story check: saved %u bytes\n", size);
    GameDataHolder loaded(nullptr);
    assert(loaded.loadFromFileBinary("mario1", binary.data(), size));
    assert(loaded.isPassedStoryEvent("ピーチ城浮上後") && !loaded.isPassedStoryEvent("スピン権利"));
    assert(loaded.isDataMario());
    assert(loaded.loadFromFileBinary("luigi1", binary.data(), size) && !loaded.isDataMario());
    assert(loaded.isPassedStoryEvent("ピーチ城浮上後") && !loaded.isPassedStoryEvent("スピン権利"));
    assert(loaded.loadFromFileBinary("mario1", binary.data(), size) && loaded.isDataMario());
    try { loaded.isPassedStoryEvent("missing-event"); assert(false); } catch (const std::runtime_error&) {}
    const auto progress = loaded.mPlayerStatus->mStoryProgress;
    try { loaded.followStoryEventByName("missing-event"); assert(false); } catch (const std::runtime_error&) {}
    assert(loaded.mPlayerStatus->mStoryProgress == progress);
    THPSimplePlayerWrapper movieAudio("Movie audio boundary check");
    constexpr unsigned samples = 560;
    std::array<s16, samples * 4 + 32> output;
    for (unsigned branch = 0; branch < 4; ++branch) {
        output.fill(0x5A5A);
        movieAudio.mOpen = branch != 0; movieAudio.mAudioExist = true;
        movieAudio._2F0 = branch == 2; movieAudio.mAudioOutputIndex = 0;
        for (auto& buffer : movieAudio.mAudioBuffer) buffer.validSample = 0;
        std::array<s16, 6> pcm{111, 222, 333, 444, 555, 666};
        if (branch == 3) {
            movieAudio.mAudioBuffer[0].curPtr = pcm.data(); movieAudio.mAudioBuffer[0].validSample = 2;
            movieAudio.mAudioBuffer[1].curPtr = pcm.data() + 4; movieAudio.mAudioBuffer[1].validSample = 1;
            movieAudio.mCurrentVolume = movieAudio.mTargetVolume = 127;
            movieAudio._2F4 = 1; movieAudio._2FC = 0; movieAudio._30C = 0;
        }
        movieAudio.mixAudio(output.data() + 8, samples);
        assert(std::all_of(output.begin(), output.begin() + 8, [](s16 v) { return v == 0x5A5A; }));
        assert(std::all_of(output.begin() + 8 + samples * 2, output.end(), [](s16 v) { return v == 0x5A5A; }));
        const unsigned mixed = branch == 3 ? 6 : 0;
        assert(std::all_of(output.begin() + 8 + mixed, output.begin() + 8 + samples * 2, [](s16 v) { return v == 0; }));
        if (branch == 3) assert(output[8] == 111 && output[9] == 222 && output[12] == 555);
    }
    movieAudio.mOpen = false;
    for (bool hd : {true, false}) {
        assert(galaxy::setHdMoviesEnabled(hd));
        assert(movieAudio.init(0) && movieAudio.open("/MovieData/PrologueA.thp"));
        for (unsigned i = 0; !movieAudio._9 && i < 2000; ++i) {
            movieAudio.updateNerve(); aurora_update(); SDL_Delay(1);
        }
        assert(movieAudio._9 && !movieAudio.mDvdError);
        auto* movieMemory = new (JKRHeap::sRootHeap, 32) u8[movieAudio.calcNeedMemory()];
        assert(movieAudio.setBuffer(movieMemory) && movieAudio.preLoad(0));
        for (unsigned i = 0; movieAudio.isPreLoading() && i < 2000; ++i) {
            movieAudio.updateNerve(); aurora_update(); SDL_Delay(1);
        }
        assert(!movieAudio.isPreLoading() && !movieAudio.mDvdError);
        const bool fullPlayback = hd && std::getenv("GALAXY_VERIFY_HD_PLAYBACK") != nullptr;
        const unsigned frames = fullPlayback ? 5591 : 60;
        if (fullPlayback) SDL_SetWindowSize(platform.window, 1920, 1080);
        GXRenderModeObj movieMode{}; movieMode.fbWidth = 640; movieMode.efbHeight = 456;
        const auto playbackStart = std::chrono::steady_clock::now();
        double worstWork = 0, totalWork = 0;
        unsigned decoded = 0, waiting = 0;
        for (unsigned i = 0; decoded < frames && i < (fullPlayback ? frames+300 : 4000); ++i) {
            const auto tick = std::chrono::steady_clock::now();
            movieAudio.updateNerve(); aurora_update();
            const int result = movieAudio.decode(0);
            if (result == 0) ++decoded;
            else { assert(result == 2 || result == 3); ++waiting; }
            if (fullPlayback) {
                assert(beginHostFrame());
                GXSetCullMode(GX_CULL_NONE);
                GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
                assert(movieAudio.drawCurrentFrame(&movieMode, 0, 44, 640, 368) == int(decoded)-1);
                GXDrawDone(); endHostFrame();
                const double work = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-tick).count();
                totalWork += work; worstWork = std::max(worstWork, work);
                std::this_thread::sleep_until(playbackStart + std::chrono::microseconds(std::llround((i+1)*1000000.0/59.94)));
                if (decoded && decoded % 600 == 0) std::fprintf(stderr, "HD live playback: %u/%u frames\n", decoded, frames);
            } else SDL_Delay(2);
        }
        assert(decoded == frames && !movieAudio.mDvdError);
        assert(bool(galaxy::currentHdMovieFrame(frames-1)) == hd);
        if (fullPlayback) {
            const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now()-playbackStart).count();
            std::fprintf(stderr, "HD live playback: %u frames in %.3f seconds, %.3f FPS, %.3f ms average work, %.3f ms worst, %u waits\n",
                         decoded, seconds, decoded/seconds, totalWork/(decoded+waiting), worstWork, waiting);
            assert(seconds < frames/59.94+1);
        }
        assert(movieAudio.loadStop() && movieAudio.close()); movieAudio.quit();
        output.fill(0x5A5A);
        movieAudio.mixAudio(output.data(), samples);
        assert(std::all_of(output.begin(), output.begin() + samples * 2, [](s16 v) { return v == 0; }));
        assert(!movieAudio.mOpen && !galaxy::currentHdMovieFrame(0));
        delete[] movieMemory;
    }
    assert(galaxy::setHdMoviesEnabled(true));
    delete[] movieAudio.mSoundBuffer[0]; delete[] movieAudio.mSoundBuffer[1];
    for (unsigned i = 0; (game->mSceneController->mSceneInitializeState != SceneInitializeState_End ||
                         std::strcmp(game->mSceneController->mCurrSceneControlInfo.mStage, "FileSelect")) && i < 1200; ++i) {
        aurora_update();
        if (beginHostFrame()) { game->frameLoop(); endHostFrame(); }
        SDL_Delay(2);
    }
    assert(game->mSceneController->mSceneInitializeState == SceneInitializeState_End);
    assert(!std::strcmp(game->mSceneController->mCurrSceneControlInfo.mStage, "FileSelect"));
    {
        auto* scene = game->mSceneController->mScene;
        assert(scene && scene->mSceneObjHolder);
        assert(galaxy::setFpsCounterEnabled(true));
        assert(beginHostFrame());
        game->mSceneController->mScene = nullptr;
        galaxy::drawFpsCounter();
        game->mSceneController->mScene = scene;
        auto* objects = scene->mSceneObjHolder;
        scene->mSceneObjHolder = nullptr;
        galaxy::drawFpsCounter();
        scene->mSceneObjHolder = objects;
        endHostFrame();
        assert(galaxy::setFpsCounterEnabled(false));
        std::fprintf(stderr, "Story check: FPS survives absent scene and scene objects\n");
        auto* logo = static_cast<TitleSequenceProductSub::LogoLayout*>(SingletonHolder<NameObjRegister>::get()->mHolder->find("ロゴ"));
        auto* selector = static_cast<FileSelector*>(SingletonHolder<NameObjRegister>::get()->mHolder->find(MR::getJapaneseObjectName("FileSelector")));
        assert(logo && selector);
        auto* settings = logo->mSettings;
        assert(MR::isDead(settings->buttonLayout));
        auto tick = [&](unsigned buttons, unsigned frames) {
            PADStatus pad{}; pad.button = buttons; pad.substickX = -112; pad.substickY = -99;
            PADSetVirtualStatus(0, &pad);
            for (unsigned frame = 0; frame < frames; ++frame) {
                aurora_update(); assert(beginHostFrame()); game->frameLoop(); endHostFrame(); SDL_Delay(2);
            }
        };
        for (unsigned frame = 0; MR::isDead(settings->buttonLayout) && frame < 600; ++frame) tick(0, 1);
        assert(!MR::isDead(settings->buttonLayout));
        auto* prompt = static_cast<LayoutActor*>(SingletonHolder<NameObjRegister>::get()->mHolder->find("Press[A][B]"));
        assert(prompt && !MR::isDead(prompt));
        assert(settings->buttonPosition.x == 50 && settings->buttonPosition.y == MR::getScreenHeight()-50);
        tick(0, 40); tick(PAD_BUTTON_A, 1); tick(0, 45);
        assert(!MR::isDead(settings) && settings->phase == 1);
        for (auto character : {galaxy::Character::Mario, galaxy::Character::Luigi, galaxy::Character::Peach, galaxy::Character::Daisy}) {
            galaxy::activateCharacter(character); settings->calcAnim();
            const auto color = MR::getPane(settings, "WinWindow")->FindMaterialByName("WinWindowC ", false)->GetTevColor(1);
            assert(color.r == 20 && color.g == 135 && color.b == 200);
        }
        galaxy::activateCharacter(galaxy::Character::Mario);

        settings->kill(); settings->open();
        tick(PAD_BUTTON_A | PAD_BUTTON_B, 5);
        assert(!MR::isDead(settings->buttonLayout) && selector->mTitle->isActive());
        tick(0, 45); tick(PAD_BUTTON_B, 1); tick(0, 2); assert(settings->page==13);
        settings->selected=1; tick(PAD_BUTTON_A,1); tick(0,90);
        assert(MR::isDead(settings) && !galaxy::controllerSettingsOpen());
        tick(PAD_BUTTON_A | PAD_BUTTON_B, 10);
        assert(MR::isDead(settings->buttonLayout));
        for (unsigned frame = 0; selector->mTitle->isActive() && frame < 240; ++frame) tick(0, 1);
        assert(!selector->mTitle->isActive());
        std::fprintf(stderr, "Story check: title cog appears with prompt, pointer/A opens, modal blocks start, B asks to apply/discard and A+B starts even over cog\n");
    }
    {
        auto* holder = GameDataFunction::getCurrentGameDataHolder();
        const std::string savedName = holder->mName;
        std::array<u8,4096> saved{};
        const auto savedSize = holder->makeFileBinary(saved.data(), saved.size());
        for (auto character : {galaxy::Character::Mario, galaxy::Character::Luigi, galaxy::Character::Peach, galaxy::Character::Daisy}) {
            galaxy::activateCharacter(character);
            holder->resetAllData();
            const auto freshSize = holder->makeFileBinary(binary.data(), binary.size());
            assert(holder->loadFromFileBinary(galaxy::marioCharacter(character) ? "mario1" : "luigi1", binary.data(), freshSize));
            StorySequenceExecutor sequence;
            GalaxyMoveArgument move(6, nullptr, 1, nullptr);
            sequence.overwriteGalaxyNameAfterLoading(&move);
            const bool prologue = character != galaxy::Character::Luigi;
            assert(!std::strcmp(move.mStageName, prologue ? "PeachCastleGardenGalaxy" : "HeavensDoorGalaxy"));
            assert((sequence._48.size() > 0) == prologue);
            holder->followStoryEventByName("ピーチ城浮上後");
            StorySequenceExecutor progressed;
            progressed.overwriteGalaxyNameAfterLoading(&move);
            assert(!std::strcmp(move.mStageName, "HeavensDoorGalaxy") && progressed._48.size() == 0);
        }
        assert(holder->loadFromFileBinary(savedName.c_str(), saved.data(), savedSize));
        galaxy::activateCharacter(galaxy::Character::Mario);
        std::fprintf(stderr, "Story check: fresh Daisy prologue, Luigi route and progressed saves passed\n");
    }
    {
        FileSelector selector("Character availability check");
        UserFile file;
        selector.mUserFile = &file;
        assert(!selector.isUserFileAppearLuigi(1));
        file.setCreated();
        assert(selector.isUserFileAppearLuigi(1) && !file.isViewCompleteEnding());
        file.mIsGameDataCorrupted = true; assert(!selector.isUserFileAppearLuigi(1));
        file.mIsGameDataCorrupted = false;
        file.mIsConfigDataCorrupted = true; assert(!selector.isUserFileAppearLuigi(1));
        file.mIsConfigDataCorrupted = false;
        selector.setUserFileMario(1, false); assert(selector.isUserFileLuigi(1));
        selector.setUserFileMario(1, true); assert(!selector.isUserFileLuigi(1));
        CameraViewInterpolator camera;
        TVec3f direction, position(0, 0, 0);
        camera.calcBinder(&direction, position, position);
        assert(camera.mBinder->mRadius > 100);
        camera.mIsFreeLook = true;
        camera.calcBinder(&direction, position, position);
        assert(camera.mBinder->mRadius == 25 && !camera.translateByRepulsion());
        camera.mIsFreeLook = false;
        camera.calcBinder(&direction, position, position);
        assert(camera.mBinder->mRadius > 100);
    }
    {
        auto* picker = new NativeCharacterSelect();
        auto tick = [&](u16 buttons, unsigned frames = 1) {
            PADStatus pad{}; pad.button = buttons; pad.substickX = -127; pad.substickY = 127;
            PADSetVirtualStatus(0, &pad);
            for (unsigned i = 0; i < frames; ++i) {
                aurora_update();
                if (beginHostFrame()) {
                    game->frameLoop(); picker->movement(); picker->calcAnim(); picker->draw(); endHostFrame();
                }
            }
        };
        picker->open(1, true);
        assert(picker->phase == 0);
        for (auto* head : picker->heads) if (head) assert(MR::isDead(head));
        tick(0);
        assert(picker->phase == 0);
        for (auto* head : picker->heads) if (head) assert(MR::isDead(head));
        tick(0, 60);
        assert(picker->phase == 1 && galaxy::characterList().opened);
        {
            std::ofstream capture(std::filesystem::path(argv[3])/"character-lighting.ppm", std::ios::binary);
            capture << "P6\n640 456\n255\n";
            for (u16 y = 0; y < 456; ++y) for (u16 x = 0; x < 640; ++x) {
                u32 color; GXPeekARGB(x, y, &color);
                const char rgb[]{char(color>>16), char(color>>8), char(color)}; capture.write(rgb, 3);
            }
            capture.close(); assert(capture.good());
        }
        for (auto* head : picker->heads) if (head) assert(!MR::isDead(head));
        for (unsigned h : {2u, 3u}) {
            auto* data = picker->heads[h]->mModelManager->getJ3DModelData();
            for (unsigned m = 0; m < data->getMaterialNum(); ++m) {
                auto* block = data->getMaterialNodePointer(m)->mColorBlock;
                const auto color = *block->getMatColor(1);
                assert(color.r == 0 && color.g == 0 && color.b == 0 && color.a == 255);
                assert(!block->getColorChan(2)->getEnable());
            }
        }
        assert(!galaxy::nativeMenuOpen());
        const auto peachFrame = MR::getBckFrame(picker->heads[2]);
        const auto daisyFrame = MR::getBckFrame(picker->heads[3]);
        tick(0, 5);
        assert(MR::getBckFrame(picker->heads[2]) == peachFrame);
        assert(MR::getBckFrame(picker->heads[3]) == daisyFrame);
        assert(picker->heads[2]->mScale.x > picker->heads[0]->mScale.x * 1.3f);
        assert(picker->hosts[1][0][3] - picker->hosts[0][0][3] >= 150);
        assert(picker->hosts[0][1][3] == -MR::getScreenHeight() * 0.5f - 30);
        auto* background = MR::getPane(picker, "PicWindow")->GetMaterial();
        const auto color = background->GetTevColor(1);
        assert(background->GetTextureNum() == 0 && color.r == 255 && color.g == 255 && color.b == 255);
        assert(MR::getPane(picker, "BG")->GetMaterial()->GetTextureNum() > 0);
        auto* border = MR::getPane(picker, "WinWindow")->FindMaterialByName("WinWindowLT", false);
        assert(border->GetTevColor(1).r > border->GetTevColor(1).g);
        tick(PAD_BUTTON_RIGHT); tick(0);
        assert(galaxy::characterList().preview == galaxy::Character::Luigi);
        assert(border->GetTevColor(1).g > border->GetTevColor(1).r);
        tick(PAD_BUTTON_RIGHT); tick(0);
        assert(galaxy::characterList().preview == galaxy::Character::Peach);
        assert(border->GetTevColor(1).r > border->GetTevColor(1).b && border->GetTevColor(1).b > border->GetTevColor(1).g);
        tick(PAD_BUTTON_A);
        assert(picker->phase == 2);
        for (auto* head : picker->heads) if (head) assert(MR::isDead(head));
        tick(0, 60);
        assert(MR::isDead(picker) && galaxy::fileCharacter(1, true) == galaxy::Character::Peach);
        picker->open(1, true); tick(0, 60);
        tick(PAD_BUTTON_RIGHT); tick(0);
        assert(galaxy::characterList().preview == galaxy::Character::Daisy);
        assert(border->GetTevColor(1).r > border->GetTevColor(1).g && border->GetTevColor(1).g > border->GetTevColor(1).b);
        tick(PAD_BUTTON_B); tick(0, 60);
        assert(MR::isDead(picker) && galaxy::fileCharacter(1, true) == galaxy::Character::Peach);
        PADSetVirtualStatus(0, nullptr);
        std::fprintf(stderr, "Story check: native face list navigation, A confirmation, B cancellation passed\n");
    }
    {
        auto* selector=static_cast<FileSelector*>(SingletonHolder<NameObjRegister>::get()->mHolder->find(MR::getJapaneseObjectName("FileSelector")));
        auto* menu=selector->mMultiplayer;assert(menu);
        const auto previousFileCharacter=galaxy::fileCharacter(1,true);
        auto tick=[&](u16 buttons,unsigned frames=1,bool guide=false){PADStatus pad{};pad.button=buttons;pad.extButton=guide?PAD_BUTTON_GUIDE:0;pad.substickX=-127;pad.substickY=127;PADSetVirtualStatus(0,&pad);
            for(unsigned i=0;i<frames;++i){aurora_update();if(beginHostFrame()){game->frameLoop();endHostFrame();}SDL_Delay(2);}};
        for(unsigned channel=0;channel<4;++channel){PADStatus pad{};pad.extButton=PAD_BUTTON_GUIDE;PADSetVirtualStatus(channel,&pad);tick(0,2,channel==0);assert(!game->mHomeButtonLayout->tryCorePadTriggerHome() && !game->mHomeButtonLayout->isActive());PADClearVirtualStatus(channel);}
        menu->updateButton(true,galaxy::Character::Mario);assert(menu->buttonVisible==!galaxy::lan::radminAdapter().ip.empty());
        menu->adapter=galaxy::lan::radminAdapter();assert(!menu->adapter.ip.empty());menu->open(galaxy::Character::Mario);tick(0,60);
        assert(!MR::isDead(menu) && menu->phase==1 && galaxy::nandReadOnly());
        assert(NANDCreate("lobby-must-not-save.dat",0x30,0)==NAND_RESULT_ACCESS);
        menu->selected=0;tick(PAD_BUTTON_A);tick(0,20);assert(menu->lobby.hosting && menu->lobby.selected==0);
        std::array<galaxy::lan::Discovery,3> browsers;std::array<galaxy::lan::Session,3> peers;
        for(unsigned i=0;i<3;++i){assert(browsers[i].open(menu->adapter,{},15));browsers[i].scan(menu->adapter.ip.c_str());}
        for(unsigned frame=0;frame<80;++frame){tick(0);for(auto& b:browsers)b.pump();}
        for(unsigned i=0;i<3;++i){assert(browsers[i].rows().size()==1);const auto& row=browsers[i].rows()[0];assert(peers[i].join(row.ip.c_str(),row.port,{},row.key,i+1));}
        for(unsigned frame=0;frame<150;++frame){for(auto& p:peers)p.pump();tick(0);}
        assert(menu->lobby.session.players()==4);
        const auto world=menu->lobby.session.world();for(unsigned i=0;i<3;++i)assert(world.players[peers[i].slot()].character==i+1);
        assert(menu->portraits->heads[0]->isOpen() && menu->portraits->heads[1]->isOpen());
        {
            std::ofstream capture(std::filesystem::path(argv[3])/"multiplayer-roster.ppm",std::ios::binary);capture<<"P6\n640 456\n255\n";
            for(u16 y=0;y<456;++y)for(u16 x=0;x<640;++x){u32 color;GXPeekARGB(x,y,&color);const char rgb[]{char(color>>16),char(color>>8),char(color)};capture.write(rgb,3);}assert(capture.good());
        }
        for(unsigned character=1;character<4;++character){menu->selected=0;tick(PAD_BUTTON_A);tick(0,25);assert(menu->lobby.selected==character && menu->lobby.session.world().players[0].character==character);}
        tick(PAD_BUTTON_B);tick(0,60);assert(MR::isDead(menu) && !galaxy::nandReadOnly() && !galaxy::controllerSettingsOpen());
        assert(galaxy::fileCharacter(1,true)==previousFileCharacter);
        PADClearAllVirtualStatus();std::fprintf(stderr,"Story check: native four-character lobby, no-save guard, character changes, B leave and all HOME channels passed\n");
    }
    {
        FileSelectIconID icon;
        icon.setFellowID(FileSelectIconID::EFellowID(2));
        auto* selector = static_cast<FileSelector*>(SingletonHolder<NameObjRegister>::get()->mHolder->find(MR::getJapaneseObjectName("FileSelector")));
        assert(selector);
        auto* item = selector->mItems->getMember(0);
        auto* legacy = selector->mItems->getMember(2);
        item->forceChange(icon, false);
        legacy->forceChange(icon, false);
        assert(legacy->fellowModel() == legacy->mModels[2]);
        FileSelectInfo info(11, "Selected file name check");
        info.initWithoutIter();
        u16 oldName[11]{};
        constexpr unsigned indices[]{0, 1, 4, 5};
        struct CheckName : TextBoxRecursiveOperation {
            const wchar_t* expected;
            mutable unsigned count = 0;
            void execute(nw4r::lyt::TextBox* box) const override {
                if (std::wcscmp(box->mTextBuf, expected)) std::fprintf(stderr, "Label mismatch %s: '%ls' expected '%ls', capacity %u\n", box->mName, box->mTextBuf, expected, box->GetStringBufferLength());
                assert(!std::wcscmp(box->mTextBuf, expected)); ++count;
            }
        } check;
        for (unsigned i = 0; i < 4; ++i) {
            const auto character = static_cast<galaxy::Character>(i);
            assert(galaxy::saveFileCharacter(1, character));
            item->appear(); item->control(); item->fellowModel()->calcAnim();
            assert(item->fellowModel() == item->mModels[indices[i]] && !MR::isDead(item->fellowModel()));
            for (unsigned j = 0; j < 6; ++j) if (item->mModels[j] && j != indices[i]) assert(MR::isDead(item->mModels[j]));
            info.setInfo(oldName, 1, 12, 34, galaxy::marioCharacter(character), false, false, L"", L"", -1);
            info.change();
            wchar_t expected[16]{};
            std::swprintf(expected, 16, L"%hs", galaxy::characterName(character));
            check.expected = expected;
            check.count = 0; MR::executeTextBoxRecursive(&info, "FileName", check); assert(check.count);
            info.calcAnim();
            assert(!MR::getPane(&info, "BrosIcon")->IsVisible());
            constexpr GXColorS10 colors[] = {{220,40,40,0}, {35,170,65,0}, {240,105,165,0}, {150,90,45,0}};
            for (const char* pane : {"PicFileInfoL", "PicFileInfoC", "PicFileInfoR"}) {
                const auto tint = MR::getPane(&info, pane)->GetMaterial()->GetTevColor(0);
                assert(tint.r == colors[i].r && tint.g == colors[i].g && tint.b == colors[i].b);
            }
            galaxy::initializeCharacters(hdRoot / "mods");
            item->forceChange(icon, false);
            assert(galaxy::fileCharacter(1, true) == character && item->fellowModel() == item->mModels[indices[i]]);
            info.forceChange();
            check.count = 0; MR::executeTextBoxRecursive(&info, "FileName", check); assert(check.count);
        }
        auto* daisy = item->fellowModel();
        icon.setMiiIndex(0);
        item->forceChange(icon, false);
        assert(!item->isMiiIcon() && item->fellowModel() == daisy);
        for (unsigned i = 0; i < 3; ++i) {
            aurora_update();
            if (beginHostFrame()) { game->frameLoop(); endHostFrame(); }
        }
        assert(MR::isConnectToDrawTemporarily(daisy));
        auto* model = daisy->mModelManager->getJ3DModel();
        for (unsigned i = 0; i < model->getModelData()->getMaterialNum(); ++i)
            assert(bool(model->getMatPacket(i)->getShapePacket()->mFlags & 0x10) == (i >= 6));
        const auto eye = MR::getJointMtx(daisy, "EyePosition");
        assert(std::abs(eye[0][3] - item->_D4[0][3]) < 0.1f);
        assert(eye[1][3] - item->_D4[1][3] > 400 && eye[1][3] - item->_D4[1][3] < 1200);
        item->makeActorDead(); legacy->makeActorDead(); info.kill();
        std::fprintf(stderr, "Story check: all four save heads and file labels update and survive reload; legacy icons preserved\n");
    }
    {
        auto* heaps = SingletonHolder<HeapMemoryWatcher>::get();
        heaps->setCurrentHeapToSceneHeap();
        InformationMessage oneUp; oneUp.initWithoutIter();
        PlayerLeft lives("Chosen character life icon check"); lives.initWithoutIter();
        PlayerMissLeft death("Chosen character death icon check"); death.initWithoutIter();
        std::array<const void*, 4> textures{};
        for (unsigned i = 0; i < 4; ++i) {
            const auto character = static_cast<galaxy::Character>(i);
            galaxy::activateCharacter(character);
            nw4r::ut::Glyph glyph; assert(galaxy::characterLifeGlyph(&glyph));
            textures[i] = glyph.pTexture;
            assert(glyph.texFormat == GX_TF_RGBA8 && glyph.texWidth == 128 && glyph.texHeight == 128);
            const auto* pixels = static_cast<const u8*>(glyph.pTexture);
            assert(pixels[0] == 0 && pixels[30] == 0);
            oneUp.setMessage(MR::getLayoutMessageDirect("InformationObserverOneUp")); oneUp.appear();
            lives.appear(); lives.forceAppear();
            assert(!MR::isDead(&lives));
            death.appear(); death.movement(); death.movement();
            lives.calcAnim(); death.calcAnim();
            for (auto* layout : {static_cast<LayoutActor*>(&lives), static_cast<LayoutActor*>(&death)}) {
                for (const char* name : {"PicMario", "ShaMario", "PicMarioC", "ShaMarioC"}) {
                    auto* picture = static_cast<nw4r::lyt::Picture*>(MR::getPane(layout, name));
                    const auto& texture = picture->GetMaterial()->GetTexture(0);
                    assert(texture.mImage == glyph.pTexture && texture.GetTexelFormat() == GX_TF_RGBA8);
                    const auto uv = picture->mTexCoordAry.GetArray()[0];
                    assert(uv[0].x == 0 && uv[0].y == 0 && uv[3].x == 1 && uv[3].y == 1);
                }
            }
            death.kill();
            for (unsigned frame = 0; frame < 65; ++frame) {
                heaps->setCurrentHeapToSceneHeap();
                aurora_update(); assert(beginHostFrame());
                game->frameLoop();
                heaps->setCurrentHeapToSceneHeap();
                lives.movement(); lives.calcAnim(); lives.draw();
                oneUp.movement(); oneUp.calcAnim(); oneUp.draw();
                MR::setupDrawForNW4RLayout(1.0f, true);
                Mtx identity{{1,0,0,0}, {0,-1,0,0}, {0,0,1,0}};
                GXLoadPosMtxImm(identity, GX_PNMTX0); GXSetCurrentMtx(GX_PNMTX0);
                nw4r::ut::TextWriterBase<wchar_t> writer;
                writer.SetFont(*MR::getPictureFontNW4R()); writer.SetFontSize(48, 48);
                writer.SetCursor(-260, -120); writer.SetupGX();
                const auto advance = writer.Print(L"BCLM", 4);
                assert(std::abs(advance - writer.CalcStringWidth(L"BCLM", 4)) < 0.01f);
                u32 color; GXPeekARGB(0, 0, &color);
                endHostFrame();
            }
            std::ofstream capture(std::filesystem::path(argv[3])/(std::string("life-")+galaxy::characterName(character)+".ppm"), std::ios::binary);
            capture << "P6\n640 456\n255\n";
            for (u16 y = 0; y < 456; ++y) for (u16 x = 0; x < 640; ++x) {
                u32 color; GXPeekARGB(x, y, &color);
                const char rgb[]{char(color>>16), char(color>>8), char(color)}; capture.write(rgb, 3);
            }
            assert(capture.good());
            oneUp.kill(); lives.kill();
        }
        for (unsigned i = 0; i < 4; ++i) for (unsigned j = i+1; j < 4; ++j) assert(textures[i] != textures[j]);
        galaxy::activateCharacter(galaxy::Character::Mario);
        std::fprintf(stderr, "Story check: all four life/death icons, transparent corners, UVs and native counter rendering passed\n");
    }
    for (const char* name : {"FileSelectDataMario", "FileSelectDataLuigi"}) {
        TPos3f host; host.identity();
        FileSelectModel portrait(name, host, "Portrait blink check");
        portrait.makeActorAppeared(); portrait.movement(); portrait.calcAnim();
        auto* player = portrait.mModelManager->mBtpPlayer;
        auto* pattern = static_cast<J3DAnmTexPattern*>(player->mAnmRes);
        assert(pattern && pattern->getUpdateMaterialNum() >= 1 && pattern->isValidUpdateMaterialID(0));
        auto* material = portrait.mModelManager->getJ3DModelData()->getMaterialNodePointer(pattern->getUpdateMaterialID(0));
        assert(material->mMaterialAnm && material->getMaterialAnm() == material->mMaterialAnm);
        const auto openTexture = material->getTexNo(0);
        assert(openTexture == 1);
        portrait.blinkOnce();
        bool closed = false;
        for (unsigned frame = 0; frame < 40; ++frame) {
            portrait.movement(); portrait.calcAnim();
            closed |= material->getTexNo(0) != openTexture;
        }
        assert(closed && portrait.isOpen() && material->getTexNo(0) == openTexture);
        portrait.close();
        for (unsigned frame = 0; frame < 5; ++frame) { portrait.movement(); portrait.calcAnim(); }
        assert(material->getTexNo(0) != openTexture);
        portrait.open(); portrait.movement(); portrait.calcAnim();
        assert(material->getTexNo(0) == openTexture);
        portrait.makeActorDead();
    }
    {
        galaxy::activateCharacter(galaxy::Character::Daisy);
        auto* holder = GameDataFunction::getCurrentGameDataHolder();
        holder->resetAllData();
        const auto freshSize = holder->makeFileBinary(binary.data(), binary.size());
        assert(holder->loadFromFileBinary("luigi1", binary.data(), freshSize));
        holder->followStoryEventByName("ピーチ城浮上後");
        assert(galaxy::setFpsCounterEnabled(true));
        MR::requestChangeStageInGameMoving("HeavensDoorGalaxy", 1, JMapIdInfo(0,0));
        for (unsigned i = 0; i < 1800; ++i) {
            JKRHeap::sCurrentHeap = nullptr;
            aurora_update();
            if (beginHostFrame()) { game->frameLoop(); galaxy::drawFpsCounter(); JKRHeap::sCurrentHeap = nullptr; endHostFrame(); }
            if (!std::strcmp(game->mSceneController->mCurrSceneControlInfo.mStage, "HeavensDoorGalaxy") &&
                game->mSceneController->mSceneInitializeState == SceneInitializeState_End) break;
            SDL_Delay(2);
        }
        assert(!std::strcmp(game->mSceneController->mCurrSceneControlInfo.mStage, "HeavensDoorGalaxy"));
        assert(game->mSceneController->mSceneInitializeState == SceneInitializeState_End);
        assert(galaxy::setFpsCounterEnabled(false));
        std::fprintf(stderr, "Story check: FPS enabled across FileSelect teardown and Gateway scene initialization passed\n");
        // Real Gateway scene: rendering at 240 must not advance simulation four times faster.
        galaxy::resetRenderHistory();
        for(unsigned fps:{60u,120u,240u}) {
            galaxy::FrameTiming timing; const auto before=galaxy::simulationTick();
            for(unsigned render=0;render<fps;++render) {
                aurora_update(); assert(beginHostFrame());
                galaxy::setRenderTiming(true,galaxy::simulationTick(),timing.alpha(),true);
                game->frameLoop(timing.advance(1.0/fps),false); JKRHeap::sCurrentHeap=nullptr; endHostFrame();
            }
            assert(galaxy::simulationTick()-before==60);
            assert(std::isfinite(MR::getPlayerPos()->x) && std::isfinite(MR::getPlayerPos()->y) && std::isfinite(MR::getPlayerPos()->z));
        }
        galaxy::setRenderTiming(false,0,1,false); galaxy::resetRenderHistory();
        auto* actor = MarioAccess::getPlayerActor();
        assert(actor && gIsLuigi);
        SingletonHolder<HeapMemoryWatcher>::get()->setCurrentHeapToSceneHeap();
        const bool upperAnimation = actor->mMarioAnim->_6C; actor->mMarioAnim->_6C = false;
        const auto animationLockA=actor->_482, animationLockB=actor->_483;
        actor->_482=actor->_483=false; actor->mMarioAnim->mXanimePlayer->_80=false;
        for (const char* animation : {"LuigiRun", "LuigiJump", "Wait", "SpinGround"}) {
            assert(actor->mMarioAnim->mXanimePlayer->changeSimpleBck(animation));
            actor->mMarioAnim->mXanimePlayer->changeInterpoleFrame(0);
            TVec3f firstHand; bool movingPose=false;
            for (unsigned frame = 0; frame < unsigned(actor->mMarioAnim->mXanimePlayer->getFrameCtrl()->getEnd()); ++frame) {
                actor->mMarioAnim->mXanimePlayer->getFrameCtrl()->setFrame(frame);
                actor->mMarioAnim->calc();
                const auto hand = MR::getJointMtx(actor,"HandR");
                const TVec3f handPosition(hand[0][3],hand[1][3],hand[2][3]);
                if(frame==0) firstHand=handPosition; else movingPose |= (handPosition-firstHand).length()>0.01f;
                const auto head = MR::getJointMtx(actor, "Head");
                const auto eye = MR::getJointMtx(actor, "EyePosition");
                const TVec3f gap(eye[0][3]-head[0][3], eye[1][3]-head[1][3], eye[2][3]-head[2][3]);
                assert(gap.length() < 0.01f);
                const auto index = MR::getJointIndex(actor, "Head");
                const auto& rest = actor->getModelData()->getJointNodePointer(index)->getTransformInfo().mTranslate;
                const auto& offset = actor->mMarioAnim->mXanimePlayer->mCore->mJointList[index]._28._C;
                assert(std::abs(offset.x-rest.x)<0.001f && std::abs(offset.y-rest.y)<0.001f && std::abs(offset.z-rest.z)<0.001f);
            }
            assert(movingPose);
        }
        std::fprintf(stderr, "Story check: Daisy running, jumping, idle and spin head attachment passed\n");
        {
            auto* model = actor->getJ3DModel();
            const auto joint = model->getModelData()->mJointTree.getDrawMtxIndex(0);
            Mtx base, previous, current; PSMTXCopy(model->getAnmMtx(joint), base);
            const auto tick = galaxy::simulationTick(); galaxy::resetRenderHistory();
            galaxy::setRenderTiming(true,tick,1,true); model->viewCalc3(0,nullptr); PSMTXCopy(model->getDrawMtx(0),previous);
            model->getAnmMtx(joint)[0][3] += 40;
            galaxy::setRenderTiming(true,tick+1,1,true); model->viewCalc3(0,nullptr); PSMTXCopy(model->getDrawMtx(0),current);
            float difference=0; for(unsigned axis=0;axis<3;++axis) difference+=std::abs(current[axis][3]-previous[axis][3]); assert(difference>20);
            for (float fraction : {0.25f,0.5f,0.75f,1.0f}) {
                galaxy::setRenderTiming(true,tick+1,fraction,true); model->viewCalc3(0,nullptr);
                for (unsigned axis=0;axis<3;++axis) assert(std::abs(model->getDrawMtx(0)[axis][3]-(previous[axis][3]+(current[axis][3]-previous[axis][3])*fraction))<0.001f);
            }
            PSMTXCopy(base,model->getAnmMtx(joint)); galaxy::setRenderTiming(false,tick,1,false); galaxy::resetRenderHistory();
            std::fprintf(stderr, "Story check: real player draw matrices contain four distinct interpolated poses per simulation step\n");
        }
        for (const auto& sound : {std::pair{"ルイージ滑り", u32(SE_PM_LV_LUIGI_WALK_SLIP)}, std::pair{"スリップ", u32(SE_PM_LV_SLIP)}}) {
            actor->mMario->playSoundJ(sound.first, -1);
            auto* handle = actor->mSoundObject->getHandleSoundID(sound.second);
            assert(handle && handle->isSoundAttached() && handle->getSound()->hasLifeTime());
            for (unsigned frame=0; frame<12; ++frame) {
                aurora_update(); assert(beginHostFrame()); game->frameLoop(); JKRHeap::sCurrentHeap=nullptr; endHostFrame();
            }
            assert(!actor->mSoundObject->getHandleSoundID(sound.second));
        }
        std::fprintf(stderr, "Story check: Daisy skid/slip audio expires after movement stops\n");
        {
            const TPos3f savedView = MR::getCameraViewMtx();
            const auto tick = galaxy::simulationTick();
            const TVec3f up = actor->mMario->mHeadVec, front = actor->mMario->mFrontVec;
            const TVec3f side = up.cross(front);
            TPos3f view;
            galaxy::setRenderTiming(true,tick,1,false);
            for (const char* animation : {"LuigiRun", "LuigiJump"}) {
                assert(actor->mMarioAnim->mXanimePlayer->changeSimpleBck(animation));
                actor->mMarioAnim->mXanimePlayer->changeInterpoleFrame(0);
                actor->mMarioAnim->mXanimePlayer->getFrameCtrl()->setFrame(10); actor->mMarioAnim->calc();
                const auto head = MR::getJointMtx(actor,"Head");
                const TVec3f target(head[0][3],head[1][3],head[2][3]);
                view.setPositionFromLookAt(target+front*140+side*180,up,target);
                for (unsigned render=0;render<35;++render) {
                    aurora_update(); assert(beginHostFrame()); game->frameLoop(0,false);
                    MR::setCameraViewMtx(view,false,false,TVec3f(0,0,0));
                    MR::loadViewMtx(); MR::loadProjectionMtx(); MR::setDefaultViewportAndScissor();
                    GXSetColorUpdate(GX_TRUE); GXSetAlphaUpdate(GX_TRUE); GXSetDstAlpha(GX_TRUE,0);
                    actor->getJ3DModel()->viewCalc3(0,nullptr); actor->getJ3DModel()->setDrawView(0); actor->getJ3DModel()->directDraw(nullptr);
                    u32 pixel; GXPeekARGB(0,0,&pixel); JKRHeap::sCurrentHeap=nullptr; endHostFrame(); SDL_Delay(2);
                }
                std::ofstream capture(std::filesystem::path(argv[3])/(std::string("neck-")+animation+".ppm"),std::ios::binary);
                capture << "P6\n640 456\n255\n";
                for (u16 y=0;y<456;++y) for (u16 x=0;x<640;++x) {
                    u32 color; GXPeekARGB(x,y,&color); const char rgb[]{char(color>>16),char(color>>8),char(color)}; capture.write(rgb,3);
                }
                assert(capture.good());
            }
            MR::setCameraViewMtx(savedView,false,false,TVec3f(0,0,0)); galaxy::setRenderTiming(false,tick,1,false); galaxy::resetRenderHistory();
        }


        actor->mMarioAnim->_6C = upperAnimation; actor->_482=animationLockA; actor->_483=animationLockB;
        auto* scene = static_cast<GameScene*>(game->mSceneController->mScene);
        auto* pause = scene->mPauseSeq->mPauseMenu;
        assert(pause);
        constexpr GXColorS10 colors[] = {{220,40,40,0}, {35,170,65,0}, {240,105,165,0}, {150,90,45,0}};
        for (unsigned i = 0; i < 4; ++i) {
            const auto character = static_cast<galaxy::Character>(i);
            galaxy::activateCharacter(character); gIsLuigi = !galaxy::marioCharacter(character);
            pause->appear();
            for (unsigned frame = 0; frame < 65; ++frame) {
                JKRHeap::sCurrentHeap = nullptr;
                aurora_update(); assert(beginHostFrame());
                game->frameLoop();
                SingletonHolder<HeapMemoryWatcher>::get()->setCurrentHeapToSceneHeap();
                pause->movement(); pause->calcAnim(); pause->draw();
                for (const char* name : {"PicStagebase", "PicStagebase1", "PicCButton1", "PicCFrame2", "PicCShine1", "PicCLButton"}) {
                    const auto tint = MR::getPane(pause, name)->GetMaterial()->GetTevColor(0);
                    assert(tint.r == colors[i].r && tint.g == colors[i].g && tint.b == colors[i].b);
                }
                const auto title = MR::getPane(pause, "TxtStageTitle")->GetMaterial()->GetTevColor(1);
                assert(title.r == 255 && title.g == 255 && title.b == 255);
                JKRHeap::sCurrentHeap = nullptr;
                endHostFrame();
            }
            std::ofstream capture(std::filesystem::path(argv[3])/(std::string("pause-")+galaxy::characterName(character)+".ppm"), std::ios::binary);
            capture << "P6\n640 456\n255\n";
            for (u16 y = 0; y < 456; ++y) for (u16 x = 0; x < 640; ++x) {
                u32 color; GXPeekARGB(x, y, &color);
                const char rgb[]{char(color>>16), char(color>>8), char(color)}; capture.write(rgb, 3);
            }
            capture.close(); assert(capture.good());
            SingletonHolder<HeapMemoryWatcher>::get()->setCurrentHeapToSceneHeap();
            auto* settings = pause->mSettings;
            assert(settings && MR::isDead(settings));
            auto tickSettings = [&](u16 buttons, unsigned frames = 1, s8 stickY = 0) {
                PADStatus pad{}; pad.button = buttons; pad.substickY = -94;
                if (galaxy::mouseControlsInitialized()) {
                    float x, y; assert(galaxy::mousePointer(x, y));
                    // Verification routes coordinates through virtual PAD; preserve the synthetic mouse position.
                    pad.substickX = std::lround(x*127); pad.substickY = std::lround(-y*127);
                }
                pad.stickY = stickY;
                PADSetVirtualStatus(0, &pad);
                for (unsigned frame = 0; frame < frames; ++frame) {
                    JKRHeap::sCurrentHeap = nullptr;
                    aurora_update(); assert(beginHostFrame()); galaxy::beginMouseFrame(); game->frameLoop();
                    SingletonHolder<HeapMemoryWatcher>::get()->setCurrentHeapToSceneHeap();
                    pause->movement(); pause->calcAnim(); pause->draw();
                    galaxy::drawFpsCounter();
                    JKRHeap::sCurrentHeap = nullptr; endHostFrame();
                    galaxy::applyPendingSettings();
                }
            };
            tickSettings(0, 8);
            const auto* buttonPane = MR::getPane(settings->buttonLayout, "BoxButton1");
            assert(std::abs(buttonPane->mGlbMtx._03) < 1 && buttonPane->mGlbMtx._13 < -155);
            assert(!MR::getPane(settings->buttonLayout, "Txt1")->IsVisible());
            const auto buttonColor = MR::getPane(settings->buttonLayout, "PicCFrame1")->GetMaterial()->GetTevColor(0);
            assert(buttonColor.r == colors[i].r && buttonColor.g == colors[i].g && buttonColor.b == colors[i].b);
            assert(std::abs(settings->buttonPosition.x-MR::getScreenWidth()*0.5f) < 0.01f);
            assert(std::abs(settings->buttonPosition.y-(MR::getScreenHeight()-50)) < 0.01f);
            tickSettings(PAD_BUTTON_A);
            tickSettings(0, 45);
            assert(settings->phase == 1 && galaxy::controllerSettingsOpen() && !galaxy::nativeMenuOpen());
            const auto border = MR::getPane(settings, "WinWindow")->FindMaterialByName("WinWindowLT", false)->GetTevColor(1);
            assert(border.r == colors[i].r && border.g == colors[i].g && border.b == colors[i].b);
            tickSettings(PAD_BUTTON_A); tickSettings(0); assert(settings->page == 1);
            const auto beforeController = galaxy::getControllerSettings();
            tickSettings(PAD_BUTTON_DOWN); tickSettings(0); tickSettings(PAD_BUTTON_A); tickSettings(0);
            assert(settings->pendingController.swapLeft != beforeController.swapLeft);
            assert(galaxy::getControllerSettings()==beforeController);
            settings->pendingController=beforeController;
            tickSettings(PAD_BUTTON_B); tickSettings(0); assert(settings->page == 0);
            tickSettings(PAD_BUTTON_DOWN); tickSettings(0); tickSettings(PAD_BUTTON_A); tickSettings(0);
            assert(settings->page == 2);
            tickSettings(PAD_BUTTON_DOWN); tickSettings(0); tickSettings(PAD_BUTTON_A); tickSettings(0);
            assert(settings->pendingFps && !galaxy::fpsCounterEnabled());
            for (unsigned frame = 0; frame < 3; ++frame) tickSettings(0);
            std::ofstream settingsCapture(std::filesystem::path(argv[3])/(std::string("settings-")+galaxy::characterName(character)+".ppm"), std::ios::binary);
            settingsCapture << "P6\n640 456\n255\n";
            for (u16 y = 0; y < 456; ++y) for (u16 x = 0; x < 640; ++x) {
                u32 color; GXPeekARGB(x, y, &color);
                const char rgb[]{char(color>>16), char(color>>8), char(color)}; settingsCapture.write(rgb, 3);
            }
            settingsCapture.close(); assert(settingsCapture.good());
            tickSettings(PAD_BUTTON_B); tickSettings(0); assert(settings->page == 0);
            tickSettings(PAD_BUTTON_DOWN); tickSettings(0); tickSettings(PAD_BUTTON_DOWN); tickSettings(0);
            tickSettings(PAD_BUTTON_A); tickSettings(0); assert(settings->page == 3);
            const auto textures = galaxy::hdTexturesEnabled();
            tickSettings(PAD_BUTTON_A); tickSettings(0); assert(settings->pendingTextures != textures && galaxy::hdTexturesEnabled()==textures);
            tickSettings(PAD_BUTTON_DOWN); tickSettings(0);
            const auto movies = galaxy::hdMoviesEnabled();
            tickSettings(PAD_BUTTON_A); tickSettings(0); assert(settings->pendingMovies != movies && galaxy::hdMoviesEnabled()==movies);
            tickSettings(PAD_BUTTON_B); tickSettings(0); assert(settings->page == 0);
            for (unsigned down = 0; down < 3; ++down) { tickSettings(PAD_BUTTON_DOWN); tickSettings(0); }
            tickSettings(PAD_BUTTON_A); tickSettings(0); assert(settings->page == 4);
            const auto previousGuide = settings->keyboardGuide;
            tickSettings(PAD_BUTTON_A); tickSettings(0); assert(settings->keyboardGuide != previousGuide);
            for (unsigned down = 0; down < 5; ++down) { tickSettings(PAD_BUTTON_DOWN); tickSettings(0); }
            tickSettings(PAD_BUTTON_A); tickSettings(0); assert(settings->guidePage == 1);
            tickSettings(PAD_BUTTON_B); tickSettings(0); assert(settings->page == 0);
            for(const unsigned newPage : {5u,6u,7u,11u}) {
                settings->page=newPage; settings->selected=0; settings->calcAnim();
                const auto key=newPage==5 ? galaxy::Option::CameraMode : newPage==6 ? galaxy::Option::SoundOutput : newPage==7 ? galaxy::Option::Language : galaxy::Option::RotateLeftStick;
                const auto before=galaxy::option(key); settings->activateRow();
                if(newPage!=6) assert(settings->pendingOptions[unsigned(key)]!=before);
                assert(galaxy::option(key)==before); settings->pendingOptions[unsigned(key)]=before;
                settings->selected=settings->rowCount()-1; assert(settings->firstVisible()+7>=settings->rowCount());
                tickSettings(0); settings->activateRow(); assert(settings->page==0);
            }
            settings->page=2; settings->selected=6;
            settings->pendingOptions[unsigned(galaxy::Option::FrameLimit)]=200; settings->activateRow(); assert(settings->pendingOptions[unsigned(galaxy::Option::FrameLimit)]==240);
            assert(galaxy::option(galaxy::Option::FrameLimit)==60);
            settings->activateRow(-1); assert(settings->pendingOptions[unsigned(galaxy::Option::FrameLimit)]==200);
            for(unsigned bindingPage:{8u,9u,10u}) {
                settings->page=bindingPage; settings->selected=0; settings->activateRow(); assert(galaxy::bindingCaptureActive()); galaxy::cancelBindingCapture();
                settings->selected=settings->rowCount()-1; settings->activateRow(); assert(settings->page==0);
            }
            const auto settingsOnDisk=aurora::io::read_file(std::filesystem::path(platform.userPath)/"galaxy_options.dat");
            tickSettings(PAD_BUTTON_B); tickSettings(0); assert(settings->page==13 && settings->phase==1);
            if(i==0) {
                tickSettings(0,3);
                std::ofstream capture(std::filesystem::path(argv[3])/"settings-exit-prompt.ppm",std::ios::binary);
                capture<<"P6\n640 456\n255\n";
                for(u16 y=0;y<456;++y) for(u16 x=0;x<640;++x) {
                    u32 color; GXPeekARGB(x,y,&color); const char rgb[]{char(color>>16),char(color>>8),char(color)}; capture.write(rgb,3);
                }
                capture.close(); assert(capture.good());
            }
            settings->selected=2; settings->activateRow(); assert(settings->page==0 && settings->pendingFps);
            tickSettings(PAD_BUTTON_B); tickSettings(0); assert(settings->page==13);
            settings->selected=1; tickSettings(PAD_BUTTON_A); tickSettings(0,45);
            assert(MR::isDead(settings) && !galaxy::controllerSettingsOpen() && !MR::isDead(pause));
            assert(!galaxy::fpsCounterEnabled() && galaxy::hdTexturesEnabled()==textures && galaxy::hdMoviesEnabled()==movies);
            assert(galaxy::option(galaxy::Option::FrameLimit)==60);
            assert(aurora::io::read_file(std::filesystem::path(platform.userPath)/"galaxy_options.dat")==settingsOnDisk);
            if(i==0) {
                const auto originalOptions=galaxy::runtimeOptions();
                const auto originalBindings=galaxy::inputBindings();
                const auto* sceneBefore=game->mSceneController->mScene;
                const auto* actorBefore=MarioAccess::getPlayerActor();
                const auto* englishMessages=game->mObjHolder->mMessageHolder->mGameMessageData;
                TalkNodeCtrl cachedDialogue;
                for(unsigned index=0;index<englishMessages->mFlowBlock->mNodeCount;++index) {
                    auto* node=englishMessages->getNode(index);
                    if(node->mNodeType==1) { cachedDialogue.mCurrentNode=node; break; }
                }
                assert(cachedDialogue.mCurrentNode); cachedDialogue.updateMessage();
                for(unsigned pass=0;pass<4;++pass) {
                    settings->open(); tickSettings(0,45);
                    settings->pendingOptions[unsigned(galaxy::Option::Backend)]=pass%2 ? 1 : 2;
                    settings->pendingOptions[unsigned(galaxy::Option::AntiAliasing)]=pass%2 ? 0 : 1;
                    settings->pendingOptions[unsigned(galaxy::Option::Language)]=pass==0 ? 1 : pass==1 ? 2 : 0;
                    settings->pendingOptions[unsigned(galaxy::Option::FrameLimit)]=240;
                    settings->pendingOptions[unsigned(galaxy::Option::FrameMode)]=1;
                    settings->pendingOptions[unsigned(galaxy::Option::Vsync)]=0;
                    settings->pendingFps=true; settings->pendingTextures=pass%2;
                    settings->pendingController.swapLeft=!beforeController.swapLeft;
                    assert(galaxy::setBinding(0,0,SDL_SCANCODE_J));
                    assert(galaxy::bindingCode(0,0)==(pass ? SDL_SCANCODE_J : originalBindings[0][0]));
                    settings->page=13; settings->selected=0;
                    settings->activateRow(); assert(settings->phase==3);
                    assert(aurora_get_backend()!=(pass%2 ? BACKEND_D3D12 : BACKEND_VULKAN));
                    tickSettings(0,45);
                    assert(MR::isDead(settings) && !settings->saveFailed);
                    assert(aurora_get_backend()==(pass%2 ? BACKEND_D3D12 : BACKEND_VULKAN));
                    assert(galaxy::option(galaxy::Option::FrameLimit)==240 && galaxy::fpsCounterEnabled());
                    assert(galaxy::getControllerSettings().swapLeft!=beforeController.swapLeft);
                    assert(galaxy::bindingCode(0,0)==SDL_SCANCODE_J);
                    assert(galaxy::hdTexturesEnabled()==bool(pass%2));
                    assert(game->mSceneController->mScene==sceneBefore && MarioAccess::getPlayerActor()==actorBefore);
                    assert(game->mObjHolder->mMessageHolder->mGameMessageData!=(pass<2 ? englishMessages : nullptr));
                    assert(std::strcmp(MR::getCurrentLanguagePrefix(),pass==0 ? "UsFrench" : pass==1 ? "UsSpanish" : "UsEnglish")==0);
                    checkPlayerNames();
                    cachedDialogue.refreshLanguage();
                    TalkMessageInfo localized;
                    assert(game->mObjHolder->mMessageHolder->mGameMessageData->getMessage(&localized,0,cachedDialogue.mCurrentNode->mIndex));
                    assert(cachedDialogue.mMessageInfo._0==localized._0);
                    assert(aurora::io::read_file(std::filesystem::path(platform.userPath)/"galaxy_options.dat"));
                    for(unsigned frame=0;frame<4;++frame) tickSettings(0);
                    u32 pixel; GXPeekARGB(50,50,&pixel); assert(pixel&0x00ffffff);
                }
                settings->open(); tickSettings(0,45);
                settings->pendingOptions[unsigned(galaxy::Option::AntiAliasing)]=1;
                settings->pendingOptions[unsigned(galaxy::Option::Aspect43)]=1;
                settings->pendingOptions[unsigned(galaxy::Option::SoundOutput)]=1;
                settings->page=13; settings->selected=0; settings->activateRow(); tickSettings(0,45);
                assert(MR::isDead(settings) && !settings->saveFailed && aurora_get_backend()==BACKEND_D3D12);
                assert(MR::getScreenWidth()==608 && galaxy::appliedOption(galaxy::Option::AntiAliasing)==1);
                assert(galaxy::appliedOption(galaxy::Option::SoundOutput)==1);
                assert(game->mSceneController->mScene==sceneBefore && MarioAccess::getPlayerActor()==actorBefore);
                u32 updatedPixel; GXPeekARGB(50,50,&updatedPixel); assert(updatedPixel&0x00ffffff);
                settings->open(); tickSettings(0,45);
                settings->pendingOptions=originalOptions; settings->pendingController=beforeController;
                settings->pendingTextures=textures; settings->pendingMovies=movies; settings->pendingFps=false;
                assert(galaxy::resetInputBindings());
                settings->page=13; settings->selected=0; settings->activateRow(); tickSettings(0,45);
                assert(MR::isDead(settings) && !settings->saveFailed);
                assert(galaxy::runtimeOptions()==originalOptions && galaxy::inputBindings()==originalBindings);
                assert(game->mObjHolder->mMessageHolder->mGameMessageData==englishMessages);
                assert(MR::getScreenWidth()==832);
                const auto blocked=std::filesystem::path(argv[3])/"blocked-apply";
                std::filesystem::create_directories(blocked/"galaxy_options.dat");
                galaxy::initializeRuntimeOptions(blocked); galaxy::applyRuntimeOptions();
                settings->open(); tickSettings(0,45);
                settings->pendingOptions[unsigned(galaxy::Option::CameraMode)]=0;
                settings->pendingOptions[unsigned(galaxy::Option::Backend)]=2;
                settings->pendingOptions[unsigned(galaxy::Option::AntiAliasing)]=1;
                settings->pendingOptions[unsigned(galaxy::Option::SoundOutput)]=1;
                settings->pendingOptions[unsigned(galaxy::Option::Language)]=1;
                settings->pendingController.swapLeft=!beforeController.swapLeft;
                settings->pendingTextures=!textures; settings->pendingMovies=!movies;
                settings->pendingFps=true; assert(galaxy::setBinding(0,0,SDL_SCANCODE_J));
                settings->page=13; settings->selected=0; settings->activateRow(); tickSettings(0,2);
                assert(settings->phase==1 && settings->page==13 && settings->saveFailed);
                assert(galaxy::option(galaxy::Option::CameraMode)==1 && !galaxy::fpsCounterEnabled());
                assert(galaxy::inputBindings()==originalBindings);
                assert(galaxy::getControllerSettings()==beforeController && galaxy::hdTexturesEnabled()==textures && galaxy::hdMoviesEnabled()==movies);
                assert(aurora_get_backend()==BACKEND_D3D12 && galaxy::appliedOption(galaxy::Option::AntiAliasing)==0);
                assert(std::strcmp(MR::getCurrentLanguagePrefix(),"UsEnglish")==0);
                assert(game->mSceneController->mScene==sceneBefore && MarioAccess::getPlayerActor()==actorBefore);
                settings->selected=1; settings->activateRow(); tickSettings(0,45);
                galaxy::initializeRuntimeOptions(platform.userPath); galaxy::applyRuntimeOptions();
                std::fprintf(stderr,"Story check: Apply/Discard/Cancel, failed save rollback, live D3D12/Vulkan/MSAA, languages and current scene preservation passed\n");
            }
            assert(galaxy::setFpsCounterEnabled(false));
            if (i == 3) {
                galaxy::initializeMouseControls(platform.window);
                SDL_Event focus{}; focus.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
                focus.window.windowID = SDL_GetWindowID(platform.window);
                galaxy::handleMouseWindowEvent(focus); galaxy::setMouseContext(false, true);
                settings->open(); tickSettings(0, 45);
                int width, height; assert(SDL_GetWindowSize(platform.window, &width, &height));
                auto click = [&](float x, float y) {
                    SDL_Event event{}; event.type = SDL_EVENT_MOUSE_MOTION;
                    event.motion.x = x; event.motion.y = y;
                    aurora::input::sdl::dispatch(event);
                    event = {}; event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
                    event.button.button = SDL_BUTTON_LEFT; event.button.down = true;
                    aurora::input::sdl::dispatch(event); tickSettings(0);
                    event.type = SDL_EVENT_MOUSE_BUTTON_UP; event.button.down = false;
                    aurora::input::sdl::dispatch(event); tickSettings(0);
                };
                click(width*0.02f, height*0.9f); assert(settings->page == 0);
                click(width*0.5f, height*138.0f/456.0f); assert(settings->page == 2);
                SDL_Event wheel{}; wheel.type=SDL_EVENT_MOUSE_WHEEL; wheel.wheel.y=-8;
                aurora::input::sdl::dispatch(wheel); tickSettings(0);
                assert(settings->selected==8 && settings->firstVisible()==2);
                wheel.wheel.y=100; aurora::input::sdl::dispatch(wheel); tickSettings(0); assert(settings->selected==0);
                tickSettings(0, 1, -100); assert(settings->selected==1);
                tickSettings(0, 30, -100); assert(settings->selected>=4 && settings->selected<=5);
                tickSettings(0); const auto stationary=settings->selected;
                tickSettings(0, 10, -20); assert(settings->selected==stationary);
                tickSettings(0, 1, 100); assert(settings->selected==stationary-1);
                tickSettings(0, 90, -100); assert(settings->selected==settings->rowCount()-1 && settings->firstVisible()==settings->rowCount()-7);
                settings->kill(); galaxy::shutdownMouseControls();
            }
            PADSetVirtualStatus(0, nullptr);
            SingletonHolder<HeapMemoryWatcher>::get()->setCurrentHeapToSceneHeap();
            pause->kill();
            tickSettings(0);
        }
        galaxy::activateCharacter(galaxy::Character::Daisy); gIsLuigi = true;
        std::fprintf(stderr, "Story check: all four native pause settings, pointer/A opening, submenus, modal input, FPS and portrait rendering passed\n");
    }
    {
        auto* controller=game->mSceneController;
        const auto savedInfo=controller->mCurrSceneControlInfo;
        controller->mCurrSceneControlInfo.setStage("EggStarGalaxy");
        controller->setCurrentScenarioNo(1,1);
        auto* select=controller->mScenarioSelectScene;
        std::fprintf(stderr,"Story check: mission regression setup\n");
        SingletonHolder<HeapMemoryWatcher>::get()->setCurrentHeapToSceneHeap();
        select->start(); select->_28=0;
        select->mScenarioLayout->appear();
        for(unsigned frame=0;frame<180;++frame) {
            JKRHeap::sCurrentHeap=nullptr; aurora_update(); assert(beginHostFrame());
            SingletonHolder<HeapMemoryWatcher>::get()->setCurrentHeapToSceneHeap();
            select->update(); select->calcAnim();
            MainLoopFramework::sManager->beginRender(); select->draw(); MainLoopFramework::sManager->endRender();
            JKRHeap::sCurrentHeap=nullptr; endHostFrame();
        }
        struct CountedStar : ScenarioSelectStar {
            unsigned entries=0;
            explicit CountedStar(EffectSystem* effects):ScenarioSelectStar(effects) {}
            void calcViewAndEntry() override { ++entries; ScenarioSelectStar::calcViewAndEntry(); }
        };
        auto* original=select->mScenarioLayout->mStar[0];
        original->kill();
        SingletonHolder<HeapMemoryWatcher>::get()->setCurrentHeapToSceneHeap();
        auto* star=new CountedStar(select->mEffectSystem);
        star->initWithoutIter(); star->setup(1,1,original->mBasePos,0); star->appear();
        select->mScenarioLayout->mStar[0]=star;
        for(unsigned frame=0;frame<180;++frame) { star->movement(); star->calcAnim(); }
        for(unsigned rate:{60u,120u,240u}) {
            galaxy::setRenderTiming(true,galaxy::simulationTick(),0,false);
            select->calcAnim(); star->entries=0;
            for(unsigned frame=0;frame<std::max(2u,rate/60);++frame) {
                JKRHeap::sCurrentHeap=nullptr; aurora_update(); assert(beginHostFrame());
                SingletonHolder<HeapMemoryWatcher>::get()->setCurrentHeapToSceneHeap();
                MainLoopFramework::sManager->beginRender(); select->draw(); MainLoopFramework::sManager->endRender();
                JKRHeap::sCurrentHeap=nullptr; endHostFrame();
                assert(star->entries==frame+1);
            }
        }
        select->suspend(); controller->mCurrSceneControlInfo=savedInfo;
        galaxy::setRenderTiming(false,galaxy::simulationTick(),0,false);
        std::fprintf(stderr,"Story check: mission models persist on consecutive draws at 60/120/240 FPS without advancing simulation\n");
    }
    std::fprintf(stderr, "Story check: progression, save, movie audio boundaries, and portrait blinking passed\n");
    galaxy::shutdownNandWorker(); galaxy::shutdownAlarms(); galaxy::shutdownAudioDevice();
    aurora_dvd_close(); galaxy::shutdownOsServices(); galaxy::shutdownNand(); galaxy::shutdownHdAssets(); aurora_shutdown();
    return 0;
}
