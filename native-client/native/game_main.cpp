#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/Scene/GameScene.hpp"
#include "Game/Scene/GameScenePauseControl.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/AudioLib/AudSceneMgr.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Animation/XanimePlayer.hpp"
#include "JSystem/JUtility/JUTVideo.hpp"
#include "os_services.hpp"
#include "nand.hpp"
#include "crash.hpp"
#include "lifecycle.hpp"
#include "audio_device.hpp"
#include "home_menu.hpp"
#include "pause_settings.hpp"
#include "characters.hpp"
#include "mouse_controls.hpp"
#include "cutscene_skip.hpp"
#include "hd_assets.hpp"
#include "runtime_options.hpp"
#include "frame_timing.hpp"
#include "input_bindings.hpp"
#include "test_checkpoint.hpp"
#include "rom_resources.hpp"
#include "multiplayer_menu.hpp"
#ifdef GALAXY_PORTABLE
#include "rom_import.hpp"
#endif
#include "io.hpp"
#include <chrono>
#include <thread>
#include <SDL3/SDL.h>
#include <revolution/nand.h>
#include "Game/Util/SingletonHolder.hpp"
#include <aurora/aurora.h>
#include <aurora/dvd.h>
#include <JSystem/JKernel/JKRMemArchive.hpp>
#include <aurora/event.h>
#include <aurora/gfx.h>
#include <aurora/main.h>
#include <dolphin/pad.h>
extern "C" const u8* DVDGetDOLLocation(s32*);
#include <dolphin/gx/GXAurora.h>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <array>
#include <cstring>
#include <optional>

namespace {
std::optional<galaxy::TestCheckpoint> pendingCheckpoint;
bool checkpointStarted = false;
unsigned checkpointWait = 0;
void updateVerificationCheckpoint(GameSystem* game, unsigned updates) {
    if (!pendingCheckpoint) return;
    auto* controller = game->mSceneController;
    if (!checkpointStarted) {
        if (!controller->mScenarioParser || controller->mSceneInitializeState != SceneInitializeState_End) return;
        if (!galaxy::startTestCheckpoint(game, *pendingCheckpoint)) throw std::runtime_error("Cannot start testing checkpoint");
        checkpointStarted = true; checkpointWait = 0;
    } else if (controller->mSceneInitializeState == SceneInitializeState_End &&
               !std::strcmp(controller->mCurrSceneControlInfo.mStage, pendingCheckpoint->stage.data())) {
        checkpointWait += updates;
        if (checkpointWait > 3600) throw std::runtime_error("Testing checkpoint scene did not become playable");
        // Scene setup can start its opening demo on the first update after placement.
        if (checkpointWait >= 300 && galaxy::restoreTestCheckpoint(game, *pendingCheckpoint)) {
            pendingCheckpoint.reset(); checkpointStarted = false;
            std::fprintf(stdout, "[test checkpoint] restored\n"); std::fflush(stdout);
        }
    }
}
void logMessage(AuroraLogLevel level, const char* module, const char* message, unsigned int length) {
    FILE* output = level >= LOG_ERROR ? stderr : stdout;
    std::fprintf(output, "[%s] %.*s\n", module, int(length), message);
    std::fflush(output);
    if (level == LOG_FATAL) { galaxy::printStackTrace(); std::abort(); }
}
void captureVerificationFrame(const char* path, unsigned frame, GameSystem* game) {
    const bool readback = !std::getenv("GALAXY_VERIFY_NO_READBACK");
    const bool metrics = std::getenv("GALAXY_VERIFY_METRICS");
    u32 color; if (readback) GXPeekARGB(0, 0, &color);
    const auto& scene = game->mSceneController->mCurrSceneControlInfo;
    if (!metrics && !(frame % 15) && game->mSceneController->mSceneInitializeState == 6 &&
        !std::strcmp(scene.mScene, "Game") && std::strcmp(scene.mStage, "FileSelect")) {
        const auto& pos = *MR::getPlayerPos(); const auto& velocity = *MR::getPlayerVelocity();
        std::fprintf(stdout, "Player frame %u: pos=(%g,%g,%g) velocity=(%g,%g,%g) ground=%d jump=%d spin=%d character=%s voice=%s\n",
                     frame, pos.x, pos.y, pos.z, velocity.x, velocity.y, velocity.z,
                     MR::isOnGroundPlayer(), MR::isPlayerJumpRising(), MR::isPlayerSwingAction(),
                     galaxy::characterName(galaxy::activeCharacter()), AudWrap::getSceneMgr()->isPlayerModeLuigi() ? "Luigi-family" : "Mario-family");
        std::fflush(stdout);
        const auto* actor = MarioAccess::getPlayerActor();
        if (actor && actor->mMarioAnim && actor->mMarioAnim->mXanimePlayer) {
            auto* animation = actor->mMarioAnim->mXanimePlayer;
            const auto* control = animation->getFrameCtrl();
            const auto* name = animation->getCurrentBckName();
            std::fprintf(stdout, "Animation frame %u: name=%s frame=%g rate=%g end=%d loop=%u\n",
                         frame, name ? name : "none", control->mFrame, control->mRate, control->mEnd, control->mAttribute);
        }
    }
    if (frame % 120) return;
    if (MR::isMoviePlayerPlaying())
        std::fprintf(stdout, "Movie playback: frame %d/%u, %.2f rendered FPS\n",
                     MR::getMovieCurrentFrame(), MR::getMovieTotalFrame(), aurora_get_fps());
    std::fprintf(stdout, "Verify frame %u: scene=%s stage=%s state=%d\n", frame, scene.mScene, scene.mStage,
                 int(game->mSceneController->mSceneInitializeState)); std::fflush(stdout);
    if (!readback) return;
    auto* video = JUTVideo::getManager(); const auto width = video->getFbWidth(), height = video->getEfbHeight();
    if (!width || !height || width > 1024 || height > 1024) throw std::runtime_error("Invalid verification framebuffer extent");
    auto file = aurora::io::open_atomic_file(path);
    if (!file) throw std::runtime_error("Cannot write verification frame");
    char header[64]; const auto headerSize = std::snprintf(header, sizeof(header), "P6\n%u %u\n255\n", unsigned(width), unsigned(height));
    std::array<u8, 1024 * 3> row;
    bool failed = SDL_WriteIO(file.get(), header, headerSize) != std::size_t(headerSize);
    for (u16 y = 0; y < height; ++y) {
        for (u16 x = 0; x < width; ++x) {
            GXPeekARGB(x, y, &color); const auto offset = std::size_t(x) * 3;
            row[offset] = color >> 16; row[offset + 1] = color >> 8; row[offset + 2] = color;
        }
        failed |= SDL_WriteIO(file.get(), row.data(), width * 3) != width * 3;
    }
    if (failed || !file.commit()) throw std::runtime_error("Cannot finish verification frame");
}
void readVerificationPad(const char* path, GameSystem* game) {
    auto* file = std::fopen(path, "r"); if (!file) return;
    char line[128];
    const auto* read = std::fgets(line, sizeof(line), file); std::fclose(file);
    if (!read) return;
    if (!std::strcmp(line, "quit\n")) {
        SDL_Event event{}; event.type = SDL_EVENT_QUIT; SDL_PushEvent(&event); return;
    }
    static bool checkpointCommand = false;
    const bool save = !std::strcmp(line, "checkpoint-save\n");
    const bool load = !std::strcmp(line, "checkpoint-load\n");
    if (save || load) {
        if (!std::getenv("GALAXY_VERIFY_NAND") || !*std::getenv("GALAXY_VERIFY_NAND")) return;
        if (!checkpointCommand) {
            const auto destination = std::filesystem::path(std::string(path) + ".checkpoint");
            galaxy::TestCheckpoint c;
            const bool ok = save ? galaxy::captureTestCheckpoint(game, &c) && galaxy::writeTestCheckpoint(destination, c) :
                galaxy::readTestCheckpoint(destination, &c);
            if (ok && load) { pendingCheckpoint = c; checkpointStarted = false; checkpointWait = 0; }
            std::fprintf(stdout, "[test checkpoint] %s %s\n", save ? "save" : "load", ok ? (save ? "passed" : "queued") : "rejected");
            std::fflush(stdout);
        }
        checkpointCommand = true; PADSetVirtualStatus(0, nullptr); return;
    }
    checkpointCommand = false;
    unsigned buttons; int x, y, px, py;
    const auto fields = std::sscanf(line, "%u %d %d %d %d", &buttons, &x, &y, &px, &py);
    if (fields != 5 || buttons > 65535 || x < -127 || x > 127 || y < -127 || y > 127 ||
        px < -127 || px > 127 || py < -127 || py > 127) return;
    PADStatus status{}; status.button = buttons; status.stickX = x; status.stickY = y;
    status.substickX = px; status.substickY = py; status.err = PAD_ERR_NONE; PADSetVirtualStatus(0, &status);
}
}

int main(int argc, char** argv) {
    galaxy::installCrashHandler();
#ifdef GALAXY_PORTABLE
    const auto base=std::filesystem::u8path(SDL_GetBasePath());
    const auto data=base/"game-data";
    try {
        if(argc>=3 && !std::strcmp(argv[1],"--import-rom")) {
            std::atomic<unsigned> progress{0};
            galaxy::importRom(std::filesystem::u8path(argv[2]),argc>=4?std::filesystem::u8path(argv[3]):data,progress);
            return 0;
        }
        auto imported=galaxy::readImportedGame(data);
        if(imported.rom.empty()) {
            if(!galaxy::showRomBuilder(data)) return 0;
            imported=galaxy::readImportedGame(data);
        }
        const auto romUtf8=imported.rom.u8string(),filesUtf8=imported.files.u8string(),modsUtf8=(base/"mods").u8string();
        const std::string ownedRom(romUtf8.begin(),romUtf8.end()),ownedFiles(filesUtf8.begin(),filesUtf8.end()),ownedMods(modsUtf8.begin(),modsUtf8.end());
#define GALAXY_ROM_PATH ownedRom.c_str()
#define GALAXY_RESOURCE_ROOT ownedFiles.c_str()
#define GALAXY_MOD_ROOT ownedMods.c_str()
        galaxy::setNandRoot(base/"userdata/nand");
#endif
    SDL_SetHint(SDL_HINT_XINPUT_ENABLED, "1");
    AuroraConfig config{};
    config.appName = "Super Mario Galaxy";
    config.logCallback = logMessage;
    char* preferences = SDL_GetPrefPath(nullptr, config.appName);
    const char* testPreferences = std::getenv("GALAXY_VERIFY_CAPTURE") && std::getenv("GALAXY_VERIFY_INPUT") ? std::getenv("GALAXY_VERIFY_USER_PATH") : nullptr;
    galaxy::initializeRuntimeOptions(testPreferences ? testPreferences : preferences ? preferences : "");
    config.userPath = config.cachePath = testPreferences;
    config.vsync = galaxy::option(galaxy::Option::Vsync);
    config.msaa = galaxy::option(galaxy::Option::AntiAliasing) ? 4 : 1;
    const AuroraBackend backends[]{BACKEND_AUTO, BACKEND_D3D12, BACKEND_VULKAN};
    config.desiredBackend = backends[galaxy::option(galaxy::Option::Backend)];
    config.allowJoystickBackgroundEvents = galaxy::option(galaxy::Option::BackgroundGamepad);
    SDL_free(preferences);
    config.mem2Size = 64 * 1024 * 1024;
    config.windowPosX = config.windowPosY = SDL_WINDOWPOS_CENTERED;
    config.windowWidth = 1280;
    config.windowHeight = 720;
    const auto platform = aurora_initialize(argc, argv, &config);
    // Galaxy's Wii framebuffer is anamorphic; the native window already uses its widescreen aspect.
    galaxy::applyRuntimeOptions();
    int result = 0;
    bool restarting = false;
    bool frameActive = false;
    const char* capturePath = std::getenv("GALAXY_VERIFY_CAPTURE"); unsigned verificationFrame = 0;
    const char* inputPath = capturePath ? std::getenv("GALAXY_VERIFY_INPUT") : nullptr;
    try {
        if (!galaxy::initializeDisplaySettings(platform.window, platform.userPath))
            std::fprintf(stderr, "Could not restore display settings: %s\n", SDL_GetError());
        int windowX, windowY;
        if (SDL_GetWindowPosition(platform.window, &windowX, &windowY)) {
            const auto size = aurora_get_window_size();
            std::fprintf(stdout, "[native display] %ux%u %s at (%d,%d)\n", size.native_fb_width, size.native_fb_height,
                galaxy::borderlessFullscreen() ? "borderless fullscreen" : "windowed", windowX, windowY);
        }
        if (!aurora_dvd_open(GALAXY_ROM_PATH)) throw std::runtime_error("Could not open the owned Galaxy disc image");
        s32 dolSize=0;const auto* dol=DVDGetDOLLocation(&dolSize);
        if(!dol || dolSize<=0) throw std::runtime_error("Cannot validate the game executable");
        galaxy::initializeRomResources({dol,std::size_t(dolSize)});
        JKRMemArchive::setNativeResourceRoot(GALAXY_RESOURCE_ROOT);
        galaxy::initializeHdAssets(GALAXY_MOD_ROOT, platform.userPath);
        if (capturePath) if (const char* nand = std::getenv("GALAXY_VERIFY_NAND")) galaxy::setNandRoot(nand);
        if (NANDInit() != NAND_RESULT_OK) throw std::runtime_error("Could not initialize native save storage");
        galaxy::initializeCharacters(GALAXY_MOD_ROOT);
        galaxy::initializeCharacterOverlays(GALAXY_MOD_ROOT);
        galaxy::initializeMultiplayer(std::filesystem::path(GALAXY_MOD_ROOT)/"characters/peach-daisy");
        initializeGame();
        const char* checkpointPath = inputPath ? std::getenv("GALAXY_VERIFY_CHECKPOINT") : nullptr;
        if (checkpointPath) {
            if (!std::getenv("GALAXY_VERIFY_NAND") || !*std::getenv("GALAXY_VERIFY_NAND"))
                throw std::runtime_error("Testing checkpoints require separate save storage");
            galaxy::TestCheckpoint c;
            if (!galaxy::readTestCheckpoint(checkpointPath, &c)) throw std::runtime_error("Invalid testing checkpoint");
            pendingCheckpoint = c;
        }
        if (!inputPath) galaxy::initializeMouseControls(platform.window);
        auto* gameHeap = JKRHeap::sCurrentHeap;
        bool paused = false;
        bool closeRequested = false;
        using Clock = std::chrono::steady_clock;
        galaxy::FrameTiming timing;
        auto lastUpdate = Clock::now(), nextFrame = lastUpdate;
        bool previousIndependent = false;
        const void* previousScene = nullptr;
        unsigned pendingUpdates=0, metricFrames=0;
        auto metricStart=lastUpdate; uint64_t metricTick=0;
        const bool metrics=capturePath && inputPath && std::getenv("GALAXY_VERIFY_METRICS");
        galaxy::measureFrameWork=metrics;
        double metricUpdate=0, metricDraw=0, metricBegin=0, metricEnd=0;
        if(const auto* mode=SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(platform.window)))
            std::fprintf(stdout,"[native display] refresh %.2f Hz, rendering limit %u FPS\n",mode->refresh_rate,galaxy::option(galaxy::Option::FrameLimit));
        while (true) {
            JKRHeap::sCurrentHeap=nullptr;
            if(galaxy::applyPendingSettings()) { lastUpdate=nextFrame=Clock::now(); timing.remainder=0; pendingUpdates=0; }
            const bool independent = galaxy::option(galaxy::Option::FrameMode);
            const unsigned limit = independent ? galaxy::option(galaxy::Option::FrameLimit) : 60;
            if (limit) {
                nextFrame += std::chrono::nanoseconds(1000000000/limit);
                if (nextFrame < Clock::now()) nextFrame = Clock::now();
                const auto delay=std::chrono::duration_cast<std::chrono::nanoseconds>(nextFrame-Clock::now()).count();
                if(delay>0) SDL_DelayPrecise(Uint64(delay));
            } else nextFrame = Clock::now();
            galaxy::checkAudioDevice();
            const auto* game = SingletonHolder<GameSystem>::get();
            const auto& scene = game->mSceneController->mCurrSceneControlInfo;
            bool pointerMenu = closeRequested || !game->mSceneController->mScene ||
                game->mSceneController->mSceneInitializeState != SceneInitializeState_End ||
                std::strcmp(scene.mScene, "Game") || !std::strcmp(scene.mStage, "FileSelect");
            if (!pointerMenu) {
                const auto* gameScene = static_cast<GameScene*>(game->mSceneController->mScene);
                pointerMenu = gameScene->isExecScenarioStarter() || (gameScene->mPauseCtrl &&
                    gameScene->isNerve(gameScene->mPauseCtrl->mPauseMenuNerve));
            }
            galaxy::setMouseContext(galaxy::nativeMenuOpen(), pointerMenu);
            const AuroraEvent* events = aurora_update();
            for (const AuroraEvent* event = events; event && event->type != AURORA_NONE; ++event) {
                if (event->type == AURORA_SDL_EVENT) {
                    if(event->sdl.type==SDL_EVENT_KEY_DOWN && event->sdl.key.scancode==SDL_SCANCODE_ESCAPE && galaxy::takeBindingEscape()) continue;
                    galaxy::handleControllerSettingsEvent(event->sdl);
                    galaxy::handleMouseWindowEvent(event->sdl);
                    if (event->sdl.type == SDL_EVENT_WINDOW_FOCUS_LOST) galaxy::setAudioFocus(false);
                    if (event->sdl.type == SDL_EVENT_WINDOW_FOCUS_GAINED) galaxy::setAudioFocus(true);
                }
                if (event->type == AURORA_EXIT && !closeRequested) {
                    closeRequested = true;
                    SDL_RestoreWindow(platform.window);
                    galaxy::requestPowerOff();
                }
                else if (event->type == AURORA_PAUSED) paused = true;
                else if (event->type == AURORA_UNPAUSED) paused = false;
            }
            const auto now = Clock::now();
            if (independent != previousIndependent || game->mSceneController->mScene != previousScene || paused) {
                timing.remainder = 0; pendingUpdates=0; lastUpdate = now; galaxy::resetRenderHistory();
            }
            previousIndependent = independent; previousScene = game->mSceneController->mScene;
            pendingUpdates=std::min(15u,pendingUpdates+(independent ? timing.advance(std::chrono::duration<double>(now-lastUpdate).count()) : 1));
            lastUpdate = now;
            galaxy::setRenderTiming(independent, galaxy::simulationTick(), timing.alpha(), galaxy::option(galaxy::Option::Interpolation));
            if (paused && !closeRequested) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); continue; }
            const auto beginStart=metrics ? Clock::now() : Clock::time_point{};
            if ((!paused || closeRequested) && aurora_begin_frame()) {
                if(metrics) metricBegin+=std::chrono::duration<double,std::milli>(Clock::now()-beginStart).count();
                frameActive = true;
                JKRHeap::sCurrentHeap=gameHeap;
                if (inputPath) readVerificationPad(inputPath, SingletonHolder<GameSystem>::get());
                if (!independent) galaxy::beginMouseFrame();
                JKRHeap::sCurrentHeap=gameHeap;
                SingletonHolder<GameSystem>::get()->frameLoop(pendingUpdates, !independent);
                if (inputPath) updateVerificationCheckpoint(SingletonHolder<GameSystem>::get(), pendingUpdates);
                gameHeap=JKRHeap::sCurrentHeap;
                pendingUpdates=0;
                JKRHeap::sCurrentHeap=nullptr;
                galaxy::drawCutsceneSkipPrompt();
                galaxy::drawFpsCounter();
                if (capturePath && !closeRequested) captureVerificationFrame(capturePath, ++verificationFrame, SingletonHolder<GameSystem>::get());
const auto endStart=metrics ? Clock::now() : Clock::time_point{};
                aurora_end_frame();
                if(metrics) { metricEnd+=std::chrono::duration<double,std::milli>(Clock::now()-endStart).count(); metricUpdate+=galaxy::frameWork.updateMs; metricDraw+=galaxy::frameWork.drawMs; }
                frameActive = false;
                if(metrics) {
                    ++metricFrames; const auto measured=Clock::now(); const double seconds=std::chrono::duration<double>(measured-metricStart).count();
                    if(seconds>=5) {
                        const auto* current=SingletonHolder<GameSystem>::get();
                        std::fprintf(stdout,"[native performance] scene=%s stage=%s render=%.2f submitted=%.2f simulation=%.2f Hz mode=%u limit=%u interpolation=%u\n",current->mSceneController->mCurrSceneControlInfo.mScene,current->mSceneController->mCurrSceneControlInfo.mStage,aurora_get_fps(),metricFrames/seconds,(galaxy::simulationTick()-metricTick)/seconds,galaxy::option(galaxy::Option::FrameMode),galaxy::option(galaxy::Option::FrameLimit),galaxy::option(galaxy::Option::Interpolation));
                        std::fprintf(stdout,"[native frame work] begin=%.3f update=%.3f draw=%.3f submit=%.3f ms per rendered frame\n",metricBegin/metricFrames,metricUpdate/metricFrames,metricDraw/metricFrames,metricEnd/metricFrames);
                        metricBegin=metricUpdate=metricDraw=metricEnd=0;
                        std::fflush(stdout); metricFrames=0; metricTick=galaxy::simulationTick(); metricStart=measured;
                    }
                }
            }
        }
    } catch (const galaxy::ApplicationExit& request) {
        restarting = request.restart;
        if (frameActive) aurora_end_frame();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Game failed: %s\n", error.what());
#ifdef GALAXY_PORTABLE
        if(!capturePath) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Galaxy PC",error.what(),platform.window);
#endif
        result = 1;
    }
    try {
        galaxy::shutdownNandWorker();
        galaxy::shutdownAlarms();
        galaxy::shutdownAudioDevice();
        aurora_dvd_close();
        galaxy::shutdownOsServices();
        galaxy::shutdownNand();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Game services could not stop safely: %s\n", error.what());
        // Keep the platform alive if a worker might still be using it.
        return 1;
    }
    galaxy::shutdownHdAssets();
    galaxy::shutdownMouseControls();
    aurora_shutdown();
    if (restarting) {
        try { galaxy::restartApplication(); }
        catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); return 1; }
    }
    return result;
#ifdef GALAXY_PORTABLE
    } catch(const std::exception& error) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Galaxy PC",error.what(),nullptr);
        std::fprintf(stderr,"%s\n",error.what());return 1;
    }
#endif
}
