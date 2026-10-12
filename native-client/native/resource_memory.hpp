#pragma once
#include "JSystem/JKernel/JKRDisposer.hpp"
#include <vector>
#include <utility>
namespace galaxy {
struct NativeResource final : JKRDisposer {
    std::vector<u8> bytes;
    explicit NativeResource(std::vector<u8>&& data) : bytes(std::move(data)) {}
};
}
