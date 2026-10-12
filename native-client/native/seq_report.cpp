#include "seq_report.hpp"
#include <array>
#include <charconv>
#include <stdexcept>
#include <vector>

namespace galaxy {
std::string readSequenceReport(std::span<const u8> sequence, size_t& position, u32 (*readRegister)(void*, u8), void* context) {
    size_t cursor = position;
    auto byte = [&]() -> u8 { if (cursor >= sequence.size()) throw std::invalid_argument("truncated sequence report"); return sequence[cursor++]; };
    std::string format;
    std::vector<char> types;
    while (true) {
        char ch = byte();
        if (!ch) break;
        if (format.size() >= 127) throw std::invalid_argument("sequence report format too long");
        if (ch == '\\') { ch = byte(); if (!ch) throw std::invalid_argument("truncated sequence report escape"); if (ch == 'n') ch = '\r'; }
        format += ch;
        if (ch == '%') {
            ch = byte();
            if (ch != '%' && ch != 'd' && ch != 'x' && ch != 's' && ch != 'r' && ch != 'R') throw std::invalid_argument("invalid sequence report conversion");
            format += ch;
            if (ch != '%') { if (types.size() == 4) throw std::invalid_argument("too many sequence report arguments"); types.push_back(ch); }
        }
    }
    std::array<u32, 4> arguments{};
    for (size_t i = 0; i < types.size(); ++i) {
        arguments[i] = byte();
        if (types[i] == 'r' || types[i] == 'R') {
            if (!readRegister) throw std::invalid_argument("sequence register reader unavailable");
            arguments[i] = readRegister(context, arguments[i]);
        }
    }
    std::string result;
    size_t argument = 0;
    for (size_t i = 0; i < format.size(); ++i) {
        if (format[i] != '%') { result += format[i]; continue; }
        const char type = format[++i];
        if (type == '%') { result += '%'; continue; }
        const u32 value = arguments[argument++];
        if (type == 's') {
            if (value >= sequence.size()) throw std::invalid_argument("sequence string offset out of bounds");
            size_t end = value;
            while (end < sequence.size() && sequence[end]) ++end;
            if (end == sequence.size()) throw std::invalid_argument("unterminated sequence string");
            result.append(reinterpret_cast<const char*>(sequence.data() + value), end - value);
        } else {
            char buffer[32];
            const auto converted = type == 'd' || type == 'r' ? std::to_chars(buffer, buffer + sizeof(buffer), s32(value)) : std::to_chars(buffer, buffer + sizeof(buffer), value, 16);
            result.append(buffer, converted.ptr);
        }
    }
    position = cursor;
    return result;
}
}
