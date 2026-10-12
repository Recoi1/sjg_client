# Galaxy development and publishing

The user authorized automatic updates to Recoi1/sjg_client. After changing the game, build and run the relevant checks, then the full regression suite and portable ROM-import/native-assets-only boot checks before packaging. tools/package-portable.py publishes automatically when github-publishing.json is enabled. Never publish a binary with failed or stale verification.

Use only the explicit source export and package allowlists. Never upload ROMs, extracted game assets, game-data, userdata, mods, HD packs, custom character assets, replacement videos, checkpoints, screenshots or credentials. Public builder game resources come from each recipient's own ROM. Preserve the user's regular save and existing local modded build.

Releases remain experimental while the reported second-PC Windows 10 silent launch failure and shared-level co-op integration are unresolved. State these limits honestly. Do not call lobby/transport checks playable co-op or smoke checks a full campaign playthrough.
