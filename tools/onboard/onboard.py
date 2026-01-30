#!/usr/bin/env python3
import argparse
import json
import os
import platform
import shlex
import shutil
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[2]
TOOLS_DIR = ROOT_DIR / "tools"
ONBOARD_DIR = TOOLS_DIR / "onboard"
DEFAULT_ARGS_FILE = ONBOARD_DIR / "engine_args.json"
VSCODE_EXT_FILE = ONBOARD_DIR / "vscode_extensions.json"
STATE_FILE = ONBOARD_DIR / "onboard_state.json"
VSCODE_SETTINGS = ROOT_DIR / ".vscode" / "settings.json"
PRESET_REQ_FILE = ONBOARD_DIR / "preset_requirements.json"

MIN_VULKAN_SDK_VERSION = (1, 3, 0)


def run_command(cmd, check=False, cwd=None):
    try:
        return subprocess.run(cmd, check=check, cwd=cwd)
    except FileNotFoundError:
        return subprocess.CompletedProcess(cmd, returncode=127)


def format_cmd(cmd):
    if is_windows():
        return subprocess.list2cmdline(cmd)
    return " ".join(shlex.quote(part) for part in cmd)


def format_cwd(cwd):
    return str(cwd) if cwd else ""


def load_state():
    try:
        return json.loads(STATE_FILE.read_text(encoding="utf-8"))
    except Exception:
        return {}


def save_state(state):
    STATE_FILE.write_text(json.dumps(state, indent=2), encoding="utf-8")


def which(cmd):
    return shutil.which(cmd)


def is_windows():
    return platform.system().lower().startswith("win")


def is_macos():
    return platform.system().lower() == "darwin"


def is_linux():
    return platform.system().lower() == "linux"


def detect_vs_installation():
    if not is_windows():
        return False, "not applicable"
    program_files_x86 = os.environ.get("ProgramFiles(x86)", "")
    vswhere = Path(program_files_x86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.exists():
        return False, "vswhere.exe not found"
    result = subprocess.run(
        [str(vswhere), "-latest", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
        capture_output=True,
        text=True,
    )
    if result.returncode == 0 and result.stdout.strip():
        return True, result.stdout.strip()
    return False, "VS C++ tools not detected"


def get_vs_devcmd():
    if not is_windows():
        return None
    program_files_x86 = os.environ.get("ProgramFiles(x86)", "")
    vswhere = Path(program_files_x86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.exists():
        return None
    find_cmd = [
        str(vswhere),
        "-latest",
        "-requires",
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
        "-find",
        "Common7\\Tools\\VsDevCmd.bat",
    ]
    result = subprocess.run(find_cmd, capture_output=True, text=True)
    if result.returncode == 0 and result.stdout.strip():
        raw = result.stdout.strip().strip('"')
        devcmd = Path(raw)
        if devcmd.exists():
            return devcmd

    find_vcvars = [
        str(vswhere),
        "-latest",
        "-requires",
        "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
        "-find",
        "VC\\Auxiliary\\Build\\vcvars64.bat",
    ]
    result = subprocess.run(find_vcvars, capture_output=True, text=True)
    if result.returncode == 0 and result.stdout.strip():
        raw = result.stdout.strip().strip('"')
        vcvars = Path(raw)
        if vcvars.exists():
            return vcvars
    return None


def run_cmake_in_vs_env(cmake_cmd):
    devcmd = get_vs_devcmd()
    if not devcmd:
        return False
    cmake_line = subprocess.list2cmdline(cmake_cmd)
    cmd = ["cmd", "/c", f'call "{str(devcmd)}" && {cmake_line}']
    return run_command(cmd).returncode == 0


def detect_compiler():
    if is_windows():
        if which("cl"):
            return True, "MSVC (cl.exe)"
        if which("clang++"):
            return True, "Clang++"
        return False, "MSVC/Clang not found"
    if which("clang++"):
        return True, "Clang++"
    if which("g++"):
        return True, "G++"
    return False, "Clang++/G++ not found"


def detect_raddbg():
    if not is_windows():
        return False, "RadDbg is Windows-only"
    candidates = [
        ROOT_DIR / "raddbg" / "raddbg.exe",
        ROOT_DIR / "raddbg" / "raddbg" / "raddbg.exe",
    ]
    for path in candidates:
        if path.exists():
            return True, str(path)
    if which("raddbg"):
        return True, "raddbg (PATH)"
    return False, "raddbg.exe not found"


def detect_vscode():
    if which("code"):
        return True, "code"
    if which("code-insiders"):
        return True, "code-insiders"
    return False, "VS Code not found"


def detect_tool(cmd):
    path = which(cmd)
    if path:
        return True, path
    return False, "not found"


def parse_version_tuple(text):
    parts = []
    for token in text.replace("\r", "").replace("\n", "").strip().split("."):
        if token.isdigit():
            parts.append(int(token))
        else:
            break
    return tuple(parts)


def detect_vulkan_sdk():
    sdk_path = os.environ.get("VULKAN_SDK")
    if sdk_path:
        sdk_root = Path(sdk_path)
        header = sdk_root / "Include" / "vulkan" / "vulkan.h"
        version_file = sdk_root / "version.txt"
        if header.exists():
            version = None
            if version_file.exists():
                version = parse_version_tuple(version_file.read_text(encoding="utf-8"))
            if version and version < MIN_VULKAN_SDK_VERSION:
                return False, f"Vulkan SDK {'.'.join(map(str, version))} < required {'.'.join(map(str, MIN_VULKAN_SDK_VERSION))}"
            return True, f"{sdk_root}"
    # Fallback header checks (Linux/macOS)
    if (Path("/usr/include/vulkan/vulkan.h").exists() or Path("/usr/local/include/vulkan/vulkan.h").exists()):
        return True, "system Vulkan headers"
    return False, "Vulkan SDK not found"


def detect_volk():
    # Volk is provided via FetchContent; no system install required.
    return True, "FetchContent (no install required)"


def get_package_manager():
    if is_windows():
        if which("winget"):
            return "winget"
        if which("choco"):
            return "choco"
    if is_macos():
        if which("brew"):
            return "brew"
    if is_linux():
        if which("apt-get"):
            return "apt"
        if which("dnf"):
            return "dnf"
    return None


def install_with_manager(tool_key):
    manager = get_package_manager()
    if manager is None:
        print("No supported package manager found. Install manually.")
        return False

    windows_map = {
        "git": {"winget": ["winget", "install", "--id", "Git.Git", "-e"], "choco": ["choco", "install", "git", "-y"]},
        "cmake": {"winget": ["winget", "install", "--id", "Kitware.CMake", "-e"], "choco": ["choco", "install", "cmake", "-y"]},
        "ninja": {"winget": ["winget", "install", "--id", "Ninja-build.Ninja", "-e"], "choco": ["choco", "install", "ninja", "-y"]},
        "llvm": {"winget": ["winget", "install", "--id", "LLVM.LLVM", "-e"], "choco": ["choco", "install", "llvm", "-y"]},
        "vscode": {"winget": ["winget", "install", "--id", "Microsoft.VisualStudioCode", "-e"], "choco": ["choco", "install", "vscode", "-y"]},
        "python": {"winget": ["winget", "install", "--id", "Python.Python.3.12", "-e"], "choco": ["choco", "install", "python", "-y"]},
        "vs2022": {"winget": ["winget", "install", "--id", "Microsoft.VisualStudio.2022.Community", "-e"], "choco": ["choco", "install", "visualstudio2022community", "-y"]},
        "vulkan_sdk": {"winget": ["winget", "install", "--id", "KhronosGroup.VulkanSDK", "-e"], "choco": ["choco", "install", "vulkan-sdk", "-y"]},
    }

    mac_map = {
        "git": ["brew", "install", "git"],
        "cmake": ["brew", "install", "cmake"],
        "ninja": ["brew", "install", "ninja"],
        "llvm": ["brew", "install", "llvm"],
        "vscode": ["brew", "install", "--cask", "visual-studio-code"],
        "python": ["brew", "install", "python"],
        "vulkan_sdk": ["brew", "install", "vulkan-sdk"],
    }

    linux_map = {
        "git": ["sudo", "apt-get", "install", "-y", "git"],
        "cmake": ["sudo", "apt-get", "install", "-y", "cmake"],
        "ninja": ["sudo", "apt-get", "install", "-y", "ninja-build"],
        "llvm": ["sudo", "apt-get", "install", "-y", "clang", "clang-tools", "clang-tidy", "clang-format"],
        "vscode": ["sudo", "apt-get", "install", "-y", "code"],
        "python": ["sudo", "apt-get", "install", "-y", "python3"],
        "vulkan_sdk": ["sudo", "apt-get", "install", "-y", "vulkan-sdk"],
    }

    cmd = None
    if is_windows():
        tool_entry = windows_map.get(tool_key, {})
        cmd = tool_entry.get(manager)
    elif is_macos():
        cmd = mac_map.get(tool_key)
    elif is_linux():
        cmd = linux_map.get(tool_key)

    if not cmd:
        print(f"No installer configured for {tool_key} with {manager}.")
        return False

    print("Running:", " ".join(cmd))
    result = run_command(cmd)
    return result.returncode == 0


def install_raddbg(force=False):
    if not is_windows():
        print("RadDbg is Windows-only.")
        return False
    dest_dir = ROOT_DIR / "raddbg"
    exe_path = dest_dir / "raddbg.exe"
    if exe_path.exists() and not force:
        print("RadDbg already installed. Use --force to reinstall.")
        return True

    api_url = "https://api.github.com/repos/EpicGamesExt/raddebugger/releases/latest"
    print("Fetching RadDbg release metadata...")
    with urllib.request.urlopen(api_url) as response:
        release = json.loads(response.read().decode("utf-8"))

    asset_url = None
    for asset in release.get("assets", []):
        name = asset.get("name", "")
        if name.lower().endswith("raddbg.zip"):
            asset_url = asset.get("browser_download_url")
            break
    if not asset_url:
        raise RuntimeError("Could not find raddbg.zip asset in latest release.")

    zip_path = dest_dir / "raddbg.zip"
    dest_dir.mkdir(parents=True, exist_ok=True)

    print("Downloading RadDbg...")
    urllib.request.urlretrieve(asset_url, zip_path)

    print("Extracting RadDbg...")
    with zipfile.ZipFile(zip_path, "r") as zip_ref:
        zip_ref.extractall(dest_dir)

    zip_path.unlink(missing_ok=True)
    return True


def install_llvm_windows_fallback():
    installer = TOOLS_DIR / "install-llvm.ps1"
    if not installer.exists():
        return False
    print("Running PowerShell LLVM installer...")
    result = run_command(["powershell", "-ExecutionPolicy", "Bypass", "-File", str(installer)])
    return result.returncode == 0


def load_preset_requirements():
    if PRESET_REQ_FILE.exists():
        try:
            return json.loads(PRESET_REQ_FILE.read_text(encoding="utf-8")).get("presets", [])
        except Exception:
            return []
    return []


def resolve_preset_cache(preset_name):
    presets_path = ROOT_DIR / "CMakePresets.json"
    if not presets_path.exists():
        return {}
    data = json.loads(presets_path.read_text(encoding="utf-8"))
    presets = {p.get("name"): p for p in data.get("configurePresets", [])}

    def merge_cache(name, visited):
        if name in visited or name not in presets:
            return {}
        visited.add(name)
        preset = presets[name]
        cache = {}
        for parent in preset.get("inherits", []) if isinstance(preset.get("inherits"), list) else ([preset.get("inherits")] if preset.get("inherits") else []):
            cache.update(merge_cache(parent, visited))
        cache.update(preset.get("cacheVariables", {}) or {})
        return cache

    return merge_cache(preset_name, set())


def get_preset_requirements(preset_name):
    cache = resolve_preset_cache(preset_name)
    requirements = []
    rules = load_preset_requirements()
    for rule in rules:
        when = rule.get("when", {})
        match = True
        for key, expected in when.items():
            if str(cache.get(key, "")) != str(expected):
                match = False
                break
        if match:
            requirements.extend(rule.get("requirements", []))
    # Fallback: if preset name contains "vulkan"
    if preset_name and "vulkan" in preset_name.lower():
        if "vulkan_sdk" not in requirements:
            requirements.append("vulkan_sdk")
        if "volk" not in requirements:
            requirements.append("volk")
    return requirements


def check_tools(role, preset_name=""):
    tools = []
    tools.append(("git", "Git", *detect_tool("git"), True))
    tools.append(("cmake", "CMake", *detect_tool("cmake"), True))
    tools.append(("ninja", "Ninja", *detect_tool("ninja"), True))

    compiler_ok, compiler_details = detect_compiler()
    tools.append(("compiler", "C/C++ Compiler", compiler_ok, compiler_details, True))

    if is_windows():
        vs_ok, vs_details = detect_vs_installation()
        tools.append(("vs2022", "Visual Studio 2022 C++", vs_ok, vs_details, True))

    if role in ("contributor", "debugger"):
        tools.append(("llvm", "LLVM (clang-format/clang-tidy)", *detect_tool("clang-format"), True))
        tools.append(("clang-tidy", "clang-tidy", *detect_tool("clang-tidy"), False))

    vscode_ok, vscode_details = detect_vscode()
    tools.append(("vscode", "VS Code", vscode_ok, vscode_details, False))

    if role == "debugger":
        raddbg_ok, raddbg_details = detect_raddbg()
        tools.append(("raddbg", "RadDbg", raddbg_ok, raddbg_details, True))

    preset_reqs = get_preset_requirements(preset_name)
    if "vulkan_sdk" in preset_reqs:
        tools.append(("vulkan_sdk", "Vulkan SDK", *detect_vulkan_sdk(), True))
    if "volk" in preset_reqs:
        tools.append(("volk", "volk", *detect_volk(), True))

    return tools


def print_tools(tools):
    for key, label, ok, details, required in tools:
        status = "OK" if ok else "MISSING"
        req = "required" if required else "optional"
        print(f"{label:28} {status:8} ({req}) - {details}")


def install_missing(role, preset_name=""):
    tools = check_tools(role, preset_name)
    missing = [t for t in tools if not t[2] and t[4]]
    if not missing:
        print("All required tools already installed.")
        return True

    for key, label, _, _, _ in missing:
        if key == "compiler" and is_windows():
            print("Install Visual Studio 2022 with Desktop C++ workload.")
            install_with_manager("vs2022")
            continue
        if key == "compiler" and not is_windows():
            install_with_manager("llvm")
            continue
        if key == "llvm" and is_windows():
            if install_with_manager("llvm"):
                continue
            install_llvm_windows_fallback()
            continue
        if key == "vulkan_sdk":
            install_with_manager("vulkan_sdk")
            continue
        if key == "raddbg":
            install_raddbg()
            continue
        install_with_manager(key)

    return True


def load_engine_args(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except FileNotFoundError:
        return {"args": []}


def build_args_from_spec(spec, values, extra_args):
    args = []
    for entry in spec.get("args", []):
        flag = entry.get("flag")
        arg_id = entry.get("id")
        arg_type = entry.get("type", "string")
        value = values.get(arg_id)
        if arg_type == "bool":
            if value:
                args.append(flag)
        else:
            if value is not None and value != "":
                args.extend([flag, str(value)])
    if extra_args:
        args.extend(extra_args.split())
    return args


def find_editor_executable(preferred=None):
    if preferred:
        p = Path(preferred)
        if p.exists():
            return p
    exe_name = "LudusEditor.exe" if is_windows() else "LudusEditor"
    candidates = [
        ROOT_DIR / "build" / "bin" / exe_name,
    ]
    out_build = ROOT_DIR / "out" / "build"
    if out_build.exists():
        for child in out_build.iterdir():
            candidates.append(child / "bin" / exe_name)
            candidates.append(child / exe_name)
    for path in candidates:
        if path.exists():
            return path
    return None


def find_target_executable(target_name):
    if not target_name:
        return None
    exe_name = f"{target_name}.exe" if is_windows() else target_name
    candidates = [
        ROOT_DIR / "build" / "bin" / exe_name,
    ]
    out_build = ROOT_DIR / "out" / "build"
    if out_build.exists():
        for child in out_build.iterdir():
            candidates.append(child / "bin" / exe_name)
            candidates.append(child / exe_name)
    for path in candidates:
        if path.exists():
            return path
    return None


def run_editor(extra_args, args_file, binary):
    spec = load_engine_args(args_file)
    values = {}
    args = build_args_from_spec(spec, values, extra_args)
    exe = find_editor_executable(binary)
    if not exe:
        print("LudusEditor executable not found. Build the project first.")
        return False
    cmd = [str(exe)] + args
    print("Running:", format_cmd(cmd))
    print("Working dir:", format_cwd(exe.parent))
    return run_command(cmd, cwd=exe.parent).returncode == 0


def debug_editor(extra_args, args_file, binary):
    ok, details = detect_raddbg()
    if not ok:
        print("RadDbg not installed. Install it with onboarding first.")
        return False
    exe = find_editor_executable(binary)
    if not exe:
        print("LudusEditor executable not found. Build the project first.")
        return False
    raddbg_path = details if details.endswith(".exe") else which("raddbg")
    if not raddbg_path:
        print("RadDbg executable not found.")
        return False
    args = []
    spec = load_engine_args(args_file)
    values = {}
    args.extend(build_args_from_spec(spec, values, extra_args))
    cmd = [raddbg_path, str(exe)]
    if args:
        cmd += ["--"] + args
    print("Debug command:", format_cmd(cmd))
    print("Working dir:", format_cwd(exe.parent))
    return run_command(cmd, cwd=exe.parent).returncode == 0


def list_presets():
    presets_path = ROOT_DIR / "CMakePresets.json"
    if not presets_path.exists():
        print("CMakePresets.json not found.")
        return
    data = json.loads(presets_path.read_text(encoding="utf-8"))
    presets = data.get("configurePresets", [])
    for preset in presets:
        if preset.get("hidden"):
            continue
        print(f"{preset.get('name')} - {preset.get('displayName')}")


def configure_preset(name):
    if not name:
        return False
    return run_command(["cmake", "--preset", name]).returncode == 0


def build_preset(name, target):
    if not name:
        return False
    cmd = ["cmake", "--build", "--preset", name]
    if target:
        cmd += ["-t", target]
    return run_command(cmd).returncode == 0


def one_click_setup(role, preset, target):
    install_missing(role, preset)
    if not detect_vscode()[0]:
        install_with_manager("vscode")
    install_vscode_extensions()
    set_vscode_preset(preset)
    open_vscode()


def install_vscode_extensions():
    ok, _ = detect_vscode()
    if not ok:
        print("VS Code not installed.")
        return False
    if not VSCODE_EXT_FILE.exists():
        print("VS Code extension list not found.")
        return False
    extensions = json.loads(VSCODE_EXT_FILE.read_text(encoding="utf-8")).get("extensions", [])
    code_cmd = "code" if which("code") else "code-insiders"
    for ext in extensions:
        run_command([code_cmd, "--install-extension", ext])
    return True


def open_vscode():
    ok, details = detect_vscode()
    if not ok:
        print("VS Code not installed.")
        return False
    code_cmd = "code" if which("code") else "code-insiders"
    return run_command([code_cmd, str(ROOT_DIR)]).returncode == 0


def set_vscode_preset(preset):
    if not preset:
        return False
    VSCODE_SETTINGS.parent.mkdir(parents=True, exist_ok=True)
    settings = {}
    if VSCODE_SETTINGS.exists():
        try:
            settings = json.loads(VSCODE_SETTINGS.read_text(encoding="utf-8"))
        except Exception:
            settings = {}
    settings["cmake.configurePreset"] = preset
    settings.setdefault("cmake.buildPreset", preset)
    VSCODE_SETTINGS.write_text(json.dumps(settings, indent=2), encoding="utf-8")
    return True


def launch_gui():
    try:
        import tkinter as tk
        from tkinter import ttk
    except Exception:
        print("Tkinter not available. Run with CLI options or install python3-tk.")
        return

    root = tk.Tk()
    root.title("Ludus Onboarding")
    root.geometry("860x620")

    state = load_state()
    role_var = tk.StringVar(value=state.get("role", "user"))

    def refresh_prereqs(*_args):
        for row in tree.get_children():
            tree.delete(row)
        tools = check_tools(role_var.get(), preset_var.get() if "preset_var" in locals() else "")
        for _, label, ok, details, required in tools:
            tree.insert("", tk.END, values=(label, "OK" if ok else "MISSING", "required" if required else "optional", details))
        state["role"] = role_var.get()
        save_state(state)

    def install_missing_clicked():
        install_missing(role_var.get(), preset_var.get() if "preset_var" in locals() else "")
        refresh_prereqs()

    def install_vscode_clicked():
        install_with_manager("vscode")
        refresh_prereqs()

    def install_extensions_clicked():
        install_vscode_extensions()

    def open_vscode_clicked():
        open_vscode()

    def one_click_clicked():
        preset = preset_var.get()
        if preset:
            one_click_setup(role_var.get(), preset, "")
            refresh_prereqs()

    args_spec = load_engine_args(DEFAULT_ARGS_FILE)
    arg_vars = {}

    def rebuild_args_fields():
        for child in args_spec_frame.winfo_children():
            child.destroy()
        arg_vars.clear()
        row = 0
        for entry in args_spec.get("args", []):
            arg_id = entry.get("id")
            label = entry.get("flag", arg_id)
            arg_type = entry.get("type", "string")
            default = entry.get("default", "")

            ttk.Label(args_spec_frame, text=label).grid(row=row, column=0, sticky="w", padx=6, pady=2)
            if arg_type == "bool":
                var = tk.BooleanVar(value=bool(default))
                ttk.Checkbutton(args_spec_frame, variable=var).grid(row=row, column=1, sticky="w", padx=6, pady=2)
            else:
                var = tk.StringVar(value=str(default))
                ttk.Entry(args_spec_frame, textvariable=var, width=18).grid(row=row, column=1, sticky="w", padx=6, pady=2)
            arg_vars[arg_id] = var
            row += 1

    def collect_arg_values():
        values = {}
        for entry in args_spec.get("args", []):
            arg_id = entry.get("id")
            var = arg_vars.get(arg_id)
            if var is None:
                continue
            values[arg_id] = var.get()
        return values

    def reload_args_clicked():
        nonlocal args_spec
        path = Path(args_file_var.get())
        if path.exists():
            args_spec = load_engine_args(path)
            rebuild_args_fields()

    def run_clicked():
        values = collect_arg_values()
        args = build_args_from_spec(args_spec, values, extra_args_var.get())
        binary = binary_var.get()
        exe = find_editor_executable(binary)
        if not exe:
            print("Target executable not found. Build the project first.")
            return
        cmd = [str(exe)] + args
        print("Running:", format_cmd(cmd))
        print("Working dir:", format_cwd(exe.parent))
        run_command(cmd, cwd=exe.parent)

    def debug_clicked():
        values = collect_arg_values()
        args = build_args_from_spec(args_spec, values, extra_args_var.get())
        ok, details = detect_raddbg()
        if not ok:
            print("RadDbg not installed. Install it with onboarding first.")
            return
        raddbg_path = details if details.endswith(".exe") else which("raddbg")
        if not raddbg_path:
            print("RadDbg executable not found.")
            return
        binary = binary_var.get()
        exe = find_editor_executable(binary)
        if not exe:
            print("Target executable not found. Build the project first.")
            return
        cmd = [raddbg_path, str(exe)]
        if args:
            cmd += ["--"] + args
        print("Debug command:", format_cmd(cmd))
        print("Working dir:", format_cwd(exe.parent))
        run_command(cmd, cwd=exe.parent)

    header = ttk.Label(root, text="Ludus Onboarding", font=("Segoe UI", 16, "bold"))
    header.pack(pady=10)

    role_frame = ttk.Frame(root)
    role_frame.pack(fill="x", padx=12)
    ttk.Label(role_frame, text="Profile:").pack(side="left")
    ttk.Combobox(role_frame, textvariable=role_var, values=["user", "contributor", "debugger"], width=16, state="readonly").pack(side="left", padx=8)
    ttk.Button(role_frame, text="Refresh", command=refresh_prereqs).pack(side="left", padx=6)
    ttk.Button(role_frame, text="Install Missing", command=install_missing_clicked).pack(side="left", padx=6)
    role_var.trace_add("write", refresh_prereqs)

    prereq_frame = ttk.LabelFrame(root, text="Prerequisites")
    prereq_frame.pack(fill="both", expand=False, padx=12, pady=8)

    columns = ("tool", "status", "required", "details")
    tree = ttk.Treeview(prereq_frame, columns=columns, show="headings", height=6)
    tree.heading("tool", text="Tool")
    tree.heading("status", text="Status")
    tree.heading("required", text="Required")
    tree.heading("details", text="Details")
    tree.column("tool", width=180)
    tree.column("status", width=90)
    tree.column("required", width=90)
    tree.column("details", width=420)
    tree.pack(fill="x", padx=6, pady=6)

    vscode_frame = ttk.LabelFrame(root, text="VS Code")
    vscode_frame.pack(fill="x", padx=12, pady=6)
    ttk.Button(vscode_frame, text="Install VS Code", command=install_vscode_clicked).pack(side="left", padx=6, pady=6)
    ttk.Button(vscode_frame, text="Install Extensions", command=install_extensions_clicked).pack(side="left", padx=6)
    ttk.Button(vscode_frame, text="Open VS Code", command=open_vscode_clicked).pack(side="left", padx=6)

    build_frame = ttk.LabelFrame(root, text="VS Code Preset")
    build_frame.pack(fill="x", padx=12, pady=6)
    presets = []
    presets_path = ROOT_DIR / "CMakePresets.json"
    if presets_path.exists():
        data = json.loads(presets_path.read_text(encoding="utf-8"))
        presets = [p["name"] for p in data.get("configurePresets", []) if not p.get("hidden")]
    default_preset = state.get("preset", presets[0] if presets else "")
    preset_var = tk.StringVar(value=default_preset if default_preset in presets else (presets[0] if presets else ""))
    ttk.Label(build_frame, text="Preset:").pack(side="left", padx=6)
    ttk.Combobox(build_frame, textvariable=preset_var, values=presets, width=32).pack(side="left")
    ttk.Button(build_frame, text="Set Preset + Open VS Code", command=one_click_clicked).pack(side="left", padx=6)
    preset_var.trace_add("write", lambda *_: (state.__setitem__("preset", preset_var.get()), save_state(state)))
    preset_var.trace_add("write", refresh_prereqs)

    run_frame = ttk.LabelFrame(root, text="Run / Debug")
    run_frame.pack(fill="x", padx=12, pady=6)
    args_file_var = tk.StringVar(value=str(DEFAULT_ARGS_FILE))
    extra_args_var = tk.StringVar(value="")
    binary_var = tk.StringVar(value="")
    target_options = ["LudusEditor", "LudusTests", "Custom..."]
    target_var = tk.StringVar(value=target_options[0])
    def on_target_change(*_args):
        name = target_var.get()
        if name == "Custom...":
            return
        exe = find_target_executable(name)
        if exe:
            binary_var.set(str(exe))
        else:
            binary_var.set("")
    ttk.Label(run_frame, text="Args file:").grid(row=0, column=0, sticky="w", padx=6, pady=4)
    ttk.Entry(run_frame, textvariable=args_file_var, width=60).grid(row=0, column=1, padx=6, pady=4, sticky="w")
    ttk.Button(run_frame, text="Reload Args", command=reload_args_clicked).grid(row=0, column=2, padx=6)

    args_spec_frame = ttk.Frame(run_frame)
    args_spec_frame.grid(row=1, column=0, columnspan=3, sticky="w", padx=6, pady=2)
    rebuild_args_fields()

    ttk.Label(run_frame, text="Target:").grid(row=2, column=0, sticky="w", padx=6, pady=4)
    ttk.Combobox(run_frame, textvariable=target_var, values=target_options, width=18, state="readonly").grid(row=2, column=1, padx=6, pady=4, sticky="w")
    ttk.Label(run_frame, text="Binary path:").grid(row=3, column=0, sticky="w", padx=6, pady=4)
    ttk.Entry(run_frame, textvariable=binary_var, width=60).grid(row=3, column=1, padx=6, pady=4, sticky="w")
    ttk.Label(run_frame, text="Extra args:").grid(row=4, column=0, sticky="w", padx=6, pady=4)
    ttk.Entry(run_frame, textvariable=extra_args_var, width=60).grid(row=4, column=1, padx=6, pady=4, sticky="w")
    ttk.Button(run_frame, text="Run Target", command=run_clicked).grid(row=3, column=2, padx=6)
    ttk.Button(run_frame, text="Debug Target", command=debug_clicked).grid(row=4, column=2, padx=6)
    target_var.trace_add("write", on_target_change)
    on_target_change()

    refresh_prereqs()
    root.mainloop()


def main():
    parser = argparse.ArgumentParser(
        description="Ludus onboarding: prerequisites, VS Code, build, run, debug.",
        formatter_class=argparse.RawTextHelpFormatter,
    )
    parser.add_argument("--role", choices=["user", "contributor", "debugger"], default="user")
    parser.add_argument("--gui", action="store_true", help="Launch GUI")
    parser.add_argument("--check", action="store_true", help="Print prerequisite status")
    parser.add_argument("--install", action="store_true", help="Install missing prerequisites")
    parser.add_argument("--preset", default="", help="Preset name used for preset-specific prerequisites")
    parser.add_argument("--install-vscode", action="store_true", help="Install VS Code")
    parser.add_argument("--install-vscode-extensions", action="store_true", help="Install VS Code extensions")
    parser.add_argument("--open-vscode", action="store_true", help="Open VS Code in repo")
    parser.add_argument("--list-presets", action="store_true", help="List CMake configure presets")
    parser.add_argument("--configure-preset", default="", help="Run: cmake --preset <name>")
    parser.add_argument("--build-preset", default="", help="Run: cmake --build --preset <name>")
    parser.add_argument("--build-target", default="LudusEditor", help="Build target (default: LudusEditor)")
    parser.add_argument("--one-click", action="store_true", help="Install prerequisites, configure, and build")
    parser.add_argument("--run-editor", action="store_true", help="Run LudusEditor")
    parser.add_argument("--debug-editor", action="store_true", help="Launch RadDbg (Windows)")
    parser.add_argument("--args-file", default=str(DEFAULT_ARGS_FILE))
    parser.add_argument("--args", default="", help="Extra args to pass to LudusEditor")
    parser.add_argument("--binary", default="", help="Explicit path to LudusEditor binary")
    parser.add_argument("--install-raddbg", action="store_true", help="Install RadDbg (Windows only)")
    parser.add_argument("--force", action="store_true", help="Force reinstall (for RadDbg)")

    if len(sys.argv) == 1:
        launch_gui()
        return

    args = parser.parse_args()
    if args.gui:
        launch_gui()
        return

    if args.check:
        print_tools(check_tools(args.role, args.preset))
    if args.install:
        install_missing(args.role, args.preset)
    if args.install_vscode:
        install_with_manager("vscode")
    if args.install_vscode_extensions:
        install_vscode_extensions()
    if args.open_vscode:
        open_vscode()
    if args.list_presets:
        list_presets()
    if args.configure_preset:
        configure_preset(args.configure_preset)
    if args.build_preset:
        build_preset(args.build_preset, args.build_target)
    if args.one_click:
        preset = args.build_preset or args.configure_preset or "ninja_msvc-debug"
        one_click_setup(args.role, preset, "")
    if args.install_raddbg:
        install_raddbg(force=args.force)
    if args.run_editor:
        run_editor(args.args, args.args_file, args.binary)
    if args.debug_editor:
        debug_editor(args.args, args.args_file, args.binary)


if __name__ == "__main__":
    main()
