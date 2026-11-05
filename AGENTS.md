# jEngine development roles

The complete JaehoGameStudio plugin is the `tools/JaehoGameStudio` submodule.
Register paths only: Codex roles use `config_file` entries in
`.codex/config.toml`; the plugin marketplace points to the same package.
Claude loads it directly with `claude --plugin-dir ./tools/JaehoGameStudio`.
Do not copy shared definitions into this project's discovery folders or create
symlinks, junctions, file watchers or update jobs. Host-managed installed plugin
caches are separate from the source package.

Edit shared agents and skills inside the submodule. After changes on either
platform, mirror its native formats using Python 3.11 or later:

```sh
python3 tools/JaehoGameStudio/scripts/sync-studio.py --project .
python3 tools/JaehoGameStudio/scripts/sync-studio.py --project . --check
```

Claude definitions are `agents/`, Codex configurations are `codex-agents/`, and
shared procedures are `skills/` inside the package. Developer roles remain
agents only. Models, reasoning and permissions inherit existing host settings.
Preserve unrelated configuration and resolve conflicts without overwriting
local edits. Commit package changes first, then the engine's submodule pin and
any changed path registrations. See `docs/game-studio.md`.

Actual engine decisions, contracts and role checkpoints belong in this
checkout's `.jaeho-studio/`. Keep credentials, private conversation history and
host configuration out of the shared package.
