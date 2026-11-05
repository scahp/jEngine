# JaehoGameStudio development tools

[JaehoGameStudio](https://github.com/scahp/JaehoGameStudio) provides seven development agents for **Claude Code and Codex**, the `jgs-sync` maintenance skill, and OpenClaw role guidance. jEngine pins the package as the `tools/JaehoGameStudio` submodule. CMake and the engine executable do not depend on it.

## Get the pinned package

From the repository root:

```sh
git submodule update --init -- tools/JaehoGameStudio
```

A recursive clone also fetches the package. This checkout includes the following project files, so both tools can discover the roles without a separate plugin installation:

| Path | Purpose |
| --- | --- |
| `.codex/agents/jgs-*.toml` | Codex custom agents |
| `.claude/agents/jgs-*.md` | Claude Code custom agents |
| `.agents/skills/<name>/` | Codex skills |
| `.claude/skills/<name>/` | Claude Code skills |
| `AGENTS.md` and `CLAUDE.md` | Shared maintenance instructions; Claude imports `AGENTS.md` |
| `.jgs-mirror.json` | Relative package path and last synchronized hashes |
| `.gitattributes` studio block | LF checkout rules scoped to mirrored files |

All copies are regular files, including on Windows. Scoped LF rules keep mirror hashes consistent with the submodule while preserving the engine's existing source-file line endings. New Codex agents should be checked in a new conversation. The agents leave model, reasoning, tools and permissions to the existing host settings.

Developer roles are agents only. Invoke the maintenance workflow with `$jgs-sync` in Codex or `/jgs-sync` in Claude Code. For development work, ask the director to delegate to `jgs-core`, `jgs-graphics`, `jgs-editor`, `jgs-network`, `jgs-devrel` and `jgs-particles` as appropriate.

## Mirror edits in either direction

The agent instructions, maintenance skill and project instructions require mirroring after creating or editing an agent or skill. **Python 3.11 or later** is needed for synchronization; no additional Python packages are required. On Windows, use the installed interpreter, for example `py -3.11` instead of `python3`.

```sh
python3 tools/JaehoGameStudio/scripts/sync-studio.py --project .
python3 tools/JaehoGameStudio/scripts/sync-studio.py --project . --check
```

An edit in `.claude/agents/` updates the shared role and Codex agent, and a Codex edit updates Claude. New `jgs-*` agents are discovered dynamically. Skills added or edited in either `.claude/skills/` or `.agents/skills/` update the common `skills/` tree and the other tool's copy. Scripts, references, assets and executable bits are included.

The synchronizer compares each tree with its own last synchronized hashes. Conflicting edits abort before any write; it does not pick a winner by file time. `--check` is read-only. Missing copies are restored, so deletion or renaming requires an explicit coordinated retirement of the component and its state entries.

The package explicitly retires the old developer-role skills through `retired-skills.json`. Unchanged published copies are removed from both native skill trees and mirror state, preventing resurrection. Local edits or unknown resources stop the entire transaction for review; preserve useful content in the corresponding agent before retiring it.

Synchronization runs through the AI maintenance workflow or the explicit command. No background file watcher is installed. Client plugin caches are not editable sources. Unrelated user agents, configuration and project checkpoints are preserved.

## Optional plugin installation

Project-local discovery above is sufficient. The package also retains both native plugin manifests and marketplaces for users who prefer plugin installation.

Codex, from this repository root:

```sh
codex plugin marketplace add .
codex plugin add jaeho-game-studio@jengine-tools
```

Claude Code can load the pinned plugin directly:

```sh
claude --plugin-dir ./tools/JaehoGameStudio
```

Its maintenance skill is namespaced as `/jaeho-game-studio:jgs-sync`. Installed plugin updates may require reloading the client. Native Codex agent registration is separate from plugin installation; the project files and synchronizer provide that registration.

OpenClaw can consume the maintenance skill and use its registered role agents. Independent OpenClaw agent registration and routing remain host-specific; `team.json` and `openclaw-routing.json` are reference data. See the [package README](../tools/JaehoGameStudio/README.md).

## Keep project context separate

Actual engine decisions, contracts and role checkpoints stay in the parent checkout's `.jaeho-studio/` directory. Claude Code, Codex and OpenClaw use those files for handoff; their conversation histories remain separate. The optional initializer creates missing records without replacing existing files:

```sh
python3 tools/JaehoGameStudio/scripts/init-project.py .
```

## Update the package and project together

After reviewing a new package revision:

```sh
git -C tools/JaehoGameStudio fetch origin
git -C tools/JaehoGameStudio checkout <reviewed-commit>
python3 tools/JaehoGameStudio/scripts/sync-studio.py --project .
python3 tools/JaehoGameStudio/scripts/sync-studio.py --project . --check
```

Commit the reviewed submodule pointer, both tools' native files and `.jgs-mirror.json` together. If a local edit changes the shared package during synchronization, publish that package commit first. A remote `main` change never updates the parent's pin automatically.

The package CI checks mirror consistency and Python regressions on macOS, Windows and Linux. Format references: [Codex custom agents](https://learn.chatgpt.com/docs/agent-configuration/subagents#custom-agents), [Codex skills](https://learn.chatgpt.com/docs/build-skills), [Claude agents](https://code.claude.com/docs/en/sub-agents), [Claude plugin layout](https://code.claude.com/docs/en/plugins-reference).
