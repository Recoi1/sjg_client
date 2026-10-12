#include "test_checkpoint.hpp"
#include "characters.hpp"
#include "io.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/System/BinaryDataChunkHolder.hpp"
#include "Game/System/ScenarioDataParser.hpp"
#include "Game/Scene/GameScene.hpp"
#include "Game/Util/JMapIdInfo.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/LiveActor/Binder.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace galaxy {
namespace {
bool playable(GameSystem* game) {
    const auto* controller = game->mSceneController;
    return controller->mSceneInitializeState == SceneInitializeState_End &&
        controller->mScene && !std::strcmp(controller->mCurrSceneControlInfo.mScene, "Game") &&
        std::strcmp(controller->mCurrSceneControlInfo.mStage, "FileSelect") && !MR::isDemoActive() && !MarioAccess::isOffControl() &&
        !MR::isPlayerDead() && !MR::isPlayerInBind() &&
        !static_cast<const GameScene*>(controller->mScene)->isExecScenarioOpeningCamera() &&
        !static_cast<const GameScene*>(controller->mScene)->isExecScenarioStarter();
}
std::array<float, 3> pack(const TVec3f& v) { return {v.x, v.y, v.z}; }
TVec3f unpack(const std::array<float, 3>& v) { return {v[0], v[1], v[2]}; }
uint32_t checksum(const TestCheckpoint& c) {
    uint32_t hash = 2166136261u;
    const auto* bytes = reinterpret_cast<const uint8_t*>(&c);
    for (std::size_t i=0;i<offsetof(TestCheckpoint,checksum);++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}
}
bool validTestCheckpoint(const TestCheckpoint& c) {
    if (c.magic != std::array<char,4>{'G','C','P','1'} || c.character > 3 || c.scenario < 1 || c.scenario > 99 || c.startId < -1 || c.startId > 10000 || c.startZone < 0 || c.startZone > 64 ||
        !c.progressSize || c.progressSize > c.progress.size() || c.stage[0] == '\0' || c.stage.back() != '\0') return false;
    for (char ch : c.stage) {
        if (!ch) break;
        if (!(ch >= 'A' && ch <= 'Z') && !(ch >= 'a' && ch <= 'z') && !(ch >= '0' && ch <= '9') && ch != '_') return false;
    }
    for (const auto* v : {&c.position, &c.velocity, &c.gravity, &c.front})
        for (float f : *v) if (!std::isfinite(f) || std::abs(f) > 10000000) return false;
    for (const auto* v : {&c.gravity, &c.front}) {
        const auto length = (*v)[0]*(*v)[0]+(*v)[1]*(*v)[1]+(*v)[2]*(*v)[2];
        if (length < 0.9f || length > 1.1f) return false;
    }
    if (c.progressSize < 4 || c.progress[0] != 1 || c.progress[1] != 6) return false;
    std::size_t offset = 4;
    constexpr auto header = offsetof(BinaryDataChunkHolderChunkData, mData);
    for (unsigned i=0;i<c.progress[1];++i) {
        if (c.progressSize - offset < header) return false;
        uint32_t size; std::memcpy(&size,c.progress.data()+offset+8,sizeof(size));
        if (size < header || size > c.progressSize-offset) return false;
        offset += size;
    }
    return offset == c.progressSize;
}
bool readTestCheckpoint(const std::filesystem::path& path, TestCheckpoint* result) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() != sizeof(TestCheckpoint)) return false;
    TestCheckpoint c; file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(&c), sizeof(c)) || c.checksum != checksum(c) || !validTestCheckpoint(c)) return false;
    *result = c; return true;
}
bool writeTestCheckpoint(const std::filesystem::path& path, const TestCheckpoint& c) {
    auto saved = c; saved.checksum = checksum(saved);
    return validTestCheckpoint(c) && aurora::io::write_file_atomic(path,
        {reinterpret_cast<const uint8_t*>(&saved), sizeof(saved)});
}
bool captureTestCheckpoint(GameSystem* game, TestCheckpoint* result) {
    if (!playable(game)) return false;
    TestCheckpoint c;
    const auto& scene = game->mSceneController->mCurrSceneControlInfo;
    std::memcpy(c.stage.data(), scene.mStage, c.stage.size()); c.scenario = scene.mScenarioNo;
    if (scene.mStartIdInfo) { c.startId = scene.mStartIdInfo->_0; c.startZone = scene.mStartIdInfo->mZoneID; }
    c.character = unsigned(activeCharacter());
    c.progressSize = GameDataFunction::getCurrentGameDataHolder()->makeFileBinary(c.progress.data(), c.progress.size());
    std::fprintf(stdout, "[test checkpoint] stage=%s scenario=%d stars=%d grand1=%d openedDomes=%d stageResult=%d\n",
        c.stage.data(), c.scenario, GameDataFunction::calcCurrentPowerStarNum(), GameDataFunction::hasGrandStar(1),
        MR::calcOpenedAstroDomeNum(), GameSequenceFunction::hasStageResultSequence());
    auto* actor = MarioAccess::getPlayerActor();
    c.position = pack(actor->mPosition); c.velocity = pack(actor->mVelocity);
    c.gravity = pack(actor->mMario->mAirGravityVec); c.front = pack(actor->mMario->mFrontVec);
    if (!validTestCheckpoint(c)) return false;
    *result = c; return true;
}
bool startTestCheckpoint(GameSystem* game, const TestCheckpoint& c) {
    if (!validTestCheckpoint(c) || !game->mSceneController->mScenarioParser ||
        game->mSceneController->mSceneInitializeState != SceneInitializeState_End) return false;
    const auto* scenario = game->mSceneController->mScenarioParser->getScenarioData(c.stage.data());
    // Hidden-star records are goals inside a normal mission, not standalone scenes.
    if (!scenario || c.scenario > scenario->getScenarioNum() || !scenario->getScenarioDataIter(c.scenario).isValid()) return false;
    // Validate the canonical game data before changing the current scene or character.
    GameDataHolder checked(nullptr);
    const auto character = Character(c.character);
    const char* family = marioCharacter(character) ? "mario1" : "luigi1";
    if (!checked.loadFromFileBinary(family, c.progress.data(), c.progressSize)) return false;
    auto* holder = GameDataFunction::getCurrentGameDataHolder();
    if (!holder->loadFromFileBinary(family, c.progress.data(), c.progressSize)) return false;
    activateCharacter(character);
    MR::requestChangeStageInGameMoving(c.stage.data(), c.scenario, JMapIdInfo(c.startId,c.startZone));
    return true;
}
bool restoreTestCheckpoint(GameSystem* game, const TestCheckpoint& c) {
    if (!validTestCheckpoint(c) || !playable(game) || activeCharacter() != Character(c.character) ||
        std::strcmp(game->mSceneController->mCurrSceneControlInfo.mStage, c.stage.data()) ||
        game->mSceneController->mCurrSceneControlInfo.mScenarioNo != c.scenario) return false;
    MR::setPlayerPosAndWait(unpack(c.position));
    auto* actor = MarioAccess::getPlayerActor();
    actor->_240 = unpack(c.gravity); actor->mUpVec = -actor->_240;
    actor->_2C4 = actor->mUpVec * 70.0f;
    actor->mMario->setGravityVec(actor->_240); actor->mMario->setHeadVec(actor->mUpVec);
    actor->mMario->setFrontVec(unpack(c.front));
    actor->mVelocity = unpack(c.velocity); actor->mMario->mVelocity = actor->mVelocity;
    actor->mMario->mVelocityAfter = actor->mVelocity;
    actor->_294 = actor->mPosition; actor->_270 = actor->mPosition;
    actor->mBinder->clear();
    MR::resetCameraMan();
    return true;
}
}
