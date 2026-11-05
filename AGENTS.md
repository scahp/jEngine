# jEngine development roles

JaehoGameStudio is the shared role and skill package at `tools/JaehoGameStudio`.
Project agents are registered in `.codex/agents/` and `.claude/agents/`; skills
are in `.agents/skills/` and `.claude/skills/`. A recursive checkout includes the
package. Role model, reasoning and permissions follow the existing host settings.

When creating or changing a studio agent or skill in either tool, also mirror
it to the other tool before finishing. Use Python 3.11 or later:

```sh
python3 tools/JaehoGameStudio/scripts/sync-studio.py --project .
python3 tools/JaehoGameStudio/scripts/sync-studio.py --project . --check
```

New shared agent names start with `jgs-`. New skill directories have a matching
`name` in `SKILL.md`. The script also mirrors skill scripts, references and
assets. Review conflicting edits rather than overwriting either copy. Missing
copies are restored; deletion and renaming require explicit coordinated changes.

Commit shared package changes in its repository first, then commit the engine's
submodule pointer, native copies and `.jgs-mirror.json` together. Edit these
sources rather than a client's installed plugin cache. See
`docs/game-studio.md` for installation and update instructions.

Engine decisions, contracts and role checkpoints belong in this checkout's
`.jaeho-studio/`. Preserve existing records and keep private conversation history,
credentials and host configuration out of the shared package.
