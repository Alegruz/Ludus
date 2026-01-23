# CI/CD Quick Reference - Unit Testing

## Local Testing with Presets

### Quick Start
```bash
# Windows (MSVC)
cmake --preset test_msvc && cmake --build --preset test_msvc
out\build\test_msvc\bin\LudusEditor.exe

# Linux (Clang)
cmake --preset test_clang && cmake --build --preset test_clang
./out/build/test_clang/bin/LudusEditor

# Linux (GCC)
cmake --preset test_gcc && cmake --build --preset test_gcc
./out/build/test_gcc/bin/LudusEditor
```

## GitHub Actions Workflow

### File: `.github/workflows/unit-tests.yml`

**Triggers:**
- Push to `develop` or `main` branches
- Pull requests to `develop` or `main` branches

**Test Matrix:**
- ✅ Windows (MSVC Debug)
- ✅ Linux (Clang Debug)
- ✅ Linux (GCC Debug)

### Workflow Steps
1. Checkout code
2. Install platform-specific dependencies
3. Setup build tools (Ninja, ccache)
4. Configure with test preset
5. Build project
6. **Run unit tests** (automatic via `LUDUS_RUN_TESTS`)
7. Upload artifacts on failure

### Viewing Results
- Navigate to **Actions** tab in GitHub
- Select **Unit Tests** workflow
- View individual job results
- Download artifacts if tests fail

## CMake Presets Details

| Preset | Compiler | Platform | Defines |
|--------|----------|----------|---------|
| `test_msvc` | MSVC | Windows | `LUDUS_RUN_TESTS` |
| `test_clang` | Clang | Linux | `LUDUS_RUN_TESTS` |
| `test_gcc` | GCC | Linux | `LUDUS_RUN_TESTS` |

All test presets:
- Inherit from debug configurations
- Enable unit test compilation flag
- Use Ninja generator for fast builds
- Support parallel builds (`jobs: 0` = auto-detect)

## Adding New Tests

1. Write tests in `.cpp` file (see `CoreTests.cpp`)
2. Add file to `src/Engine/Core/CMakeLists.txt`
3. Tests auto-register via static initialization
4. Push to branch → CI runs automatically

## Troubleshooting CI

### Tests fail locally but pass in CI
- Check compiler differences (MSVC vs GCC/Clang)
- Verify platform-specific behavior
- Review sanitizer outputs on Linux

### Tests pass locally but fail in CI
- Ensure all dependencies are in `CMakeLists.txt`
- Check for hard-coded paths
- Verify test doesn't rely on local state

### Build fails in CI
- Check `.github/workflows/unit-tests.yml` logs
- Verify preset names match `CMakePresets.json`
- Ensure all required tools are installed in workflow

## Best Practices

✅ **DO:**
- Run tests locally before pushing
- Use presets for consistency
- Keep tests fast (< 5 seconds total)
- Write deterministic tests
- Test on all supported platforms

❌ **DON'T:**
- Commit code without running tests
- Write tests that depend on external state
- Use sleeps or arbitrary timeouts
- Hard-code platform-specific paths
- Skip CI failures without investigation

## Performance Notes

**CI Build Times (approximate):**
- Windows (MSVC): ~3-5 minutes
- Linux (Clang): ~2-4 minutes
- Linux (GCC): ~2-4 minutes

**Optimization:**
- ccache enabled on Linux (caches compilation)
- Parallel builds enabled
- Minimal dependency installation
- Artifacts only uploaded on failure

## Maintenance

### Update Test Matrix
Edit `.github/workflows/unit-tests.yml`:
```yaml
matrix:
  include:
    - name: Your New Config
      runner: ubuntu-latest
      configure_preset: your_preset
      build_preset: your_preset
      configuration: Debug
```

### Add New Preset
Edit `CMakePresets.json`:
```json
{
  "name": "test_your_compiler",
  "inherits": "ninja_your_compiler-debug",
  "cacheVariables": {
    "CMAKE_CXX_FLAGS": "-DLUDUS_RUN_TESTS"
  }
}
```

Then add corresponding build preset.
