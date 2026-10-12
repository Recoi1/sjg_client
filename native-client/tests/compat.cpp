#include <revolution/types.h>
#include <nw4r/misc.h>
#include <nw4r/math/arithmetic.h>
#include <JSystem/JGadget/linklist.hpp>
#include <JSystem/JKernel/JKRMemArchive.hpp>
#include <Game/Util/MemoryUtil.hpp>
#include <Game/Util/MathUtil.hpp>
#include <Game/System/StationedArchiveLoader.hpp>
#include <JSystem/J3DGraphBase/J3DPacket.hpp>
#include <nw4r/lyt/pane.h>
#include <nw4r/lyt/layout.h>
#include <array>
#include <cassert>
#include <limits>
#include <type_traits>
#include "../native/pointer_edge.hpp"

struct CallbackProbe {
    int value = 0;
    void increment() { ++value; }
    void add(int amount) { value += amount; }
    bool positive() const { return value > 0; }
};

int main() {
    MR::initAcosTable();
    assert(MR::diffAngleAbs(TVec3f(0, 0, 0), TVec3f(0, 0, 1)) == 0);
    assert(MR::diffAngleAbs(TVec2f(1, 0), TVec2f(0, 0)) == 0);
    assert(MR::acosEx(std::numeric_limits<float>::quiet_NaN()) == 0);
    assert(MR::acosEx(1.000001f) == 0);
    assert(std::abs(MR::acosEx(-1.000001f) - 3.14159265f) < 0.00001f);
    for (float x : {-0.999f, -0.98f, -0.5f, 0.0f, 0.5f, 0.98f, 0.999f})
        assert(std::abs(MR::acosEx(x) - std::acos(x)) < 0.003f);
    TVec2f pointer(5, 5);
    assert(galaxy::pointerToEdge(&pointer, pointer, TVec2f(1, 0), 10, 10, -2, -2));
    assert(pointer.x == 12 && pointer.y == 5);
    pointer.set(5, 5);
    assert(galaxy::pointerToEdge(&pointer, pointer, TVec2f(-1, 0), 10, 10, -2, -2));
    assert(pointer.x == -2 && pointer.y == 5);
    pointer.set(5, 5);
    assert(galaxy::pointerToEdge(&pointer, pointer, TVec2f(0, 1), 10, 10, 0, 0));
    assert(pointer.x == 5 && pointer.y == 10);
    pointer.set(5, 5);
    assert(galaxy::pointerToEdge(&pointer, pointer, TVec2f(1, 10), 10, 10, 0, 0));
    assert(std::abs(pointer.x - 5.5f) < 0.001f && pointer.y == 10);
    pointer.set(5, 5);
    assert(!galaxy::pointerToEdge(&pointer, pointer, TVec2f(0, 0), 10, 10, 0, 0));
    assert(pointer.x == 5 && pointer.y == 5);
    pointer.set(20, 5);
    assert(!galaxy::pointerToEdge(&pointer, pointer, TVec2f(1, 0), 10, 10, 0, 0));
    static_assert(sizeof(nw4r::IntPtr) == sizeof(void*));
    static_assert(sizeof(nw4r::PtrDiff) == sizeof(void*));
    static_assert(!std::is_abstract_v<JKRMemArchive>);
    static_assert(std::is_abstract_v<J3DPacket>);
    static_assert(std::is_abstract_v<StationedArchiveLoader::Condition>);
    alignas(nw4r::lyt::Pane) std::array<unsigned char, sizeof(nw4r::lyt::Pane)> paneStorage{};
    auto* pane = reinterpret_cast<nw4r::lyt::Pane*>(paneStorage.data());
    auto* paneNode = nw4r::lyt::PaneList::GetNodeFromPointer(pane);
    assert(reinterpret_cast<uintptr_t>(paneNode) - reinterpret_cast<uintptr_t>(pane) == offsetof(nw4r::lyt::detail::PaneBase, mLink));
    assert(nw4r::lyt::PaneList::GetPointerFromNode(paneNode) == pane);
    alignas(nw4r::lyt::AnimTransform) std::array<unsigned char, sizeof(nw4r::lyt::AnimTransform)> animStorage{};
    auto* anim = reinterpret_cast<nw4r::lyt::AnimTransform*>(animStorage.data());
    auto* animNode = nw4r::lyt::AnimTransformList::GetNodeFromPointer(anim);
    assert(reinterpret_cast<uintptr_t>(animNode) - reinterpret_cast<uintptr_t>(anim) == offsetof(nw4r::lyt::AnimTransform, mLink));
    assert(nw4r::lyt::AnimTransformList::GetPointerFromNode(animNode) == anim);
    static_assert(std::is_same_v<std::iterator_traits<
        JGadget::TIterator<std::forward_iterator_tag, int>>::value_type, int>);
    CallbackProbe probe;
    auto positive = std::mem_func(&CallbackProbe::positive);
    assert(std::not1(positive)(&probe));
    std::mem_func(&CallbackProbe::increment)(&probe);
    std::bind2nd(std::mem_func(&CallbackProbe::add), 4)(&probe);
    assert(probe.value == 5 && positive(&probe));
    CallbackProbe array[3];
    std::for_each_array(array, array + 3, std::mem_func(&CallbackProbe::increment));
    assert(array[0].value == 1 && array[2].value == 1);
    array[1].value = 0;
    assert(std::find_if_array(array, array + 3, std::not1(positive)) == array + 1);
    assert(std::find_if_array(array, array, positive) == array);
    assert(nw4r::math::FSelect(0.0f, 2.0f, 3.0f) == 2.0f);
    assert(nw4r::math::FSelect(-0.0f, 2.0f, 3.0f) == 2.0f);
    assert(nw4r::math::FSelect(-1.0f, 2.0f, 3.0f) == 3.0f);
    assert(nw4r::math::FSelect(std::numeric_limits<float>::quiet_NaN(), 2.0f, 3.0f) == 3.0f);
    assert(__cntlzw(0) == 32 && __cntlzw(1) == 31 && __cntlzw(0x80000000) == 0);
    MR::copyMemory(nullptr, nullptr, 0);
    MR::zeroMemory(nullptr, 0);
    std::array<u8, 64> source;
    for (unsigned i = 0; i != source.size(); ++i) source[i] = i;
    for (unsigned offset = 0; offset != 8; ++offset) {
        for (unsigned size : {0, 1, 3, 4, 15, 16, 17, 32}) {
            std::array<u8, 64> destination;
            destination.fill(0xcc);
            MR::copyMemory(destination.data() + offset, source.data() + 7 - offset, size);
            for (unsigned i = 0; i != destination.size(); ++i) {
                assert(destination[i] == (i >= offset && i < offset + size ? source[i + 7 - 2 * offset] : 0xcc));
            }
            MR::zeroMemory(destination.data() + offset, size);
            for (unsigned i = 0; i != destination.size(); ++i) {
                assert(destination[i] == (i >= offset && i < offset + size ? 0 : 0xcc));
            }
        }
    }
}
