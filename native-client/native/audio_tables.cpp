#include "Game/RhythmLib/AudChordInfo.hpp"
#include "Game/RhythmLib/AudMePlayer.hpp"
#include "Game/Speaker/SpkTable.hpp"
#include "Game/Speaker/SpkWave.hpp"
#include "JSystem/JKernel/JKRArchive.hpp"
#include <climits>
#include <algorithm>
#include <cstring>
#include <span>
#include <stdexcept>

namespace {
static_assert(sizeof(SpkParameters) == 8 && sizeof(AudMePlayingParams) == 8 && sizeof(AudChordData) == 8 && sizeof(WaveData) == 12);
void require(bool valid) { if (!valid) throw std::runtime_error("Invalid native audio table"); }
void range(size_t offset, size_t length, size_t size) { require(offset <= size && length <= size - offset); }
u32 big(std::span<const u8> bytes, size_t offset, size_t width = 4) {
    range(offset, width, bytes.size()); u32 value = 0;
    for (size_t i = 0; i < width; ++i) value = (value << 8) | bytes[offset + i];
    return value;
}
std::span<const u8> input(void* source, size_t size) {
    require(source != nullptr);
    if (!size) { const auto known = JKRFileLoader::getGlbResourceSize(source); require(known >= 0); size = size_t(known); }
    // shortcut: 64 MiB per audio table, raise only for a verified larger asset.
    require(size && size <= 64 * 1024 * 1024);
    return {static_cast<const u8*>(source), size};
}
bool overlaps(size_t a, size_t an, size_t b, size_t bn) { return an && bn && a < b + bn && b < a + an; }
struct NamedTable {
    std::vector<u8> bytes;
    std::vector<const char*> names;
    size_t parameterOffset;
};
NamedTable namedTable(void* source, size_t size, size_t header) {
    const auto bytes = input(source, size); range(0, header, bytes.size());
    const size_t count = big(bytes, 0), params = big(bytes, 4), names = big(bytes, 8);
    require(count <= 65536 && count <= bytes.size() / 8 && params >= header && names >= header);
    range(params, count * 8, bytes.size()); range(names, count * 4, bytes.size());
    require(!overlaps(params, count * 8, names, count * 4));
    if (header == 16) require(big(bytes, 12) == 0);
    // shortcut: 65536 names, 4096 bytes per name; raise only for verified larger audio metadata.
    NamedTable result{{bytes.begin(), bytes.end()}, {}, params};
    result.names.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const size_t offset = big(bytes, names + i * 4);
        require(offset >= header && offset < bytes.size());
        const auto* end = static_cast<const u8*>(std::memchr(bytes.data() + offset, 0, std::min(size_t(4096), bytes.size() - offset)));
        require(end != nullptr);
        const size_t length = size_t(end - bytes.data() - offset + 1);
        require(!overlaps(offset, length, params, count * 8) && !overlaps(offset, length, names, count * 4));
        result.names.push_back(reinterpret_cast<const char*>(result.bytes.data() + offset));
    }
    return result;
}
}

SpkTable::SpkTable() : mInitialized(false), mResourceCount(0), mParameters(nullptr), mNames(nullptr) {}
void SpkTable::setResource(void* source, size_t size) {
    auto table = namedTable(source, size, 16);
    const std::span<const u8> bytes(table.bytes);
    std::vector<SpkParameters> parameters;
    for (size_t i = 0; i < table.names.size(); ++i) {
        const size_t p = table.parameterOffset + i * 8;
        parameters.push_back({u16(big(bytes, p, 2)), bytes[p+2], bytes[p+3], u16(big(bytes,p+4,2)), u16(big(bytes,p+6,2))});
    }
    mNativeBytes = std::move(table.bytes); mNativeNames = std::move(table.names); mNativeParameters = std::move(parameters);
    mResourceCount = s32(mNativeNames.size()); mNames = mNativeNames.data(); mParameters = mNativeParameters.data();
    mInitialized = true;
}
void AudMePlayingParamsHolder::setResource(void* source, size_t size) {
    auto table = namedTable(source, size, 12);
    const std::span<const u8> bytes(table.bytes);
    std::vector<AudMePlayingParams> parameters;
    for (size_t i = 0; i < table.names.size(); ++i) {
        const size_t p = table.parameterOffset + i * 8;
        parameters.push_back({bytes[p], bytes[p+1], bytes[p+2], bytes[p+3], u16(big(bytes,p+4,2)), bytes[p+6], bytes[p+7]});
    }
    mNativeBytes = std::move(table.bytes); mNativeNames = std::move(table.names); mNativeParameters = std::move(parameters);
    mNumEntries = u32(mNativeNames.size()); mNames = mNativeNames.data(); mParams = mNativeParameters.data();
}

void AudMeMgr::setMeSeq(void* source, size_t size) {
    if (!source) { mMeSeq = nullptr; mNativeSeqBytes.clear(); return; }
    const auto bytes = input(source, size);
    const u32 count = big(bytes, 0);
    require(count <= 65536); range(4, size_t(count) * 4, bytes.size());
    std::vector<u8> owned(bytes.begin(), bytes.end());
    std::memcpy(owned.data(), &count, 4);
    for (u32 i = 0; i < count; ++i) {
        const u32 offset = big(bytes, 4 + size_t(i) * 4);
        require(offset == 0xffffffff || (offset >= 4 + size_t(count) * 4 && offset < bytes.size()));
        std::memcpy(owned.data() + 4 + size_t(i) * 4, &offset, 4);
    }
    // Only the offset table is native; sequence instructions keep their Wii byte order.
    mNativeSeqBytes = std::move(owned);
    mMeSeq = reinterpret_cast<AudMeTable*>(mNativeSeqBytes.data());
}

AudChordTable::AudChordTable() : mLoaded(false), mChordCount(0), mScaleCount(0), mChordPtr(nullptr), mScalePtr(nullptr) {}
bool AudChordTable::setChordTable(s32 id, JKRArchive* archive) {
    if (!archive || id < 0 || id > 0xffff) { mLoaded = false; return false; }
    return setChordTableResource(archive->getResource(u16(id)));
}
bool AudChordTable::setChordTableResource(void* source, size_t size) {
    mLoaded = false;
    try {
        auto bytes = input(source, size); range(0,16,bytes.size());
        require(big(bytes,0) == 0 && std::memcmp(bytes.data()+4,"CITS",4) == 0);
        const size_t extent = big(bytes,8); require(extent >= 16 && extent <= bytes.size()); bytes = bytes.first(extent);
        const size_t chords = big(bytes,12,2), scales = big(bytes,14,2), metadata = 16 + 4*(chords+scales);
        range(16,4*(chords+scales),bytes.size());
        std::vector<u8> owned(bytes.begin(),bytes.end());
        std::vector<AudChordData*> chordPointers;
        std::vector<AudScaleData> scaleData;
        for(size_t i=0;i<chords;++i) {
            const size_t p=big(bytes,16+i*4); require(p >= metadata); range(p,8,bytes.size());
            chordPointers.push_back(reinterpret_cast<AudChordData*>(owned.data()+p));
        }
        for(size_t i=0;i<scales;++i) {
            const size_t p=big(bytes,16+chords*4+i*4); require(p >= metadata); range(p,8,bytes.size());
            const size_t up=big(bytes,p), down=big(bytes,p+4);
            require(up >= metadata && down >= metadata); range(up,12,bytes.size()); range(down,12,bytes.size());
            scaleData.push_back({owned.data()+up,owned.data()+down});
        }
        mNativeBytes=std::move(owned); mNativeChords=std::move(chordPointers); mNativeScales=std::move(scaleData);
        mNativeScalePointers.resize(scales);
        for(size_t i=0;i<scales;++i) mNativeScalePointers[i]=&mNativeScales[i];
        mChordCount=int(chords); mScaleCount=int(scales); mChordPtr=mNativeChords.data(); mScalePtr=mNativeScalePointers.data();
        mLoaded=true;
        return true;
    } catch(const std::runtime_error&) { return false; }
}

SpkWave::SpkWave() : mResource(nullptr) {}
void SpkWave::setResource(void* source, size_t size) {
    const auto bytes=input(source,size); range(0,8,bytes.size());
    // shortcut: 4096 waves per bank; raise only for a verified larger speaker bank.
    const size_t extent=big(bytes,0), count=big(bytes,4);
    require(extent >= 8 && extent <= bytes.size() && count <= 4096 && count <= (extent-8)/4);
    std::vector<u8> owned(bytes.begin(),bytes.begin()+extent);
    std::vector<u32> offsets;
    std::vector<std::pair<size_t,size_t>> records;
    for(size_t i=0;i<count;++i) {
        const size_t p=big(bytes,8+i*4); require(p >= 8+count*4 && !(p&3)); range(p,12,extent);
        const u32 length=big(bytes,p), start=big(bytes,p+4), end=big(bytes,p+8);
        require(length <= INT_MAX && !(length&1)); range(p+12,length,extent);
        require((start==0xffffffff && end==0xffffffff) || (start <= end && end <= length/2));
        bool shared=false;
        for(const auto& record:records) {
            if(record.first==p) { shared=true; break; }
            require(!overlaps(p,12+length,record.first,record.second));
        }
        if(!shared) {
            records.emplace_back(p,12+length);
            std::memcpy(owned.data()+p,&length,4); std::memcpy(owned.data()+p+4,&start,4); std::memcpy(owned.data()+p+8,&end,4);
            for(size_t sample=0;sample<length;sample+=2) { const u16 value=u16(big(bytes,p+12+sample,2)); std::memcpy(owned.data()+p+12+sample,&value,2); }
        }
        offsets.push_back(u32(p));
    }
    // shortcut: replace resources on the loader thread; add synchronization when concurrent reloads are used.
    mNativeBytes=std::move(owned); mNativeOffsets=std::move(offsets); mResource=mNativeBytes.data();
}
WaveData* SpkWave::getWaveData(s32 wave) const {
    if(wave < 0 || size_t(wave) >= mNativeOffsets.size()) return nullptr;
    return reinterpret_cast<WaveData*>(const_cast<u8*>(mNativeBytes.data())+mNativeOffsets[wave]);
}
s32 SpkWave::getWaveSize(s32 wave) const { const auto* data=getWaveData(wave); return data ? s32(data->mSize) : 0; }
u32 SpkWave::getLoopStartPos(s32 wave) const { const auto* data=getWaveData(wave); return data ? data->mLoopStartPos : 0; }
u32 SpkWave::getLoopEndPos(s32 wave) const { const auto* data=getWaveData(wave); return data ? data->mLoopEndPos : 0; }
s16* SpkWave::getWave(s32 wave) const { auto* data=getWaveData(wave); return data ? data->mWave : nullptr; }
