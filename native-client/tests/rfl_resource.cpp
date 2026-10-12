#include "rfl_resource.h"
#include "rarc.hpp"
#include "yaz0.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <fstream>
#include <iostream>
#include <stdexcept>

template<class F> void reject(F operation) {
    bool failed = false;
    try { operation(); } catch (const std::exception&) { failed = true; }
    assert(failed);
}
int main(int argc, char** argv) {
    assert(argc == 2);
    std::ifstream input(argv[1], std::ios::binary);
    assert(input);
    std::vector<u8> packed{std::istreambuf_iterator<char>(input), {}};
    const auto archiveBytes = packed[0] == 'Y' ? galaxy::decodeYaz0(packed) : packed;
    const galaxy::Rarc archive(archiveBytes);
    const auto resource = archive.resource("/RFL_Res.dat");
    RFLiLoader loader{};
    loader.cache = const_cast<u8*>(resource.data());
    loader.cacheSize = resource.size();
    loader.cached = true;
    RFLPCParseResource(&loader);
    auto truncated = loader;
    truncated.cacheSize = 10;
    reject([&] { RFLPCParseResource(&truncated); });
    reject([&] { RFLPCResourceSize(&loader, RFLiArcID_Max, 0); });
    constexpr std::array<unsigned, 9> shapeArcs{RFLiArcID_Nose, RFLiArcID_ForeHead, RFLiArcID_Faceline,
        RFLiArcID_Hair, RFLiArcID_Cap, RFLiArcID_Beard, RFLiArcID_Nline, RFLiArcID_Mask, RFLiArcID_Glass};
    unsigned shapes = 0, textures = 0;
    for (unsigned arc = 0; arc < RFLiArcID_Max; ++arc) {
        for (unsigned file = 0; file < loader.archives[arc].numFiles; ++file) {
            const auto size = RFLPCResourceSize(&loader, arc, file);
            std::vector<u8> bytes(size + 32, 0xD3);
            RFLPCLoadResource(bytes.data(), &loader, arc, file);
            const auto shape = std::find(shapeArcs.begin(), shapeArcs.end(), arc);
            if (shape != shapeArcs.end()) {
                RFLiShapeRes metadata{};
                metadata.part = RFLiPartsShp(shape - shapeArcs.begin());
                metadata.vtxPosBufSize = metadata.vtxNrmBufSize = metadata.vtxTxcBufSize = 1024;
                metadata.dlBufSize = 65535;
                RFLPCConvertShape(&metadata, bytes.data(), size);
                ++shapes;
                RFLPCLoadResource(bytes.data(), &loader, arc, file);
                reject([&] { RFLPCConvertShape(&metadata, bytes.data(), 3); });
            } else {
                RFLPCConvertTexture(reinterpret_cast<RFLiTexture*>(bytes.data()), size);
                const auto* texture = reinterpret_cast<const RFLiTexture*>(bytes.data());
                assert(texture->width > 0 && texture->height > 0 && texture->imageOfs < size);
                ++textures;
                RFLPCLoadResource(bytes.data(), &loader, arc, file);
                reject([&] { RFLPCConvertTexture(reinterpret_cast<RFLiTexture*>(bytes.data()), 31); });
            }
            assert(std::all_of(bytes.begin() + size, bytes.end(), [](u8 value) { return value == 0xD3; }));
        }
    }
    assert(shapes > 100 && textures > 100);
    std::cout << shapes << " face shapes and " << textures << " textures converted\n";
    return 0;
}
