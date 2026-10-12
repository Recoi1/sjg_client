#include "ast_player.hpp"
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>

int main() {
    // A synthetic four-channel stream exercises the real SDL queue without game assets.
    std::vector<std::uint8_t> bytes(64 + 32 + 4 * 32000);
    auto put16 = [&](std::size_t p, unsigned n) { bytes[p] = n >> 8; bytes[p + 1] = n; };
    auto put32 = [&](std::size_t p, unsigned n) { put16(p, n >> 16); put16(p + 2, n); };
    std::copy_n("STRM", 4, bytes.begin());
    put32(4, bytes.size() - 64); put16(8, 1); put16(10, 16); put16(12, 4);
    put16(14, 0xffff); put32(16, 32000); put32(20, 16000); put32(24, 200); put32(28, 16000);
    std::copy_n("BLCK", 4, bytes.begin() + 64); put32(68, 32000);
    galaxy::Ast ast(bytes);
    {
        galaxy::AstPlayer player(ast);
        player.setVolume(0.25f);
        try { player.setVolume(-1); assert(false); } catch (const std::invalid_argument&) {}
        try { player.setVolume(std::numeric_limits<float>::quiet_NaN()); assert(false); }
        catch (const std::invalid_argument&) {}
        player.pump();
        const auto queued = player.submittedFrames();
        SDL_Delay(30);
        player.pump();
        assert(player.submittedFrames() == queued); // Starts paused.
        player.pause(false);
        const auto start = SDL_GetTicks();
        while (!player.finished() && SDL_GetTicks() - start < 3000) {
            player.pump(); SDL_Delay(5);
        }
        assert(player.finished() && player.submittedFrames() == 16000);
        player.pause(true);
    }
    ast.seek(15999);
    {
        galaxy::AstPlayer player(ast, true);
        player.pump(); player.pause(false);
        const auto start = SDL_GetTicks();
        while (SDL_GetTicks() - start < 600) { player.pump(); SDL_Delay(5); }
        assert(!player.finished() && player.submittedFrames() > 16000);
        player.pause(true);
        const auto queued = player.submittedFrames();
        SDL_Delay(30); player.pump();
        assert(player.submittedFrames() == queued);
    }
    std::cout << "SDL audio queue, pause/resume, volume, EOF, looping, and cleanup passed\n";
}
