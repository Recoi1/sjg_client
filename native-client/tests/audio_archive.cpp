#include "audio_archive.hpp"
#include "yaz0.hpp"
#include "JSystem/JAudio2/JASBNKParser.hpp"
#include "JSystem/JAudio2/JASBasicBank.hpp"
#include "JSystem/JAudio2/JASWSParser.hpp"
#include "JSystem/JAudio2/JAUSoundTable.hpp"
#include "JSystem/JAudio2/JAUSeqCollection.hpp"
#include "JSystem/JAudio2/JASSeqReader.hpp"
#include "JSystem/JKernel/JKRExpHeap.hpp"
#include "crash.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
static u32 word(const std::vector<u8>& b, size_t p) { u32 value; memcpy(&value, b.data() + p, 4); return value; }
int main(int argc, char** argv) {
    assert(argc == 2); galaxy::installCrashHandler();
    u8 operands[]{0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc};
    JASSeqReader reader; reader.init(operands);
    assert(reader.get16(1) == 0x3456 && reader.get24(0) == 0x123456 && reader.get32(1) == 0x3456789a);
    assert(reader.read16() == 0x1234 && reader.getOffset() == 2);
    assert(reader.read24() == 0x56789a && reader.getOffset() == 5);
    assert(reader.call(0) && reader.read24() == 0x123456 && reader.ret() && reader.getOffset() == 5);
    std::ifstream f(argv[1], std::ios::binary); assert(f);
    const std::vector<u8> packed{std::istreambuf_iterator<char>(f), {}};
    const auto source = galaxy::decodeYaz0(packed); const auto native = galaxy::importAudioArchive(source);
    auto reject = [](const std::vector<u8>& data) { bool failed = false; try { galaxy::importAudioArchive(data); } catch (const std::invalid_argument&) { failed = true; } assert(failed); };
    for (size_t size = 0; size < 64; ++size) reject({source.begin(), source.begin() + size});
    auto invalid = source; invalid[0] = 0; reject(invalid);
    invalid = source; invalid[8] = invalid[9] = invalid[10] = invalid[11] = 0xff; reject(invalid);
    invalid = source; invalid.resize(source.size() - 32); reject(invalid);
    auto* root = JKRExpHeap::createRoot(1, false); assert(root);
    auto* heap = JKRExpHeap::create(8 * 1024 * 1024, root, false); assert(heap); root->becomeCurrentHeap();
    unsigned banks = 0, waves = 0, sequenceCount = 0;
    for (size_t p = 4; word(native, p) != 0x3e5f4141;) {
        const auto tag = word(native, p); p += 4;
        if (tag == 0x77732020 || tag == 0x626e6b20) {
            const auto offset = word(native, p + 4);
            if (tag == 0x77732020) { assert(JASWSParser::createWaveBank(native.data() + offset, heap)); ++waves; p += 12; }
            else {
                auto* bank = JASBNKParser::createBasicBank(native.data() + offset, heap); assert(bank); ++banks; p += 8;
                for (unsigned j = 0; j < INST_NUM_MAX; ++j) if (auto* inst = bank->getInst(j)) {
                    JASInstParam param; inst->getParam(60, 127, &param);
                }
            }
        } else {
            const auto offset = word(native, p);
            if (tag == 0x62737420) {
                JAUSoundTable table(false); table.init(native.data() + offset); assert(table.isValid());
                assert(table.mTable.getSection(0)->mNumGroups > 0);
                for (unsigned s = 0; s < 3; ++s) for (int g = 0; g < table.mTable.getSection(s)->mNumGroups; ++g)
                    for (int i = 0; i < table.mTable.getGroup(table.mTable.getSection(s), g)->mNumItems; ++i) {
                        const JAISoundID id(s << 24 | g << 16 | i);
                        assert(id.getSectionID() == s && id.getGroupID() == g && id.getWaveID() == i);
                        if (table.getData(id)) assert((table.getTypeID(id) & 0xf0) == 0x50 + s * 0x10);
                    }
            } else if (tag == 0x6273746e) {
                JAUSoundNameTable table(false); table.init(native.data() + offset); assert(table.getNumGroups_inSection(0) > 0);
            } else {
                JAUSeqCollection collection; collection.init(native.data() + offset); assert(collection.isValid());
                for (int g = 0; g < collection.mGroupNum; ++g) {
                    const auto* table = reinterpret_cast<const JAUSeqCollectionSoundTable*>(native.data() + offset + collection.mSoundTableOffsets[g]);
                    for (unsigned i = 0; i < table->mWaveNum; ++i) {
                        JAISeqData data(nullptr, 0); assert(collection.getSeqData(g, i, &data)); ++sequenceCount;
                        assert(source[offset + data.offset] == native[offset + data.offset]);
                    }
                }
            }
            p += 8;
        }
        assert(heap->check() && root->check());
    }
    assert(banks == 48 && waves == 48 && sequenceCount > 100);
    assert(heap->check()); heap->destroy(); assert(root->check());
    std::cout << "Original audio readers: " << banks << " instrument banks, " << waves << " wave banks, " << sequenceCount << " sequences passed\n";
    return 0;
}
