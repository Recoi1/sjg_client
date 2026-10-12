#include "test_checkpoint.hpp"
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
#include "Game/System/GameDataFunction.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/MapObj/DynamicCollisionObj.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Scene/GameScene.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/Map/KCollision.hpp"
#include "JSystem/JKernel/JKRMemArchive.hpp"
#include "JSystem/JKernel/JKRHeap.hpp"
#include <aurora/aurora.h>
#include <aurora/main.h>
#include <aurora/dvd.h>
#include <revolution/nand.h>
#include <dolphin/pad.h>
#include <SDL3/SDL.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <fstream>

int main(int argc, char** argv) {
    assert(argc == 4); galaxy::installCrashHandler();
    for (unsigned alias=0; alias<3; ++alias) {
        TVec3f from(1,2,3), to(5,-2,7), result;
        auto* out=alias==1 ? &from : alias==2 ? &to : &result;
        MR::vecBlend(from,to,out,0.25f);
        assert(out->x==2 && out->y==1 && out->z==4);
    }
    TVec3f add(1,2,3), increment(4,-2,8);
    MR::vecScaleAdd(&add,&increment,0.5f); assert(add.x==3 && add.y==1 && add.z==7);
    MR::vecScaleAdd(&add,&add,2); assert(add.x==9 && add.y==3 && add.z==21);
    std::filesystem::create_directories(argv[3]);
    AuroraConfig config{}; config.appName = "Galaxy testing checkpoints";
    config.userPath = config.cachePath = argv[3]; config.mem2Size = 64*1024*1024;
    galaxy::initializeRuntimeOptions(argv[3]);
    const auto platform = aurora_initialize(argc, argv, &config);
    SDL_SetWindowOpacity(platform.window, 0);
    assert(galaxy::initializeDisplaySettings(platform.window, argv[3]));
    assert(aurora_dvd_open(argv[1])); JKRMemArchive::setNativeResourceRoot(argv[2]);
    galaxy::setNandRoot(argv[3]); assert(NANDInit() == NAND_RESULT_OK);
    auto root = std::filesystem::path(argv[2]); for (unsigned i=0;i<4;++i) root=root.parent_path();
    galaxy::initializeHdAssets(root/"mods", argv[3]);
    galaxy::initializeCharacters(root/"mods"); galaxy::initializeCharacterOverlays(root/"mods");
    initializeGame(); auto* game = SingletonHolder<GameSystem>::get();
    auto* gameHeap=JKRHeap::sCurrentHeap;
    auto tick = [&] {
        JKRHeap::sCurrentHeap = nullptr; aurora_update();
        if (aurora_begin_frame()) { JKRHeap::sCurrentHeap=gameHeap; game->frameLoop(); gameHeap=JKRHeap::sCurrentHeap; JKRHeap::sCurrentHeap=nullptr; aurora_end_frame(); }
        SDL_Delay(1);
    };
    for (unsigned i=0;i<1500 && (game->mSceneController->mSceneInitializeState != SceneInitializeState_End ||
        std::strcmp(game->mSceneController->mCurrSceneControlInfo.mStage,"FileSelect"));++i) tick();
    assert(game->mSceneController->mSceneInitializeState == SceneInitializeState_End);
    galaxy::TestCheckpoint c;
    std::strcpy(c.stage.data(),"HeavensDoorGalaxy"); c.character=3;
    GameDataHolder fresh(nullptr); fresh.followStoryEventByName("ピーチ城浮上後");
    c.progressSize=fresh.makeFileBinary(c.progress.data(),c.progress.size());
    c.position={15977.1f,-9169.81f,5741.46f};
    c.velocity={-18.2873f,-22.3028f,20.2553f}; c.gravity={0,-1,0}; c.front={0,0,1};
    const auto path=std::filesystem::path(argv[3])/"crater.gcp";
    assert(galaxy::writeTestCheckpoint(path,c)); galaxy::TestCheckpoint loaded;
    assert(galaxy::readTestCheckpoint(path,&loaded)); assert(!std::memcmp(&c,&loaded,offsetof(galaxy::TestCheckpoint,checksum)));
    auto invalid=c; invalid.magic[3]='9'; assert(!galaxy::writeTestCheckpoint(path,invalid));
    invalid=c; invalid.position[0]=std::numeric_limits<float>::quiet_NaN(); assert(!galaxy::validTestCheckpoint(invalid));
    invalid=c; invalid.progressSize=4097; assert(!galaxy::validTestCheckpoint(invalid));
    invalid=c; invalid.stage.fill('x'); assert(!galaxy::validTestCheckpoint(invalid));
    invalid=c; invalid.stage[0]='.'; assert(!galaxy::validTestCheckpoint(invalid));
    invalid=c; invalid.character=4; assert(!galaxy::validTestCheckpoint(invalid));
    invalid=c; invalid.gravity={0,0,0}; assert(!galaxy::validTestCheckpoint(invalid));
    invalid=c; invalid.progress[1]=255; assert(!galaxy::validTestCheckpoint(invalid));
    invalid=c; std::memset(invalid.progress.data()+12,255,4); assert(!galaxy::validTestCheckpoint(invalid));
    { std::ofstream truncated(path.string()+".bad",std::ios::binary); truncated.write(reinterpret_cast<char*>(&c),sizeof(c)-1); }
    assert(!galaxy::readTestCheckpoint(path.string()+".bad",&loaded));
    { std::ofstream corrupt(path.string()+".bad",std::ios::binary); corrupt.write(reinterpret_cast<char*>(&c),sizeof(c)); }
    assert(!galaxy::readTestCheckpoint(path.string()+".bad",&loaded));
    assert(galaxy::readTestCheckpoint(path,&loaded));
    assert(!galaxy::writeTestCheckpoint(path/"missing"/"file",c));
    assert(!galaxy::captureTestCheckpoint(game,&loaded));
    invalid=c; std::strcpy(invalid.stage.data(),"MissingGalaxy"); assert(!galaxy::startTestCheckpoint(game,invalid));
    invalid=c; std::strcpy(invalid.stage.data(),"EggStarGalaxy"); invalid.scenario=6;
    assert(!galaxy::startTestCheckpoint(game,invalid));
    assert(!std::strcmp(game->mSceneController->mCurrSceneControlInfo.mStage,"FileSelect") && galaxy::activeCharacter()==galaxy::Character::Mario);
    assert(galaxy::startTestCheckpoint(game,c));
    bool restored=false;
    for(unsigned i=0;i<2500;++i) {
        PADStatus confirm{}; confirm.button=i%60<10 ? PAD_BUTTON_A : 0; confirm.err=PAD_ERR_NONE; PADSetVirtualStatus(0,&confirm);
        tick();
        if(i < 300 || game->mSceneController->mSceneInitializeState != SceneInitializeState_End ||
           std::strcmp(game->mSceneController->mCurrSceneControlInfo.mStage,c.stage.data()) || MR::isDemoActive()) continue;
        auto* actor=MarioAccess::getPlayerActor();
        TVec3f gravity; MR::calcGravityVector(actor,TVec3f(c.position[0],c.position[1],c.position[2]),&gravity,nullptr,0);
        c.gravity={gravity.x,gravity.y,gravity.z};
        TVec3f front(1,0,0); front-=gravity*front.dot(gravity); front.normalize(); c.front={front.x,front.y,front.z};
        if(galaxy::restoreTestCheckpoint(game,c)) { restored=true; break; }
    }
    assert(restored);
    JKRHeap::sCurrentHeap=gameHeap;
    auto* plane=new DynamicCollisionObj("Launch Star collision regression");
    plane->_94=2; plane->mPositionNum=4;
    plane->mPositions=new TVec3f[4]{TVec3f(-300,0,-300),TVec3f(-300,0,300),TVec3f(300,0,300),TVec3f(300,0,-300)};
    plane->_9C=new TVec3f[2]; plane->mIndices=new DynamicCollisionObj::TriangleIndexing[2];
    plane->mIndices[0].mIndex[0]=0; plane->mIndices[0].mIndex[1]=1; plane->mIndices[0].mIndex[2]=2;
    plane->mIndices[1].mIndex[0]=0; plane->mIndices[1].mIndex[1]=2; plane->mIndices[1].mIndex[2]=3;
    plane->initHitSensor(1); MR::addHitSensorEye(plane,"body",8,1.0f,TVec3f(0));
    const auto placementZone=MR::getCurrentPlacementZoneId();
    MR::setCurrentPlacementZoneId(0);
    plane->createCollision();
    MR::setCurrentPlacementZoneId(placementZone);
    u32 leaf=0; std::memcpy(&leaf,plane->mKCLFile->mOctree,sizeof(leaf));
    assert(leaf==0x80000002);
    auto* server=plane->mParts->mServer;
    s32 shift=0; u32 x=248,y=429,z=255;
    const auto* ids=reinterpret_cast<const u16*>(server->searchBlock(&shift,x,y,z));
    assert(shift==plane->mKCLFile->mBlockWidthShift && ids[1]==2 && ids[2]==1 && ids[3]==0);
    float distances[16]{}; u8 feature=0; u32 count=0; KC_PrismData* hits[16]{};
    assert(server->checkArrow(TVec3f(0,50,0),TVec3f(0,-100,0),distances,&feature,&count,hits,16));
    assert(count>0 && count<=16);
    for(unsigned i=0;i<count;++i) assert(std::abs(distances[i]-0.5f)<0.001f);
    server->checkArrow(TVec3f(-4.0918f,613.834f,28.8291f),TVec3f(-50.6943f,-376.986f,-123.739f),distances,&feature,&count,hits,16);
    assert(count<=16);
    JKRHeap::sCurrentHeap=nullptr;
    std::fprintf(stderr,"Runtime collision leaf and Launch Star shadow ray passed\n");
    assert(galaxy::captureTestCheckpoint(game,&loaded));
    assert(loaded.position==c.position && loaded.character==3 && loaded.scenario==1);
    assert(galaxy::writeTestCheckpoint(path,loaded));
    invalid=loaded; invalid.character=0; assert(!galaxy::restoreTestCheckpoint(game,invalid));
    invalid=loaded; invalid.scenario=2; assert(!galaxy::restoreTestCheckpoint(game,invalid));
    const auto start=*MR::getPlayerPos();
    PADStatus pad{}; pad.stickX=-100; pad.err=PAD_ERR_NONE; PADSetVirtualStatus(0,&pad);
    bool grounded=false;
    for(unsigned i=0;i<180;++i) {
        pad.button=(i%30<10) ? PAD_BUTTON_A : 0; PADSetVirtualStatus(0,&pad); tick();
        grounded |= MR::isOnGroundPlayer();
        assert(!MarioAccess::getPlayerActor()->mMario->isStatusActive(MarioStatus_Recovery));
    }
    PADSetVirtualStatus(0,nullptr);
    std::fprintf(stderr,"Crater distance=%g grounded=%d\n",(*MR::getPlayerPos()-start).length(),grounded);
    assert(grounded && (*MR::getPlayerPos()-start).length()>250);
    assert(galaxy::readTestCheckpoint(path,&loaded));
    assert(galaxy::restoreTestCheckpoint(game,loaded));
    assert(*MR::getPlayerPos()==start);
    GameDataHolder towerProgress(nullptr);
    assert(towerProgress.loadFromFileBinary("luigi1",loaded.progress.data(),loaded.progressSize));
    towerProgress.setPowerStar("HeavensDoorGalaxy",1,true);
    auto tower=loaded;
    std::strcpy(tower.stage.data(),"EggStarGalaxy"); tower.scenario=1;
    tower.position={-3252.8518f,-13975.3916f,-15499.6797f}; tower.velocity={0,0,0};
    tower.gravity={0,1,0}; tower.front={1,0,0};
    tower.progressSize=towerProgress.makeFileBinary(tower.progress.data(),tower.progress.size());
    assert(galaxy::startTestCheckpoint(game,tower));
    bool sawOpening=false, towerRestored=false;
    for(unsigned i=0;i<3000;++i) {
        PADStatus missionConfirm{}; missionConfirm.err=PAD_ERR_NONE;
        if(game->mSceneController->mSceneInitializeState!=SceneInitializeState_End && i%60<10) missionConfirm.button=PAD_BUTTON_A;
        PADSetVirtualStatus(0,&missionConfirm);
        tick();
        const auto* controller=game->mSceneController;
        if(controller->mSceneInitializeState!=SceneInitializeState_End ||
           std::strcmp(controller->mCurrSceneControlInfo.mStage,"EggStarGalaxy")) continue;
        const auto* scene=static_cast<const GameScene*>(controller->mScene);
        if(scene->isExecScenarioOpeningCamera() || scene->isExecScenarioStarter()) {
            sawOpening=true;
            assert(!galaxy::captureTestCheckpoint(game,&invalid));
            assert(!galaxy::restoreTestCheckpoint(game,tower));
        } else if(i>=300 && galaxy::restoreTestCheckpoint(game,tower)) { towerRestored=true; break; }
    }
    std::fprintf(stderr,"Good Egg checkpoint opening=%d restored=%d scene=%s stage=%s state=%d\n",sawOpening,towerRestored,
        game->mSceneController->mCurrSceneControlInfo.mScene,game->mSceneController->mCurrSceneControlInfo.mStage,
        int(game->mSceneController->mSceneInitializeState));
    assert(sawOpening && towerRestored);
    for(unsigned i=0;i<120;++i) tick();
    const TVec3f towerTarget(tower.position[0],tower.position[1],tower.position[2]);
    assert((*MR::getPlayerPos()-towerTarget).length()<50);
    std::fprintf(stderr,"Good Egg fly-in rejected early checkpoint; restored tower position retained\n");
    MR::requestChangeStageInGameMoving("FlipPanelExGalaxy",1);
    bool flipReady=false;
    for(unsigned i=0;i<3000;++i) {
        PADStatus input{}; input.err=PAD_ERR_NONE; input.button=i%60<10 ? PAD_BUTTON_A : 0;
        PADSetVirtualStatus(0,&input); tick();
        if(i>=300 && !std::strcmp(game->mSceneController->mCurrSceneControlInfo.mStage,"FlipPanelExGalaxy") &&
           galaxy::captureTestCheckpoint(game,&loaded)) { flipReady=true; break; }
    }
    assert(flipReady);
    for(unsigned i=0;i<600;++i) {
        PADStatus input{}; input.err=PAD_ERR_NONE; input.stickY=i<300 ? 64 : -64;
        input.button=i%120<12 ? PAD_BUTTON_A : 0; PADSetVirtualStatus(0,&input); tick();
    }
    PADSetVirtualStatus(0,nullptr);
    std::fprintf(stderr,"Flipswitch panel clipping, movement and jump regression passed\n");
    MR::requestChangeStageInGameMoving("KoopaBattleVs2Galaxy",1);
    bool bowserReady=false;
    for(unsigned i=0;i<3000;++i) {
        PADStatus input{}; input.err=PAD_ERR_NONE; input.button=i%60<10 ? PAD_BUTTON_A : 0;
        PADSetVirtualStatus(0,&input); tick();
        if(i>=300 && !std::strcmp(game->mSceneController->mCurrSceneControlInfo.mStage,"KoopaBattleVs2Galaxy") &&
           galaxy::captureTestCheckpoint(game,&loaded)) { bowserReady=true; break; }
    }
    assert(bowserReady);
    for(unsigned i=0;i<120;++i) tick();
    PADSetVirtualStatus(0,nullptr);
    std::fprintf(stderr,"Bowser dark-matter field capture regression passed\n");
    galaxy::shutdownNandWorker(); galaxy::shutdownAlarms(); galaxy::shutdownAudioDevice(); aurora_dvd_close();
    galaxy::shutdownOsServices(); galaxy::shutdownNand(); galaxy::shutdownHdAssets(); aurora_shutdown();
    return 0;
}

