#include <revolution/types.h>
#include <dolphin/ppc_math.h>
#include <bit>
#include <stdexcept>

extern "C" void C_MTX44Identity(float matrix[4][4]) {
    if (!matrix) throw std::invalid_argument("null 4x4 matrix");
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned column = 0; column < 4; ++column) matrix[row][column] = row == column ? 1.0f : 0.0f;
}
extern "C" void C_MTX44Copy(const float source[4][4], float destination[4][4]) {
    if (!source || !destination) throw std::invalid_argument("null 4x4 matrix");
    std::memmove(destination, source, sizeof(float) * 16);
}

f32 __frsqrte(f32 value) { return static_cast<f32>(frsqrte(value)); }
u32 __cntlzw(u32 value) { return std::countl_zero(value); }
s32 __abs(s32 value) { return std::abs(value); }
f32 __fabsf(f32 value) { return std::fabs(value); }
f64 __fabs(f64 value) { return std::fabs(value); }
void* __memcpy(void* dst, const void* src, int size) { return std::memcpy(dst, src, size); }
