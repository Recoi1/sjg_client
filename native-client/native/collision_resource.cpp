#include "collision_resource.hpp"
#include "Game/Map/KCollision.hpp"
#include <bit>
#include <cmath>
#include <cstring>
#include <functional>
#include <set>
#include <stdexcept>
namespace galaxy {
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::invalid_argument(message); }
struct Reader {
    std::span<const std::uint8_t> source;
    void range(std::size_t p, std::size_t n) const { require(p <= source.size() && n <= source.size() - p, "collision range exceeds file"); }
    std::uint16_t u16(std::size_t p) const { range(p, 2); return source[p] << 8 | source[p + 1]; }
    std::uint32_t u32(std::size_t p) const { return std::uint32_t(u16(p)) << 16 | u16(p + 2); }
    float number(std::size_t p) const { const auto f = std::bit_cast<float>(u32(p)); require(std::isfinite(f), "nonfinite collision coordinate"); return f; }
};
}
std::vector<std::uint8_t> importCollision(std::span<const std::uint8_t> source) {
    Reader r{source}; r.range(0, 56); constexpr auto expansion = sizeof(KCLFile) - 56; static_assert(sizeof(KC_PrismData) == 16 && sizeof(TVec3f) == 12);
    const std::size_t pos = r.u32(0), norms = r.u32(4), prisms = r.u32(8), tree = r.u32(12), firstPrism = prisms + 16;
    require(pos >= 56 && pos <= norms && norms <= firstPrism && firstPrism <= tree && tree < source.size() && pos % 4 == 0 && norms % 4 == 0 && tree % 4 == 0, "invalid collision section offsets");
    require((norms - pos) % 12 == 0 && (firstPrism - norms) % 12 == 0 && (tree - firstPrism) % 16 == 0, "misaligned collision arrays");
    const auto positions = (norms - pos) / 12, normals = (firstPrism - norms) / 12, triangles = (tree - firstPrism) / 16;
    require(positions && normals && triangles <= 65535, "invalid collision array counts");
    std::vector<std::uint8_t> result(source.size() + expansion); std::memcpy(result.data() + sizeof(KCLFile), source.data() + 56, source.size() - 56);
    auto* file = reinterpret_cast<KCLFile*>(result.data());
    file->mPos = reinterpret_cast<TVec3f*>(result.data() + pos + expansion); file->mNorms = reinterpret_cast<TVec3f*>(result.data() + norms + expansion);
    file->mPrisms = reinterpret_cast<KC_PrismData*>(result.data() + prisms + expansion); file->mOctree = result.data() + tree + expansion;
    file->mThickness = r.number(16); require(file->mThickness >= 0, "negative collision thickness");
    file->mMin.set(r.number(20), r.number(24), r.number(28)); file->mXMask = r.u32(32); file->mYMask = r.u32(36); file->mZMask = r.u32(40);
    file->mBlockWidthShift = r.u32(44); file->mBlockXShift = r.u32(48); file->mBlockXYShift = r.u32(52);
    require(file->mBlockWidthShift >= 0 && file->mBlockWidthShift < 31, "invalid collision block width");
    auto word = [&](std::size_t p) { const auto v = r.u16(p); std::memcpy(result.data() + p + expansion, &v, 2); };
    auto dword = [&](std::size_t p) { const auto v = r.u32(p); std::memcpy(result.data() + p + expansion, &v, 4); };
    for (auto p = pos; p < firstPrism; p += 4) { r.number(p); dword(p); }
    for (auto p = firstPrism; p < tree; p += 16) {
        r.number(p); require(r.u16(p + 4) < positions && r.u16(p + 6) < normals, "collision prism exceeds vertex arrays");
        for (unsigned i = 0; i < 3; ++i) require(r.u16(p + 8 + i * 2) < normals, "collision edge exceeds normal array");
        dword(p); for (unsigned i = 0; i < 6; ++i) word(p + 4 + i * 2);
    }
    auto axis = [&](std::uint32_t mask) {
        const auto maximum = ~mask; require(mask && !(maximum & (maximum + 1)), "invalid collision axis mask");
        return std::size_t(maximum >> file->mBlockWidthShift) + 1;
    };
    const auto nx = axis(file->mXMask), ny = axis(file->mYMask), nz = axis(file->mZMask);
    const auto xshift = std::countr_zero(nx), xyshift = xshift + std::countr_zero(ny);
    const bool single = file->mBlockXShift == -1 && file->mBlockXYShift == -1;
    require(single || (file->mBlockXShift == xshift && file->mBlockXYShift == xyshift), "invalid collision grid shifts");
    require(single || (nx <= (source.size() - tree) / 4 && ny <= (source.size() - tree) / (nx * 4) && nz <= (source.size() - tree) / (nx * ny * 4)), "collision grid exceeds octree");
    const auto roots = single ? 1 : nx * ny * nz; r.range(tree, roots * 4);
    std::set<std::size_t> nodes, leaves; std::vector<u8> encodings(source.size() - tree);
    auto mark = [&](std::size_t p, unsigned n, u8 kind) {
        r.range(p, n); for (unsigned i = 0; i < n; ++i) { auto& prior = encodings[p + i - tree]; require(!prior || prior == kind, "collision node overlaps prism list"); prior = kind; }
    };
    std::function<void(std::size_t, std::size_t, int)> visit = [&](std::size_t base, std::size_t entry, int depth) {
        if (!nodes.insert(entry).second) return; mark(entry, 4, 1); const auto value = r.u32(entry); dword(entry);
        const auto target = base + (value & 0x7fffffff); require(target >= tree, "invalid collision octree target");
        if (value & 0x80000000) {
            if (!leaves.insert(target).second) return;
            // Leaf offsets point two bytes before their first ID, which can overlap the last branch word.
            for (auto q = target + 2;; q += 2) { mark(q, 2, 2); const auto id = r.u16(q); require(id <= triangles, "collision leaf references missing triangle"); word(q); if (!id) break; }
        } else {
            require(depth > 0 && target > base && target % 4 == 0, "cyclic or overdeep collision octree"); r.range(target, 32);
            for (unsigned i = 0; i < 8; ++i) visit(target, target + i * 4, depth - 1);
        }
    };
    for (std::size_t i = 0; i < roots; ++i) visit(tree, tree + i * 4, file->mBlockWidthShift);
    return result;
}
}
