# Engine configuration

Project build settings answer **how this game builds and launches**. Engine
configuration answers **which typed runtime settings a supported host uses**.
The Configuration tab is an offline preview with explicit file loading.

## Use the Configuration workspace

1. Open **Configuration** in the editor.
2. Inspect shared defaults, types, help, effective values, source and apply policy.
3. Load a cooked project bundle or preference file explicitly.
4. Edit a preference or reset it to its inherited value.
5. Save a sparse preferences file. Only explicit preference overrides are saved.

Saving detects changes to a previously loaded preference file. On conflict,
preserve the draft and reconcile the external edit before retrying. This check is
best-effort revision detection, not a general interprocess file lock.

These edits affect a future launch. They do not mutate a running GameHost. Closing
a project preserves this independent preview; quitting prompts for its unsaved
preferences.

## Cook a small host layer

The initial host schema includes `host.headless` and `host.max_frames`. Create:

```toml
# project.toml
[host]
headless = true
max_frames = 120
```

With `ludus_config` built in the engine checkout:

```bash
python3 tools/configuration/cook.py project.toml project.config.json \
    --validator out/build/linux-clang-development/tools/configuration/ludus-config
```

TOML cooking requires Python 3.11+; the ordinary host tools retain Python 3.10+
support. The cooker validates against the actual runtime schema before publishing.

GameHost accepts explicit `--config`, `--preferences` and supported command-line
overrides. An explicit CLI override wins over the corresponding preference and
project value. Use the schema/validator rather than guessing a JSON payload.

Read the [typed configuration design](../../architecture/engine-configuration.md)
for layers, transactions, source explanations, diagnostics and host launch examples.
