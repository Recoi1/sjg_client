#include "test_checkpoint.hpp"
#include "characters.hpp"
#include "runtime_options.hpp"
#include "frame_timing.hpp"
#include "hd_assets.hpp"
#include "home_menu.hpp"
#include "nand.hpp"
#include "os_services.hpp"
#include "audio_device.hpp"
#include "crash.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/System/GameSequenceDirector.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/System/SaveDataHandleSequence.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/System/UserFile.hpp"
#include "Game/System/ScenarioDataParser.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/JMapInfo.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/MapObj/FlipPanel.hpp"
#include "Game/Enemy/JumpBeamer.hpp"
#include "Game/Enemy/RingBeam.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ClippingDirector.hpp"
#include "Game/LiveActor/ClippingActorHolder.hpp"
#include "Game/LiveActor/ClippingActorInfo.hpp"
#include "Game/LiveActor/LiveActorGroupArray.hpp"
#include "Game/NameObj/NameObjRegister.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "JSystem/JKernel/JKRMemArchive.hpp"
#include "JSystem/JKernel/JKRHeap.hpp"
#include "JSystem/JUtility/JUTVideo.hpp"
#include <aurora/aurora.h>
#include <aurora/main.h>
#include <aurora/dvd.h>
#include <dolphin/pad.h>
#include <dolphin/gx/GXAurora.h>
#include <revolution/nand.h>
#include <SDL3/SDL.h>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include <algorithm>
#include <array>
#include <cmath>

// Loading/input smoke coverage is separate from completing a mission's route and boss.
int main(int argc, char** argv) {
    assert(argc==5 || argc==8 || (argc==9 && (!std::strcmp(argv[8],"--flipswitch-route") || !std::strcmp(argv[8],"--sweet-route"))));
    galaxy::installCrashHandler();
    const auto directory=std::filesystem::path(argv[3]);
    assert(!std::filesystem::exists(directory));
    const auto data=directory/"title/00010000/524d4745/data";
    std::filesystem::create_directories(data);
    std::filesystem::copy_file(argv[4],data/"GameData.bin");
    AuroraConfig config{}; config.appName="Galaxy level verification";
    const auto cache=(directory.parent_path()/"level-sweep-cache").string();
    std::filesystem::create_directories(cache);
    config.userPath=argv[3]; config.cachePath=cache.c_str(); config.mem2Size=64*1024*1024;
    config.msaa=4; config.desiredBackend=BACKEND_D3D12;
    galaxy::initializeRuntimeOptions(argv[3]);
    auto options=galaxy::runtimeOptions();
    options[unsigned(galaxy::Option::Vsync)]=0;
    options[unsigned(galaxy::Option::Backend)]=1;
    options[unsigned(galaxy::Option::FrameMode)]=1;
    options[unsigned(galaxy::Option::FrameLimit)]=240;
    options[unsigned(galaxy::Option::AntiAliasing)]=1;
    options[unsigned(galaxy::Option::CameraMode)]=0;
    assert(galaxy::setRuntimeOptions(options));
    const auto platform=aurora_initialize(argc,argv,&config);
    galaxy::applyRuntimeOptions();
    galaxy::measureFrameWork=true;
    assert(galaxy::initializeDisplaySettings(platform.window,argv[3]));
    assert(aurora_dvd_open(argv[1])); JKRMemArchive::setNativeResourceRoot(argv[2]);
    galaxy::setNandRoot(directory); assert(NANDInit()==NAND_RESULT_OK);
    auto root=std::filesystem::path(argv[2]); for(unsigned i=0;i<4;++i) root=root.parent_path();
    galaxy::initializeHdAssets(root/"mods",argv[3]); galaxy::initializeCharacters(root/"mods");
    galaxy::initializeCharacterOverlays(root/"mods");
    initializeGame(); auto* game=SingletonHolder<GameSystem>::get();
    auto* heap=JKRHeap::sCurrentHeap;
    unsigned frame=0;
    using Clock=std::chrono::steady_clock;
    auto next=Clock::now();
    const void* previousScene=nullptr;
    auto tick=[&] {
        const auto begin=Clock::now();
        JKRHeap::sCurrentHeap=nullptr; aurora_update();
        if(aurora_begin_frame()) {
            JKRHeap::sCurrentHeap=heap;
            if (previousScene!=game->mSceneController->mScene) galaxy::resetRenderHistory();
            previousScene=game->mSceneController->mScene;
            // shortcut: controlled cadence checks rendering; benchmark player pacing in galaxy.exe.
            galaxy::setRenderTiming(true,galaxy::simulationTick(),float(frame%4)/4,true);
            game->frameLoop(frame%4==0 ? 1 : 0,false);
            heap=JKRHeap::sCurrentHeap; JKRHeap::sCurrentHeap=nullptr; aurora_end_frame();
        }
        ++frame;
        const auto end=Clock::now();
        next+=std::chrono::nanoseconds(4166667);
        if(end<next) SDL_DelayPrecise(std::chrono::duration_cast<std::chrono::nanoseconds>(next-end).count());
        else next=end;
        return std::chrono::duration<double,std::milli>(end-begin).count();
    };
    for(unsigned i=0;i<12000 && (game->mSceneController->mSceneInitializeState!=SceneInitializeState_End ||
        std::strcmp(game->mSceneController->mCurrSceneControlInfo.mStage,"FileSelect"));++i) tick();
    assert(game->mSceneController->mSceneInitializeState==SceneInitializeState_End);
    auto screenshot=[&](const char* name) {
        auto* video=JUTVideo::getManager(); const auto width=video->getFbWidth(),height=video->getEfbHeight();
        assert(width && width<=1024 && height && height<=1024);
        std::ofstream file(directory/(std::string(name)+".ppm"),std::ios::binary);
        file << "P6\n" << width << ' ' << height << "\n255\n";
        std::array<u8,1024*3> row;
        for(u16 y=0;y<height;++y) {
            for(u16 x=0;x<width;++x) { u32 pixel; GXPeekARGB(x,y,&pixel); row[x*3]=pixel>>16; row[x*3+1]=pixel>>8; row[x*3+2]=pixel; }
            file.write(reinterpret_cast<char*>(row.data()),width*3);
        }
        assert(file.good());
    };
    int result=0;
    if(argc==5) {
        for(auto iter=MR::makeBeginScenarioDataIter(); !iter.isEnd(); iter.goNext()) {
            const auto accessor=iter.makeAccessor();
            const auto* scenario=accessor.mScenarioData;
            for(int i=1;i<=scenario->mScenarioData->getNumEntries();++i) {
                if (accessor.getPowerStarNum()) {
                    const auto star=accessor.getActivePowerStarId(i);
                    int entry=i;
                    if (accessor.isHiddenStar(i)) {
                        entry=0;
                        for(int parent=1;parent<=accessor.getScenarioNum();++parent)
                            if ((accessor.getActivePowerStarId(parent)&star)==star) { entry=parent; break; }
                        assert(entry);
                    }
                    std::printf("MISSION %s %d star=%u hidden=%d load=%d\n",accessor.getName(),i,star,accessor.isHiddenStar(i),entry);
                }
                else std::printf("AREA %s %d\n",accessor.getName(),i);
            }
        }
    } else {
        const auto character=galaxy::Character(std::stoi(argv[7]));
        const char* stage=argv[5]; const int scenario=std::stoi(argv[6]);
        const auto* scenarioData=game->mSceneController->mScenarioParser->getScenarioData(stage);
        assert(scenarioData && scenario<=scenarioData->getScenarioNum() && scenarioData->getScenarioDataIter(scenario).isValid());
        auto* saves=game->mSequenceDirector->mSaveDataHandleSequence;
        saves->restoreUserFile(saves->getCurrentUserFile(),1,galaxy::marioCharacter(character));
        galaxy::activateCharacter(character);
        assert(saves->getCurrentUserFile()->getPowerStarNum()==121);
        std::printf("BEGIN %s %d %s\n",stage,scenario,galaxy::characterName(character)); std::fflush(stdout);
        MR::requestChangeStageInGameMoving(stage,scenario);
        galaxy::TestCheckpoint checkpoint; bool ready=false;
        PADStatus status{}; status.err=PAD_ERR_NONE;
        for(unsigned i=0;i<14400;++i) {
            status.button=i%240<12 ? PAD_BUTTON_A : 0; PADSetVirtualStatus(0,&status); tick();
            if(i>1200 && !std::strcmp(game->mSceneController->mCurrSceneControlInfo.mStage,stage) &&
                galaxy::captureTestCheckpoint(game,&checkpoint)) { ready=true; break; }
        }
        PADSetVirtualStatus(0,nullptr);
        if(ready) { std::printf("LOADED stage=%s requested=%d actual=%d\n",checkpoint.stage.data(),scenario,checkpoint.scenario); std::fflush(stdout); }
        auto steer=[&](const TVec3f& delta) {
            const auto* actor=MarioAccess::getPlayerActor();
            const auto up=actor->mMario->_398;
            const auto screenZ=-actor->mCamDirZ;
            const auto yUp=actor->mCamDirY.dot(up),zUp=screenZ.dot(up);
            TVec3f right,forward;
            // Match Mario::calcMoveDir's gravity-aware basis without calling its stateful calculation.
            if(std::abs(zUp)>std::abs(yUp)) {
                const auto ortho=zUp<0 ? -actor->mCamDirY : actor->mCamDirY;
                right.cross(ortho,up);
            } else right.cross(yUp<0 ? screenZ : -screenZ,up);
            forward.cross(actor->mCamDirX,up); forward=-forward;
            if(right.length()>0.001f) right.normalize();
            if(forward.length()>0.001f) forward.normalize();
            const auto overlap=right.dot(forward),det=1-overlap*overlap;
            const auto x=det>0.001f ? (delta.dot(right)-overlap*delta.dot(forward))/det : delta.dot(right);
            const auto y=det>0.001f ? (delta.dot(forward)-overlap*delta.dot(right))/det : delta.dot(forward);
            const auto length=std::sqrt(x*x+y*y);
            if(length>0.001f) {
                const auto speed=delta.length()<180 ? 48.0f : 100.0f;
                status.stickX=std::clamp(int(speed*x/length),-100,100);
                status.stickY=std::clamp(int(speed*y/length),-100,100);
            }
        };
        auto finishReturn=[&](std::ofstream& actions) {
            bool done=false;
            bool sawSave=false;
            for(unsigned settle=0;settle<7200;++settle) {
                status={}; status.err=PAD_ERR_NONE;
                const bool saving=GameSequenceFunction::isActiveSaveDataHandleSequence();
                sawSave |= saving;
                if(saving) { status.substickX=40; status.substickY=-62; }
                status.button=settle%240<24 ? PAD_BUTTON_A : 0;
                PADSetVirtualStatus(0,&status);
                if(settle%240==0) actions<<"return "<<settle<<' '<<status.button<<' '<<int(status.substickX)<<' '<<int(status.substickY)<<'\n';
                const auto ms=tick();
                if(settle%1200==1199 && galaxy::captureTestCheckpoint(game,&checkpoint))
                    assert(galaxy::writeTestCheckpoint(directory/("return-healthy-"+std::to_string(settle)+".gcp"),checkpoint));
                if(ms>50) {
                    const auto incident="return-frame-dip-"+std::to_string(settle); screenshot(incident.c_str());
                    if(galaxy::captureTestCheckpoint(game,&checkpoint))
                        assert(galaxy::writeTestCheckpoint(directory/(incident+".gcp"),checkpoint));
                    std::printf("INCIDENT return render work %.3f ms at frame %u update=%.3f draw=%.3f\n",ms,settle,galaxy::frameWork.updateMs,galaxy::frameWork.drawMs); std::fflush(stdout);
                }
                if(settle%600==599) screenshot(("reward-return-"+std::to_string(settle)).c_str());
                if(settle>1200 && sawSave && !GameSequenceFunction::isActiveSaveDataHandleSequence() &&
                    !GameSequenceFunction::hasStageResultSequence() &&
                    (!std::strcmp(game->mSceneController->mCurrSceneControlInfo.mStage,"AstroGalaxy") ||
                     !std::strcmp(game->mSceneController->mCurrSceneControlInfo.mStage,"AstroDome")) &&
                    galaxy::captureTestCheckpoint(game,&checkpoint)) {
                    done=true;
                    assert(galaxy::writeTestCheckpoint(directory/"reward-return.gcp",checkpoint));
                    screenshot("reward-return"); break;
                }
            }
            std::printf("RETURN RESULT stage=%s stageResult=%d playable=%d\n",game->mSceneController->mCurrSceneControlInfo.mStage,
                GameSequenceFunction::hasStageResultSequence(),done); std::fflush(stdout);
            return done;
        };
        if(!ready) { screenshot("not-playable"); std::printf("BLOCKED level did not become playable within 60 seconds\n"); result=2; }
        else if(argc==9 && !std::strcmp(argv[8],"--sweet-route")) {
            assert(!std::strcmp(stage,"BeltConveyerExGalaxy") && scenario==1);
            assert(galaxy::writeTestCheckpoint(directory/"route-start.gcp",checkpoint)); screenshot("route-start");
            // Owned placement targets; holes are jumped using collision queries and ordinary PAD inputs.
            const std::array<TVec3f,10> route={TVec3f(-50,50,-7900),TVec3f(2620,50,-7900),TVec3f(2450,50,-9500),
                TVec3f(6400,50,-9500),TVec3f(8500,50,-9000),TVec3f(9670,50,-9744),
                TVec3f(13300,50,-12600),TVec3f(17600,50,-11950),TVec3f(26000,50,-11950),
                TVec3f(26683,1260,-11987)};
            std::ofstream actions(directory/"actions.log");
            const auto* routeScene=game->mSceneController->mScene;
            unsigned waypoint=0,jumpHold=0,railJumpHold=0,lastProgress=0; float nearest=1e9f; bool reward=false,returned=false;
            auto previousHealth=MarioAccess::getPlayerActor()->getHealth();
            for(unsigned i=0;i<57600;++i) {
                reward |= GameSequenceFunction::hasStageResultSequence();
                if(game->mSceneController->mScene!=routeScene || std::strcmp(game->mSceneController->mCurrSceneControlInfo.mStage,stage)) {
                    if(reward) returned=finishReturn(actions);
                    break;
                }
                const auto position=*MR::getPlayerPos();
                const auto gravity=MarioAccess::getPlayerActor()->getGravityVec();
                auto delta=route[waypoint]-position; delta-=gravity*delta.dot(gravity);
                if(delta.length()<nearest-50) { nearest=delta.length(); lastProgress=i; }
                if(i-lastProgress>2400) {
                    screenshot("route-blocked");
                    if(galaxy::captureTestCheckpoint(game,&checkpoint)) assert(galaxy::writeTestCheckpoint(directory/"route-blocked.gcp",checkpoint));
                    std::printf("INPUT ROUTE BLOCKED waypoint=%u; revise inputs, not mission completion\n",waypoint); break;
                }
                status={}; status.err=PAD_ERR_NONE;
                auto drift=*MR::getPlayerLastMove(); drift-=gravity*drift.dot(gravity);
                auto heading=delta;
                // Correct cross-lane conveyor drift before approaching its fire edges.
                if(std::abs(heading.z)>std::abs(heading.x)) heading.x*=8; else heading.z*=8;
                steer(heading-drift*24);
                if(delta.length()<(waypoint==1 ? 180 : 70) && MR::isOnGroundPlayer() && waypoint+1<route.size()) {
                    screenshot(("waypoint-"+std::to_string(waypoint)).c_str());
                    if(galaxy::captureTestCheckpoint(game,&checkpoint)) assert(galaxy::writeTestCheckpoint(directory/("waypoint-"+std::to_string(waypoint)+".gcp"),checkpoint));
                    ++waypoint; nearest=1e9f; lastProgress=i;
                }
                if(!jumpHold && MR::isOnGroundPlayer() && delta.length()>180 && frame%4==0) {
                    auto forward=delta; forward.normalize();
                    for(float distance : {60.0f,110.0f,170.0f}) {
                        const auto ahead=position+forward*distance-gravity*(waypoint ? 40 : 100);
                        if(!Collision::checkStrikeLineToMap(ahead,gravity*(waypoint ? 120 : 250),1,nullptr,nullptr)) { jumpHold=32; break; }
                    }
                }
                if(jumpHold) { status.button=PAD_TRIGGER_Z; if(jumpHold<=24) status.button|=PAD_BUTTON_A; --jumpHold; }
                const bool approachingRail=(waypoint==2 && position.z>-8625 && position.z<-8205) ||
                    (waypoint>=8 && position.x<21800 && position.x>21380);
                if(!railJumpHold && approachingRail && MR::isOnGroundPlayer()) railJumpHold=80;
                if(railJumpHold) { status.button=PAD_BUTTON_A; jumpHold=0; --railJumpHold; }
                if(waypoint+1==route.size() && delta.length()<260 && i%120<24) status.button=PAD_BUTTON_A;
                const auto health=MarioAccess::getPlayerActor()->getHealth();
                if(health<previousHealth) {
                    const auto name="damage-"+std::to_string(i); screenshot(name.c_str());
                    if(galaxy::captureTestCheckpoint(game,&checkpoint)) assert(galaxy::writeTestCheckpoint(directory/(name+".gcp"),checkpoint));
                    std::printf("DAMAGE frame=%u health=%u\n",i,health);
                }
                previousHealth=health;
                PADSetVirtualStatus(0,&status);
                if(frame%4==0) actions<<i<<' '<<waypoint<<' '<<position.x<<' '<<position.y<<' '<<position.z<<' '<<status.button<<' '<<int(status.stickX)<<' '<<int(status.stickY)<<' '<<health<<' '<<MR::isOnGroundPlayer()<<'\n';
                if(i%120==0) { actions.flush(); std::printf("SWEET frame=%u waypoint=%u pos=%g,%g,%g health=%u ground=%d reward=%d\n",i,waypoint,position.x,position.y,position.z,health,MR::isOnGroundPlayer(),reward);
                    if(waypoint==2) { const auto* mario=MarioAccess::getPlayerActor()->mMario;
                        std::printf("INPUT BASIS raw=%d,%d actual=%.3f,%.3f world=%g,%g,%g front=%g,%g,%g flags2d=%d,%d,%d\n",int(status.stickX),int(status.stickY),mario->mStickPos.x,mario->mStickPos.y,mario->mWorldPadDir.x,mario->mWorldPadDir.y,mario->mWorldPadDir.z,mario->mFrontVec.x,mario->mFrontVec.y,mario->mFrontVec.z,mario->mMovementStates._37,mario->_10._15,mario->mMovementStates._3A);
                    } std::fflush(stdout); }
                const auto ms=tick();
                if(i%1200==1199 && galaxy::captureTestCheckpoint(game,&checkpoint)) assert(galaxy::writeTestCheckpoint(directory/("healthy-"+std::to_string(i)+".gcp"),checkpoint));
                if(ms>50) {
                    const auto name="frame-dip-"+std::to_string(i); screenshot(name.c_str());
                    if(galaxy::captureTestCheckpoint(game,&checkpoint)) assert(galaxy::writeTestCheckpoint(directory/(name+".gcp"),checkpoint));
                    std::printf("INCIDENT render work %.3f ms at frame %u update=%.3f draw=%.3f\n",ms,i,galaxy::frameWork.updateMs,galaxy::frameWork.drawMs); std::fflush(stdout);
                }
            }
            PADSetVirtualStatus(0,nullptr); screenshot("route-end");
            if(galaxy::captureTestCheckpoint(game,&checkpoint)) assert(galaxy::writeTestCheckpoint(directory/"route-end.gcp",checkpoint));
            std::printf("SWEET RESULT reward=%d returned=%d; requires visual verification\n",reward,returned);
            result=returned ? 0 : 3;
        }
        else if(argc==9) {
            assert(!std::strcmp(stage,"FlipPanelExGalaxy") && scenario==1);
            assert(galaxy::writeTestCheckpoint(directory/"route-start.gcp",checkpoint)); screenshot("route-start");
            auto* observer=static_cast<FlipPanelObserver*>(SingletonHolder<NameObjRegister>::get()->mHolder->find(MR::getJapaneseObjectName("FlipPanelObserver")));
            assert(observer);
            auto* beamer=static_cast<JumpBeamer*>(SingletonHolder<NameObjRegister>::get()->mHolder->find(MR::getJapaneseObjectName("JumpBeamer")));
            assert(beamer && beamer->mBeams);
            for(unsigned j=0;j<3;++j) assert(beamer->mBeams[j] && beamer->mBeams[j]->getSensor("Beam"));
            LiveActor* frontSpike=nullptr; unsigned spikeCount=0;
            const auto* clipping=MR::getClippingDirector()->mActorHolder;
            for(const auto* list:{clipping->_10,clipping->_14,clipping->_18,clipping->_1C}) {
                assert(list && list->_4>=0 && list->_4<=list->_0);
                for(int j=0;j<list->_4;++j) {
                    auto* actor=list->mClippingActorList[j]->mActor;
                    if(std::strcmp(actor->mName,MR::getJapaneseObjectName("SideSpikeMoveStepA"))) continue;
                    ++spikeCount;
                    if(std::abs(actor->mPosition.z-1100)<1) frontSpike=actor;
                }
            }
            assert(spikeCount==2 && frontSpike);
            // Ordinary stick input follows the owned stage's panel grid; no actor or progress writes.
            const std::array<TVec3f,18> route={
                TVec3f(800,0,1600),TVec3f(1350,-550,1600),TVec3f(1350,-550,2400),
                TVec3f(1350,-550,3200),TVec3f(1350,-2150,3200),TVec3f(1350,-2150,2400),
                TVec3f(1350,-2150,1600),TVec3f(1350,-1350,1600),TVec3f(800,0,2400),
                TVec3f(0,0,2400),TVec3f(0,0,3200),TVec3f(-800,0,3200),
                TVec3f(-800,0,2400),TVec3f(-800,0,1600),TVec3f(-800,-550,1050),
                TVec3f(-800,-1350,1050),TVec3f(-800,-2150,1050),TVec3f(0,-1350,1050)};
            std::array<FlipPanel*,18> panels{};
            assert(observer->_8C->getObjNum()==19);
            for(unsigned i=0;i<route.size();++i) {
                auto origin=route[i];
                if(origin.y==0) origin.y-=150;
                else if(origin.x==1350) origin.x-=150;
                else origin.z+=150;
                for(int j=0;j<observer->_8C->getObjNum();++j) {
                    auto* actor=observer->_8C->getActor(j);
                    if(actor!=observer && (actor->mPosition-origin).length()<1) panels[i]=static_cast<FlipPanel*>(actor);
                }
                assert(panels[i]);
            }
            std::ofstream actions(directory/"actions.log");
            const auto* routeScene=game->mSceneController->mScene;
            // Return around the right face instead of crossing the front spiked block.
            const std::array<TVec3f,9> starRoute={TVec3f(0,-1350,1050),TVec3f(1350,-1350,1600),
                TVec3f(1350,-550,1600),TVec3f(800,0,1600),TVec3f(800,0,2400),TVec3f(0,0,2400),
                TVec3f(-800,0,2400),TVec3f(-600,0,3200),TVec3f(-800,1150,3200)};
            unsigned waypoint=0,starWaypoint=0,dwell=0,jumpHold=0,dodgeHold=0; bool puzzle=false,reward=false,returned=false,jumped=false,starJumped=false;
            auto previousHealth=MarioAccess::getPlayerActor()->getHealth();
            for(unsigned i=0;i<57600;++i) {
                reward |= GameSequenceFunction::hasStageResultSequence();
                if(game->mSceneController->mScene!=routeScene || std::strcmp(game->mSceneController->mCurrSceneControlInfo.mStage,stage)) {
                    if(puzzle && reward) {
                        returned=finishReturn(actions);
                    }
                    break;
                }
                if(!puzzle && observer->_90==18 && MR::isDead(observer)) {
                    puzzle=true; dwell=0; jumpHold=0;
                    screenshot("puzzle-complete");
                    if(galaxy::captureTestCheckpoint(game,&checkpoint))
                        assert(galaxy::writeTestCheckpoint(directory/"puzzle-complete.gcp",checkpoint));
                }
                TVec3f target=route[waypoint];
                if(target.y==0 && target.x==0) target.x+=200;
                if(target.x==-800 && target.y==0 && target.z==3200) target.x+=200;
                if(puzzle) target=starRoute[starWaypoint];
                const auto position=*MR::getPlayerPos();
                bool avoidingSpike=false;
                if(!puzzle && waypoint>=14 && MR::isOnGroundPlayer()) {
                    auto path=target-position; path.z=0;
                    auto relative=frontSpike->mPosition-position; relative.z=0;
                    const auto length2=path.squared();
                    const auto nearest=position+path*(length2>1 ? std::clamp(relative.dot(path)/length2,0.0f,1.0f) : 0.0f);
                    auto distance=nearest-frontSpike->mPosition; distance.z=0;
                    if(distance.length()<600) {
                        avoidingSpike=true;
                        target=position;
                        if(std::abs(relative.x)<550 && std::abs(relative.y)<700)
                            target.y=frontSpike->mPosition.y+(relative.y<=0 ? 700 : -700);
                    }
                }
                const auto health=MarioAccess::getPlayerActor()->getHealth();
                if(health<previousHealth) {
                    const auto damage="damage-"+std::to_string(i); screenshot(damage.c_str());
                    if(galaxy::captureTestCheckpoint(game,&checkpoint)) assert(galaxy::writeTestCheckpoint(directory/(damage+".gcp"),checkpoint));
                    const auto* actor=MarioAccess::getPlayerActor();
                    std::printf("DAMAGE frame=%u health=%u squatLock=%d vertical=%g bind=%u hold=%u\n",i,health,actor->mMario->mMovementStates._21,actor->mMario->mVerticalSpeed,actor->_468,actor->_B94); std::fflush(stdout);
                }
                previousHealth=health;
                auto delta=target-position;
                const auto gravity=MarioAccess::getPlayerActor()->getGravityVec();
                delta-=gravity*delta.dot(gravity);
                status={}; status.err=PAD_ERR_NONE;
                if(!avoidingSpike && delta.length()<95 && MR::isOnGroundPlayer() && (puzzle || MR::isOnPlayer(panels[waypoint]))) {
                    status.button=PAD_TRIGGER_Z;
                    ++dwell;
                    if(dwell>=24 && puzzle && starWaypoint+1<starRoute.size()) { ++starWaypoint; dwell=0; starJumped=false; }
                    if(dwell>=24 && !puzzle && panels[waypoint]->_CD && waypoint+1<route.size()) {
                        screenshot(("waypoint-"+std::to_string(waypoint)).c_str());
                        if(galaxy::captureTestCheckpoint(game,&checkpoint))
                            assert(galaxy::writeTestCheckpoint(directory/("waypoint-"+std::to_string(waypoint)+".gcp"),checkpoint));
                        ++waypoint; dwell=0; jumped=false; jumpHold=0;
                    }
                } else {
                    dwell=0;
                    steer(delta);
                }
                const auto legLength=puzzle ? (starWaypoint ? (starRoute[starWaypoint]-starRoute[starWaypoint-1]).length() : 0) :
                    (waypoint ? (route[waypoint]-route[waypoint-1]).length() : 0);
                // The reference walks around shared edges and only jumps missing panels.
                if(legLength>1000 && !(puzzle ? starJumped : jumped) && MR::isOnGroundPlayer() &&
                   (position-(puzzle ? starRoute[starWaypoint-1] : route[waypoint-1])).length()>220) {
                    if(puzzle) starJumped=true; else jumped=true;
                    jumpHold=32;
                }
                if(jumpHold) {
                    status.button=PAD_TRIGGER_Z;
                    if(jumpHold<=24) status.button|=PAD_BUTTON_A;
                    --jumpHold;
                }
                if(!jumpHold && !dodgeHold && MR::isOnGroundPlayer()) {
                    for(unsigned j=0;j<3;++j) {
                        const auto* beam=beamer->mBeams[j];
                        if(MR::isDead(beam)) continue;
                        TVec3f normal; MR::calcUpVec(&normal,beam);
                        auto radial=position-beam->mPosition;
                        const auto height=radial.dot(normal);
                        radial-=normal*height;
                        if(std::abs(height)<200 && std::abs(radial.length()-beam->getSensor("Beam")->mRadius)<240) dodgeHold=24;
                    }
                }
                if(dodgeHold) { status.button=PAD_BUTTON_A; --dodgeHold; }
                // Crouch must be released after a jump or knockback before it can engage again.
                if(!jumpHold && !dodgeHold && MarioAccess::getPlayerActor()->mMario->mMovementStates._21) status.button&=~PAD_TRIGGER_Z;
                if(puzzle && starWaypoint+1==starRoute.size() && delta.length()<260 && i%120<24) status.button=PAD_BUTTON_A;
                PADSetVirtualStatus(0,&status);
                if(frame%4==0) {
                    actions<<i<<' '<<waypoint<<' '<<observer->_90<<' '<<position.x<<' '<<position.y<<' '<<position.z<<' '
                           <<status.button<<' '<<int(status.stickX)<<' '<<int(status.stickY)<<' '
                           <<MarioAccess::getPlayerActor()->getHealth()<<' '<<MR::isPlayerSquat()<<' '<<MR::isOnGroundPlayer()<<'\n';
                }
                if(i%120==0) {
                    actions.flush();
                    std::printf("ROUTE frame=%u waypoint=%u panels=%d pos=%g,%g,%g health=%u squat=%d ground=%d starWaypoint=%u reward=%d\n",i,waypoint,observer->_90,position.x,position.y,position.z,
                        MarioAccess::getPlayerActor()->getHealth(),MR::isPlayerSquat(),MR::isOnGroundPlayer(),starWaypoint,reward); std::fflush(stdout);
                    std::printf("SPIKE pos=%g,%g,%g avoiding=%d\n",frontSpike->mPosition.x,frontSpike->mPosition.y,frontSpike->mPosition.z,avoidingSpike); std::fflush(stdout);
                }
                const auto ms=tick();
                if(i%1200==1199 && galaxy::captureTestCheckpoint(game,&checkpoint))
                    assert(galaxy::writeTestCheckpoint(directory/("healthy-"+std::to_string(i)+".gcp"),checkpoint));
                if(ms>50) {
                    const auto incident="frame-dip-"+std::to_string(i); screenshot(incident.c_str());
                    if(galaxy::captureTestCheckpoint(game,&checkpoint)) assert(galaxy::writeTestCheckpoint(directory/(incident+".gcp"),checkpoint));
                    std::printf("INCIDENT render work %.3f ms at frame %u update=%.3f draw=%.3f\n",ms,i,galaxy::frameWork.updateMs,galaxy::frameWork.drawMs); std::fflush(stdout);
                }
            }
            PADSetVirtualStatus(0,nullptr); screenshot("route-end");
            if(galaxy::captureTestCheckpoint(game,&checkpoint)) assert(galaxy::writeTestCheckpoint(directory/"route-end.gcp",checkpoint));
            std::printf("ROUTE RESULT puzzle=%d reward=%d returned=%d; reward requires visual verification\n",puzzle,reward,returned);
            result=returned ? 0 : 3;
        } else {
            assert(galaxy::writeTestCheckpoint(directory/"before-input.gcp",checkpoint)); screenshot("before-input");
            std::vector<double> work;
            std::ofstream actions(directory/"actions.log");
            for(unsigned i=0;i<7200;++i) {
                const unsigned phase=(i/1200)%6;
                status={}; status.err=PAD_ERR_NONE;
                // Forward/backward and both lateral directions, jump/spin, then neutral settling.
                status.stickX=phase==2 ? 64 : phase==3 ? -64 : 0;
                status.stickY=phase==0 ? 64 : phase==1 ? -64 : 0;
                status.button=phase<5 && i%240<24 ? PAD_BUTTON_A : phase<5 && i%240>=120 && i%240<144 ? PAD_BUTTON_X : 0;
                PADSetVirtualStatus(0,&status);
                if(i%240==0) { actions << i << ' ' << status.button << ' ' << int(status.stickX) << ' ' << int(status.stickY) << '\n'; actions.flush(); }
                const auto ms=tick(); work.push_back(ms);
                if(i%1200==1199 && galaxy::captureTestCheckpoint(game,&checkpoint))
                    assert(galaxy::writeTestCheckpoint(directory/("healthy-"+std::to_string(i)+".gcp"),checkpoint));
                if(ms>50) {
                    const auto incident="frame-dip-"+std::to_string(i);
                    screenshot(incident.c_str());
                    if(galaxy::captureTestCheckpoint(game,&checkpoint)) assert(galaxy::writeTestCheckpoint(directory/(incident+".gcp"),checkpoint));
                    std::printf("INCIDENT render work %.3f ms at frame %u update=%.3f draw=%.3f; checkpoints retained\n",ms,i,galaxy::frameWork.updateMs,galaxy::frameWork.drawMs); std::fflush(stdout);
                }
            }
            PADSetVirtualStatus(0,nullptr);
            if(galaxy::captureTestCheckpoint(game,&checkpoint)) assert(galaxy::writeTestCheckpoint(directory/"after-input.gcp",checkpoint));
            screenshot("after-input"); std::sort(work.begin(),work.end());
            std::printf("SMOKE PASS %s %d %s work p50=%.3f p95=%.3f p99=%.3f max=%.3f ms; mission completion unverified\n",stage,scenario,galaxy::characterName(character),work[work.size()/2],work[work.size()*95/100],work[work.size()*99/100],work.back());
        }
    }
    galaxy::shutdownNandWorker(); galaxy::shutdownAlarms(); galaxy::shutdownAudioDevice(); aurora_dvd_close();
    galaxy::shutdownOsServices(); galaxy::shutdownNand(); galaxy::shutdownHdAssets(); aurora_shutdown();
    return result;
}
