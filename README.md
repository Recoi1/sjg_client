# Galaxy PC experimental client

[Download experimental builds](https://github.com/Recoi1/sjg_client/releases)

Extract the entire release ZIP, including all seven DLLs, then open `galaxy.exe`. Drag your own Super Mario Galaxy USA RMGE01 revision 0 ISO, WBFS or RVZ into its window. It validates and extracts native resources locally and starts the game. No compiler is needed to use a release. Keep the original ROM at its imported location.

The public package contains only program files, required runtime libraries and notices. ROMs, extracted textures/models/audio, saves, HD packs, custom character models and replacement videos are excluded. The native-assets-only boot test uses an installation without mods. Never share the `game-data`, `userdata` or `mods` folders created locally.

Native Radmin LAN browsing and four-player character rosters are implemented. **Shared-level co-op gameplay is unfinished.** The clean package offers Mario and Luigi; custom character assets are not included. Separate-PC Radmin connectivity is unverified.

**Known launch issue:** a silent startup failure was reported on a second Windows 10 PC. Its cause remains unresolved. Passing local tests does not establish clean-machine compatibility, every-level stability or universal 240 FPS.

## Source and updates

`native-client/` contains the native implementation, tests and source patches against pinned public upstream repositories. `upstreams.json` records those repositories and commits; preserve their licenses. Run `tools/fetch-sources.ps1` from `native-client/` to obtain and patch upstream source. The source build uses CMake 3.25+, Ninja, Python 3 and Zig 0.15.2; the Zig wrappers expect `tools/zig/zig-x86_64-windows-0.15.2/zig.exe`. Then run `build-game.ps1` and build the `galaxy_portable` target. ROM-dependent tests require the developer's own local dump and prepared test resources; those resources are never uploaded.

Publishing runs from the development PC after the current binaries pass the full local regression suite, independent ROM import, native-assets-only boot and package-manifest checks. Packaging then automatically pushes the source snapshot and uploads an experimental GitHub Release with checksums. Failed checks stop publication. Credentials stay in the local credential manager.

`release-verification.json` records the published build's verified scope and limitations. Publishing is triggered by verified packaging; it does not continuously edit the game or run while this PC and its build process are stopped.
