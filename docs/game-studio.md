# JaehoGameStudio

[Back to README](../README.md) · [Plugin package](../tools/JaehoGameStudio/README.md)

## Layout

All shared definitions, including the `jgs-sync` maintenance skill, live in the
`tools/JaehoGameStudio` submodule. The engine stores relative path registrations.
There are no shared definition copies, symlinks or junctions in the engine.

```text
jEngine/
├── AGENTS.md, CLAUDE.md
├── .agents/plugins/marketplace.json  # Codex plugin source
├── .codex/config.toml                # Role config_file paths
├── .claude/settings.json             # Claude marketplace source
├── .jgs-mirror.json                  # Relative package path
├── tools/JaehoGameStudio/
│   ├── plugin.json
│   ├── .claude-plugin/
│   ├── agents/jgs-*.md
│   ├── codex-agents/jgs-*.toml
│   ├── skills/jgs-sync/SKILL.md
│   ├── roles/, team.json
│   └── scripts/
└── .jaeho-studio/                    # Private project records
```

The seven roles are `jgs-director`, `jgs-core`, `jgs-graphics`, `jgs-editor`,
`jgs-network`, `jgs-devrel` and `jgs-particles`. Developer roles are agents;
`jgs-sync` is the shared maintenance procedure. Models, reasoning and permissions
inherit the host settings.

## Get the package

```sh
git submodule update --init -- tools/JaehoGameStudio
```

The committed configuration already points to the package. No copying or link
creation is needed after checkout. Codex reads project configuration after the
project is trusted; open a new session after changing role registrations.

## Load the plugin

Codex agent roles use `config_file` paths in `.codex/config.toml`. Plugin skills
use the marketplace registration:

```sh
codex plugin marketplace add .
codex plugin add jaeho-game-studio@jengine-tools
```

Claude Code reads the current submodule directly with:

```sh
claude --plugin-dir ./tools/JaehoGameStudio
```

This loads its seven agents and `/jaeho-game-studio:jgs-sync` from the package.
Use `/reload-plugins` for changes during a running Claude session. The project
Claude settings also register the local marketplace. A normal marketplace
installation uses a client-managed cache; use `--plugin-dir` for direct source
loading. Client installation caches are separate from project definitions.

## Edit shared agents and skills

Edit the package's `agents/` or `codex-agents/`. Keep both formats and the shared
role data consistent using Python 3.11 or later:

```sh
python3 tools/JaehoGameStudio/scripts/sync-studio.py --project .
python3 tools/JaehoGameStudio/scripts/sync-studio.py --project . --check
```

Windows can use the installed `python` or `py -3.11`. The script has no additional
dependencies. It mirrors native formats inside the package and maintains path
registrations for new roles. Common skills stay in the single package `skills/`
tree. Conflicting edits stop for review; `--check` is read-only.

Old unchanged project copies are retired during initial migration. Local edits
stop migration so they can be preserved in the package first. Unrelated user
agents and configuration remain intact. The explicit `--legacy-copies` option
exists for older clients, not this layout. No watcher, hook or update job is used.

## Update and preserve context

```sh
git pull
git submodule update --init -- tools/JaehoGameStudio
```

The engine pins an exact package commit. Ordinary role-body and common-skill
changes require no project definition synchronization. New role registrations
are committed with the updated submodule pointer. Publish package changes first.

Actual decisions, contracts and role checkpoints stay in `.jaeho-studio/`.
The optional initializer creates missing records while preserving existing data:

```sh
python3 tools/JaehoGameStudio/scripts/init-project.py .
```

Format references: [Codex config_file](https://learn.chatgpt.com/docs/config-file/config-reference),
[OpenAI plugin packaging](https://developers.openai.com/plugins/build/plugins),
[Claude direct plugin loading](https://code.claude.com/docs/en/plugins/create).
