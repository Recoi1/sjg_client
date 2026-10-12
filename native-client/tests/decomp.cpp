#include "decomp.hpp"
#include <array>
#include <cassert>
#include <stdexcept>
#include <vector>
int main() {
    std::vector<u8> yay = {'Y','a','y','0',0,0,0,6,0,0,0,20,0,0,0,22,0xe0,0,0,0,0x10,2,'A','B','C'};
    std::array<u8, 9> output{};
    galaxy::decompress(yay, output); assert(std::equal(output.begin(), output.begin() + 6, "ABCABC") && output[6] == 0);
    output.fill(0x55); galaxy::decompress(yay, std::span<u8>(output).first(2), 3);
    assert(output[0] == 'A' && output[1] == 'B' && output[2] == 0x55);
    std::vector<u8> yaz = {'Y','a','z','0',0,0,0,6,0,0,0,0,0,0,0,0,0xe0,'A','B','C',0x10,2};
    output.fill(0); galaxy::decompress(yaz, output); assert(std::equal(output.begin(), output.begin() + 6, "ABCABC"));
    auto rejected = [&](auto source) { bool caught = false; try { galaxy::decompress(source, output); } catch (const std::exception&) { caught = true; } assert(caught); };
    for (size_t size = 0; size < yay.size(); ++size) rejected(std::span<const u8>(yay).first(size));
    yay[20] = 0x1f; rejected(yay);
    bool caught = false; try { galaxy::decompress(yaz, output, 7); } catch (const std::invalid_argument&) { caught = true; } assert(caught);
}
