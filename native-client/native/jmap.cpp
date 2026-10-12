#include "Game/Util/JMapInfo.hpp"
#include "JSystem/JKernel/JKRFileLoader.hpp"
#include <algorithm>
#include <climits>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace {
static_assert(sizeof(JMapData) == 16 && sizeof(JMapItem) == 12);
u32 big(const u8* p, size_t width) {
    u32 value = 0;
    for (size_t i = 0; i < width; ++i) value = (value << 8) | p[i];
    return value;
}
template<class T> T read(const void* p) { T value; std::memcpy(&value, p, sizeof(value)); return value; }
template<class T> void put(void* p, T value) { std::memcpy(p, &value, sizeof(value)); }
const JMapItem* itemAt(const JMapInfo& info, int row, int field) {
    if (!info.mData || row < 0 || row >= info.getNumEntries() || field < 0 || field >= info.getNumFields()) return nullptr;
    return &info.mData->mItems[field];
}
const char* valueAt(const JMapInfo& info, int row, const JMapItem& item) {
    return getEntryAddress(info.mData, info.mData->mDataOffset, row) + item.mOffsData;
}
bool importString(const u8* input, size_t offset, std::map<size_t, std::string>& strings) {
    const char* text = reinterpret_cast<const char*>(input + offset);
    if (std::all_of(text, text + std::strlen(text), [](unsigned char c) { return c < 128; })) return true;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0)) return true;
    const int count = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (!count) return false;
    std::wstring wide(count, 0);
    if (!MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, text, -1, wide.data(), count)) return false;
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), count - 1, nullptr, 0, nullptr, nullptr);
    if (!size) return false;
    std::string utf8(size, 0);
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), count - 1, utf8.data(), size, nullptr, nullptr)) return false;
    strings.emplace(offset, std::move(utf8));
    return true;
}
}

JMapInfo::JMapInfo(const JMapInfo& other) : JMapInfo() { *this = other; }
JMapInfo& JMapInfo::operator=(const JMapInfo& other) {
    if (this == &other) return *this;
    mNativeData = other.mNativeData;
    mNativeStrings = other.mNativeStrings;
    mData = mNativeData.empty() ? nullptr : reinterpret_cast<const JMapData*>(mNativeData.data());
    mName = other.mName;
    mNativeSource = other.mNativeSource;
    return *this;
}

bool JMapInfo::attach(const void* source, size_t size) {
    if (!source) return false;
    if (!size) {
        const auto known = JKRFileLoader::getGlbResourceSize(source);
        if (known < 0) return false;
        size = static_cast<size_t>(known);
    }
    // shortcut: 64 MiB, 1024 fields and 16M field values per table; raise only for verified larger assets.
    if (size < 16 || size > 64 * 1024 * 1024) return false;
    const auto* input = static_cast<const u8*>(source);
    const size_t rows = big(input, 4), fields = big(input + 4, 4);
    const size_t data = big(input + 8, 4), stride = big(input + 12, 4);
    if (rows > INT_MAX || fields > 1024 || fields > (size - 16) / 12 || data > size || data < 16 + fields * 12 ||
        (rows && (!stride || rows > (size - data) / stride))) return false;
    if (rows && fields > (16 * 1024 * 1024) / rows) return false;
    const size_t strings = data + rows * stride;
    std::vector<u8> bytes(input, input + size);
    put(bytes.data(), u32(rows)); put(bytes.data() + 4, u32(fields));
    put(bytes.data() + 8, u32(data)); put(bytes.data() + 12, u32(stride));
    std::vector<std::pair<size_t, size_t>> slots;
    std::vector<std::pair<size_t, size_t>> inlineStrings;
    std::map<size_t, std::string> nativeStrings;
    for (size_t f = 0; f < fields; ++f) {
        const auto* raw = input + 16 + f * 12;
        const JMapItem item{big(raw, 4), big(raw + 4, 4), u16(big(raw + 8, 2)), raw[10], raw[11]};
        if (item.mType > JMAP_VALUE_TYPE_STRING_PTR || item.mShift >= 32 || item.mOffsData >= stride) return false;
        const size_t width = item.mType == JMAP_VALUE_TYPE_BYTE ? 1 : item.mType == JMAP_VALUE_TYPE_SHORT ? 2 : 4;
        if (item.mType != JMAP_VALUE_TYPE_STRING && width > stride - item.mOffsData) return false;
        put(bytes.data() + 16 + f * 12, item);
        bool shared = false;
        size_t stringWidth = 0;
        if (item.mType != JMAP_VALUE_TYPE_STRING) {
            for (const auto& slot : slots) {
                if (slot.first == item.mOffsData && slot.second == width) { shared = true; break; }
                if (slot.first < item.mOffsData + width && item.mOffsData < slot.first + slot.second) return false;
            }
            if (!shared) slots.emplace_back(item.mOffsData, width);
        }
        for (size_t row = 0; row < rows; ++row) {
            const size_t offset = data + row * stride + item.mOffsData;
            if (item.mType == JMAP_VALUE_TYPE_STRING) {
                const auto* end = static_cast<const u8*>(std::memchr(input + offset, 0, stride - item.mOffsData));
                if (!end) return false;
                if (!importString(input, offset, nativeStrings)) return false;
                stringWidth = std::max(stringWidth, size_t(end - input - offset + 1));
            } else {
                const auto value = big(input + offset, width);
                if (item.mType == JMAP_VALUE_TYPE_STRING_PTR &&
                    (value >= size - strings || !std::memchr(input + strings + value, 0, size - strings - value))) return false;
                if (item.mType == JMAP_VALUE_TYPE_STRING_PTR && !importString(input, strings + value, nativeStrings)) return false;
                if (!shared) {
                    if (width == 4) put(bytes.data() + offset, value);
                    else if (width == 2) put(bytes.data() + offset, u16(value));
                }
            }
        }
        if (stringWidth) inlineStrings.emplace_back(item.mOffsData, stringWidth);
    }
    for (const auto& text : inlineStrings)
        for (const auto& slot : slots)
            if (slot.first < text.first + text.second && text.first < slot.first + slot.second) return false;
    mNativeData = std::move(bytes);
    mNativeStrings = std::move(nativeStrings);
    mData = reinterpret_cast<const JMapData*>(mNativeData.data());
    mNativeSource = source;
    return true;
}

bool JMapInfo::getValueFast(int row, int field, const char** out) const {
    const auto* item = itemAt(*this, row, field);
    if (!item || !out) return false;
    const auto* value = valueAt(*this, row, *item);
    if (item->mType == JMAP_VALUE_TYPE_STRING) *out = value;
    else if (item->mType == JMAP_VALUE_TYPE_STRING_PTR)
        *out = getEntryAddress(mData, mData->mDataOffset, getNumEntries()) + read<u32>(value);
    else return false;
    const auto text = mNativeStrings.find(reinterpret_cast<const u8*>(*out) - mNativeData.data());
    if (text != mNativeStrings.end()) *out = text->second.c_str();
    return true;
}
bool JMapInfo::getValueFast(int row, int field, u32* out) const {
    const auto* item = itemAt(*this, row, field);
    if (!item || !out) return false;
    const auto* value = valueAt(*this, row, *item);
    u32 raw;
    switch (item->mType) {
    case JMAP_VALUE_TYPE_LONG: case JMAP_VALUE_TYPE_LONG_2: raw = read<u32>(value); break;
    case JMAP_VALUE_TYPE_SHORT: raw = read<u16>(value); break;
    case JMAP_VALUE_TYPE_BYTE: raw = read<u8>(value); break;
    default: return false;
    }
    *out = (raw & item->mMask) >> item->mShift;
    return true;
}
bool JMapInfo::getValueFast(int row, int field, s32* out) const {
    const auto* item = itemAt(*this, row, field);
    if (!item || !out || item->mShift) return false;
    const auto* value = valueAt(*this, row, *item);
    switch (item->mType) {
    case JMAP_VALUE_TYPE_LONG:
        if (item->mMask != 0xffffffff) return false;
        *out = read<s32>(value); break;
    case JMAP_VALUE_TYPE_SHORT:
        if (item->mMask != 0xffff) return false;
        *out = read<s16>(value); break;
    case JMAP_VALUE_TYPE_BYTE:
        if (item->mMask != 0xff) return false;
        *out = read<s8>(value); break;
    default: return false;
    }
    return true;
}
bool JMapInfo::getValueFast(int row, int field, f32* out) const {
    const auto* item = itemAt(*this, row, field);
    if (!item || !out || item->mType != JMAP_VALUE_TYPE_FLOAT) return false;
    *out = read<f32>(valueAt(*this, row, *item));
    return true;
}
bool JMapInfo::getValueFast(int row, int field, bool* out) const {
    u32 value;
    if (!out || !getValueFast(row, field, &value)) return false;
    *out = value != 0;
    return true;
}
