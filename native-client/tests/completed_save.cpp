#include "characters.hpp"
#include "runtime_options.hpp"
#include "hd_assets.hpp"
#include "home_menu.hpp"
#include "nand.hpp"
#include "os_services.hpp"
#include "audio_device.hpp"
#include "crash.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/System/GameSequenceDirector.hpp"
#include "Game/System/SaveDataHandleSequence.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/System/GameDataGalaxyStorage.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/System/UserFile.hpp"
#include "Game/System/ConfigDataHolder.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "JSystem/JKernel/JKRMemArchive.hpp"
#include <aurora/aurora.h>
#include <aurora/main.h>
#include <aurora/dvd.h>
#include <revolution/nand.h>
#include <SDL3/SDL.h>
#include <cassert>
#include <cstdio>
#include <cstring>

// Private supplied save: run explicitly with ROM, resource root, fresh test NAND, native save.
int main(int argc, char** argv) {
    assert(argc == 5); galaxy::installCrashHandler();
    const auto nand = std::filesystem::path(argv[3]);
    assert(!std::filesystem::exists(nand));
    const auto data = nand/"title/00010000/524d4745/data";
    std::filesystem::create_directories(data);
    std::filesystem::copy_file(argv[4], data/"GameData.bin");
    AuroraConfig config{}; config.appName="Completed save validation";
    config.userPath=config.cachePath=argv[3]; config.mem2Size=64*1024*1024;
    galaxy::initializeRuntimeOptions(argv[3]);
    const auto platform=aurora_initialize(argc,argv,&config); SDL_SetWindowOpacity(platform.window,0);
    assert(galaxy::initializeDisplaySettings(platform.window,argv[3]));
    assert(aurora_dvd_open(argv[1])); JKRMemArchive::setNativeResourceRoot(argv[2]);
    galaxy::setNandRoot(nand); assert(NANDInit()==NAND_RESULT_OK);
    auto root=std::filesystem::path(argv[2]); for(unsigned i=0;i<4;++i) root=root.parent_path();
    galaxy::initializeHdAssets(root/"mods",argv[3]); galaxy::initializeCharacters(root/"mods");
    galaxy::initializeCharacterOverlays(root/"mods");
    initializeGame(); auto* game=SingletonHolder<GameSystem>::get();
    for(unsigned i=0;i<1800 && (game->mSceneController->mSceneInitializeState!=SceneInitializeState_End ||
        std::strcmp(game->mSceneController->mCurrSceneControlInfo.mStage,"FileSelect"));++i) {
        aurora_update(); if(aurora_begin_frame()) { game->frameLoop(); aurora_end_frame(); } SDL_Delay(1);
    }
    assert(game->mSceneController->mSceneInitializeState==SceneInitializeState_End);
    auto* saves=game->mSequenceDirector->mSaveDataHandleSequence; assert(saves->isInitializedGameDataHolder());
    unsigned failures=0;
    auto check=[&](const char* name, bool passed) {
        std::printf("%s %s\n",passed ? "PASS" : "FAIL",name); failures += !passed;
    };
    for(unsigned i=0;i<4;++i) {
        const auto character=galaxy::Character(i); galaxy::activateCharacter(character);
        UserFile user; saves->restoreUserFile(&user,1,galaxy::marioCharacter(character));
        auto* holder=user.mGameDataHolder;
        std::printf("CHARACTER %s family=%s\n",galaxy::characterName(character),user.getGameDataName());
        check("valid game/config data", !user.mIsGameDataCorrupted && !user.mIsConfigDataCorrupted && user.isCreated());
        check("121 actual stars",holder->calcCurrentPowerStarNum()==121);
        check("both complete endings",user.isOnCompleteEndingMarioAndLuigi());
        check("normal ending",user.isViewNormalEnding()); check("complete ending",user.isViewCompleteEnding());
        check("Grand Finale star",user.isPowerStarGetFinalChallengeGalaxy());
        check("Grand Finale unlocked",holder->isOnGameEventFlag("AppearPeachCastleFinalGalaxy"));
        check("all seven Grand Stars / six powered domes",GameDataFunction::calcGrandStarNum(holder)==7);
        for(int j=0;j<holder->mAllGalaxyStorage->getGalaxyNum();++j) {
            const auto* g=holder->mAllGalaxyStorage->getGalaxyStorage(j);
            std::printf("GALAXY %s stars=%d/%d visible=%d\n",g->mGalaxyName,g->getPowerStarNumOwned(),g->mPowerStarNum,holder->isAppearGalaxy(g->mGalaxyName));
            check(g->mGalaxyName,g->getPowerStarNumOwned()==g->mPowerStarNum && holder->isAppearGalaxy(g->mGalaxyName));
        }
        for(int j=8;j<15;++j) {
            std::printf("LUMA %d fed=%d required=%d\n",j,holder->getStarPieceNumGivingToTicoSeed(j),holder->getStarPieceNumMaxGivingToTicoSeed(j));
            check("observatory Hungry Luma fully fed",holder->getStarPieceNumGivingToTicoSeed(j)==holder->getStarPieceNumMaxGivingToTicoSeed(j));
        }
        for(const char* flag : {"SpecialStarGreenAll","SpecialStarRed1","SpecialStarFindingLuigi1","SpecialStarFindingLuigi2","SpecialStarFindingLuigi3","RosettaTalkAfterNormalEnding"})
            check(flag,holder->isOnGameEventFlag(flag));
        check("all nine storybook chapters unlocked",holder->getPictureBookChapterCanRead()==9);
        check("all nine storybook chapters read",holder->getPictureBookChapterAlreadyRead()==9);
        check("observatory warp paths activated",holder->getGameEventValue("WarpPodSaveBits")==3);
        check("permanent watermelon reward",holder->isOnGameEventFlag("StarPieceCounterStop"));
        check("9999 stocked Star Bits",holder->getStockedStarPieceNum()==9999);
        for(const char* race : {"ペンギンレース[オーシャンリング]","テレサレース[ファントム]","テレサレース[デスプロムナード]","サーフィン[トライアル]","サーフィン[チャレンジ]"}) {
            std::printf("RACE %s ticks=%u\n",race,holder->getRaceBestTime(race));
            check("saved race record under 90 seconds",holder->getRaceBestTime(race)>0 && holder->getRaceBestTime(race)<90*60);
            const auto previous=holder->getRaceBestTime(race);
            holder->setRaceBestTime(race,0x12345678);
            check("race record high/low round trip",holder->getRaceBestTime(race)==0x12345678);
            holder->setRaceBestTime(race,previous);
        }
        std::printf("EXTRAS storybookRead=%d warpPods=%d watermelons=%d currentStarBits=%d\n",holder->getPictureBookChapterAlreadyRead(),holder->getGameEventValue("WarpPodSaveBits"),holder->isOnGameEventFlag("StarPieceCounterStop"),holder->getStockedStarPieceNum());
        check("character choice persists",galaxy::saveFileCharacter(1,character));
        galaxy::initializeCharacters(root/"mods");
        check("character choice reloads",galaxy::fileCharacter(1,!galaxy::marioCharacter(character))==character);
    }
    std::printf("Completed-save failures: %u\n",failures);
    galaxy::shutdownNandWorker(); galaxy::shutdownAlarms(); galaxy::shutdownAudioDevice(); aurora_dvd_close();
    galaxy::shutdownOsServices(); galaxy::shutdownNand(); galaxy::shutdownHdAssets(); aurora_shutdown();
    return failures ? 1 : 0;
}
