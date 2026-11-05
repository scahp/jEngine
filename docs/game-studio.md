# JaehoGameStudio development tools

[JaehoGameStudio](https://github.com/scahp/JaehoGameStudio) supplies seven AI development roles for Codex and OpenClaw. jEngine keeps the shared package as a Git submodule at **tools/JaehoGameStudio**. It is a development aid; CMake and the engine executable do not depend on it.

## Get the pinned package

From the jEngine repository root:

```sh
git submodule update --init -- tools/JaehoGameStudio
```

A new checkout can instead use `git clone --recurse-submodules https://github.com/scahp/jEngine.git`. The parent repository pins a specific plugin commit. A new commit on the plugin's remote `main` does not update that pin automatically.

## Use the roles

The repository marketplace at `.agents/plugins/marketplace.json` exposes **jEngine development tools**. Select that source in the desktop Plugins directory and install **JaehoGameStudio**. For the CLI, register the marketplace before installing, from the repository root:

```sh
codex plugin marketplace add .
codex plugin add jaeho-game-studio@jengine-tools
```

The project `.codex/config.toml` enables this package for trusted local projects. Host policies and authentication still apply. A client that has not discovered the new local marketplace may need an app restart.

The package provides `$jgs-director`, `$jgs-graphics`, `$jgs-editor`, `$jgs-core`, `$jgs-network`, `$jgs-devrel` and `$jgs-particles` skills. To register matching project-scoped custom agents for delegation, run:

```sh
python3 tools/JaehoGameStudio/scripts/install-codex-agents.py .
```

Use Python 3.9 or later; on Windows, replace `python3` with `py -3` or `python`. The installer creates `.codex/agents/jgs-*.toml`. It preserves identical definitions and reports conflicting files for review. It does not set a model, reasoning effort, permissions or global settings. Start a new Codex conversation after adding custom agents.

Example request:

> `$jgs-director` Review asset publication in this jEngine checkout. Discuss the ownership contract with core, graphics and editor before assigning implementation, then collect focused verification results.

OpenClaw can consume the same role skills. Its independent-agent registration and routing are host-specific; see the [plugin's instructions](../tools/JaehoGameStudio/README.md). The supplied routing JSON is reference data and is not applied automatically.

## Keep project context separate

Shared role instructions live in the submodule. jEngine's actual decisions, contracts and role checkpoints stay in the parent checkout's `.jaeho-studio/` directory. Installing or updating the plugin does not replace those records.

For a project without these records, the optional initializer only creates missing files:

```sh
python3 tools/JaehoGameStudio/scripts/init-project.py .
```

These context files are local under the existing jEngine ignore policy. Keep credentials and personal conversation history out of the shared plugin. Codex and OpenClaw use the project checkpoints for handoff; their conversation histories remain separate.

## Update deliberately

```sh
git -C tools/JaehoGameStudio fetch origin
git -C tools/JaehoGameStudio checkout <reviewed-commit>
git add tools/JaehoGameStudio
git commit -m "Update JaehoGameStudio development tools"
```

Review existing project agent definitions when role instructions change. The installer preserves conflicting files instead of replacing local edits. Commit the parent pointer when adopting a new plugin revision; keep normal jEngine batch/integration rules for publishing it.

Format references: [official OpenAI plugin packaging](https://developers.openai.com/plugins/build/plugins) and [custom agent configuration](https://learn.chatgpt.com/docs/agent-configuration/subagents#custom-agents).
