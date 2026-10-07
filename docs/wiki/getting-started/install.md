# Install and launch

The supported native reference host is Ubuntu 24.04 on Linux x64. Engine builds
use C++23 with Clang/LLVM 18 and LLD. The optional editor uses Qt 6.4-compatible
Widgets; Qt stays outside the engine SDK.

## 1. Get the source

```bash
git clone https://github.com/Alegruz/Ludus.git
cd Ludus
```

Keep this engine checkout separate from the game projects you create.

## 2. Prepare the editor

```bash
./init.sh --cli --with-editor
```

Initialization prepares pinned tools and dependencies, installs missing supported
system prerequisites when needed, and builds the selected native editor. It can
download dependencies and request a sudo password for system packages.

For the graphical setup window, run `./init.sh` on a desktop and select
**Build Ludus editor** before **Initialize**. Cancelling that window starts no
setup actions. Tests are opt-in; `--with-tests` prepares test targets and
`--run-tests` also runs them.

If you manage system packages yourself, install the required toolchain and Qt,
then use `./init.sh --cli --with-editor --no-system-install`.

## 3. Launch

```bash
./scripts/editor --preset linux-clang-development
```

The launcher finds the already-built editor. It does not build or install it.
The Welcome page offers New Project, Open Project and recent projects.

Continue with [your first project](first-project.md).

## Engine contributors

Use `./init.sh --cli --with-tests` to prepare native presets and tests. Then run:

```bash
./scripts/build linux-clang-development
./scripts/test linux-clang-development
```

See [contributing](../contribute/index.md) for the complete review gates.

## Detailed references

- [Onboarding flags and supported tools](https://github.com/Alegruz/Ludus/blob/main/README.md#quick-start)
- [Build trees, presets and IDE setup](../../development/building.md)
- [Editor prerequisites and manual build](../../development/editor-workspace.md)
