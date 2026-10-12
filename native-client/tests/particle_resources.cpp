#include "particle_resource.hpp"
#include "yaz0.hpp"
#include "crash.hpp"
#include "JSystem/JKernel/JKRExpHeap.hpp"
#include "JSystem/JKernel/JKRMemArchive.hpp"
#include "JSystem/JParticle/JPAResourceManager.hpp"
#include "JSystem/JParticle/JPAResource.hpp"
#include "JSystem/JParticle/JPADynamicsBlock.hpp"
#include "JSystem/JParticle/JPABaseShape.hpp"
#include "JSystem/JParticle/JPAKeyBlock.hpp"
#include "JSystem/JParticle/JPAEmitter.hpp"
#include "JSystem/JParticle/JPAEmitterManager.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
int main(int argc, char** argv) {
    assert(argc == 2); galaxy::installCrashHandler(); auto* root = JKRExpHeap::createRoot(1, false); assert(root);
    std::ifstream f(argv[1], std::ios::binary); assert(f); std::vector<u8> bytes{std::istreambuf_iterator<char>(f), {}};
    if (!memcmp(bytes.data(), "Yaz0", 4)) bytes = galaxy::decodeYaz0(bytes);
    JKRMemArchive archive; assert(archive.mountFixed(bytes.data(), bytes.size(), JKR_MEM_BREAK_FLAG_0));
    const auto* data = static_cast<const u8*>(archive.getResource("Particles.jpc")); const auto size = archive.getResSize(data); assert(data && size > 16);
    const auto native = galaxy::importParticles({data, std::size_t(size)}); assert(native.size() == size);
    for (unsigned n = 0; n < 64; ++n) { bool rejected = false; try { galaxy::importParticles({data, n}); } catch (const std::invalid_argument&) { rejected = true; } assert(rejected); }
    auto malformed = std::vector<u8>(data, data + size); memset(malformed.data() + 12, 255, 4); bool rejected = false;
    try { galaxy::importParticles(malformed); } catch (const std::invalid_argument&) { rejected = true; } assert(rejected);
    auto* heap = JKRExpHeap::create(32 * 1024 * 1024, root, false); assert(heap); heap->becomeCurrentHeap();
    {
        JPAResourceManager manager(data, heap); assert(manager.mResNum == (unsigned(data[8]) << 8 | data[9])); assert(manager.mTexNum == (unsigned(data[10]) << 8 | data[11]));
        JPAEmitterManager emitters(64, 4, heap, 1, 1); emitters.entryResourceManager(&manager, 0);
        auto* emitter = emitters.createSimpleEmitterID(TVec3f(0, 0, 0), manager.mpResArr[0]->mUsrIdx, 0, 0, nullptr, nullptr); assert(emitter);
        const auto owner = reinterpret_cast<uintptr_t>(&manager); emitter->setUserWork(owner); assert(emitter->getUserWork() == owner);
        emitters.forceDeleteEmitter(emitter);
        for (unsigned i = 0; i < manager.mResNum; ++i) {
            const auto* resource = manager.mpResArr[i]; assert(manager.getResource(resource->mUsrIdx) == resource);
            assert(resource->mpDynamicsBlock && resource->mpBaseShape); assert(std::isfinite(resource->mpDynamicsBlock->getRate()));
            for (unsigned k = 0; k < resource->mKeyBlockNum; ++k) for (float frame : {0.f, 12.5f, 100.f}) assert(std::isfinite(resource->mpKeyBlocks[k]->calc(frame)));
            for (unsigned t = 0; t < resource->mTDB1Num; ++t) assert(resource->mpTDB1[t] < manager.mTexNum);
        }
        unsigned spheres = 0;
        for (unsigned i = 0; i < manager.mResNum; ++i) {
            auto* resource = manager.mpResArr[i]; auto* dynamics = resource->mpDynamicsBlock;
            if (dynamics->getVolumeType() != 1 || !(dynamics->getFlag() & JPADynFlag_FixedInterval)) continue;
            auto* sphere = emitters.createSimpleEmitterID(TVec3f(0, 0, 0), resource->mUsrIdx, 0, 0, nullptr, nullptr); assert(sphere);
            auto* work = emitters.pWd; work->mpEmtr = sphere; resource->calcWorkData_c(work);
            work->mVolumeMinRad = 1; work->mVolumeSize = 100;
            const unsigned count = 4 * dynamics->getDivNumber() * dynamics->getDivNumber() + 2;
            for (unsigned particle = 0; particle < count; ++particle) {
                dynamics->calc(work);
                const auto& pos = work->mVolumeCalcData.mVolumePos;
                assert(std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z));
                assert(std::abs(pos.x * pos.x + pos.y * pos.y + pos.z * pos.z - 10000) < 1);
                if (dynamics->getDivNumber() && (particle == 0 || particle == count - 1)) {
                    assert(std::abs(pos.x) < .01f && std::abs(pos.z) < .01f);
                    assert(std::abs(pos.y - (particle == 0 ? -100.f : 100.f)) < .01f);
                }
            }
            emitters.forceDeleteEmitter(sphere); ++spheres;
        }
        assert(spheres); std::cout << spheres << " fixed-interval sphere effects, including both poles, passed\n";
        for (unsigned i = 0; i < manager.mTexNum; ++i) { const auto* texture = manager.mpTexArr[i]->getJUTTexture()->getTexInfo(); assert(texture && texture->mWidth && texture->mHeight && texture->mImageDataOffset >= 32); }
        assert(manager.mResNum > 100 && manager.mTexNum > 10 && heap->check());
        std::cout << manager.mResNum << " original particle resources, " << manager.mTexNum << " textures, curves, references and heap bounds passed\n";
    }
    root->becomeCurrentHeap(); heap->destroy(); assert(root->check());
}
