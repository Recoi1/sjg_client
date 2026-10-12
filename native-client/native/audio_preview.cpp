#include "ast_player.hpp"
#include <SDL3/SDL_timer.h>
#include <cmath>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            std::cout << "Usage: audio_preview.exe FILE.ast [--loop] [--seconds N]\n";
            return 0;
        }
        bool loop = false;
        double seconds = 0;
        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--loop") loop = true;
            else if (arg == "--seconds" && i + 1 < argc) {
                const std::string value = argv[++i];
                std::size_t used;
                seconds = std::stod(value, &used);
                if (used != value.size() || !std::isfinite(seconds) || seconds <= 0 || seconds > 86400)
                    throw std::runtime_error("Seconds must be between 0 and 86400");
            } else throw std::runtime_error("Unknown or incomplete playback option");
        }
        const auto bytes = galaxy::readAstFile(argv[1]);
        galaxy::Ast ast(bytes);
        galaxy::AstPlayer player(ast, loop);
        player.setVolume(0.25f);
        player.pump();
        player.pause(false);
        std::cout << "Playing " << ast.channels() << " channels at " << ast.sampleRate()
                  << " Hz; " << double(ast.frames()) / ast.sampleRate() << " seconds; device backend "
                  << SDL_GetCurrentAudioDriver() << "; volume 25%\n" << std::flush;
        const auto start = SDL_GetTicks();
        while (!player.finished() && (!seconds || SDL_GetTicks() - start < seconds * 1000)) {
            player.pump();
            SDL_Delay(5);
        }
        if (player.finished()) SDL_Delay(100); // Let the final device buffer finish.
        player.pause(true);
        std::cout << "Playback complete; submitted " << player.submittedFrames() << " frames\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Audio: " << error.what() << '\n';
        return 1;
    }
}
