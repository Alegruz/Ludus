"""Optional native setup selector; all installation stays in the engine runner.

Tk is loaded only for graphical setup. Closing the window cancels before any
setup mutation; accepting it returns to the terminal so sudo retains its TTY.
"""
from __future__ import annotations

import argparse
import copy
import os
import sys
from dataclasses import dataclass


@dataclass(frozen=True)
class Persona:
    key: str
    label: str
    description: str
    preset: str
    preset_only: bool = False
    validate: bool = False


PERSONAS = (
    Persona("contributor", "Engine contributor",
            "Prepare all native presets for engine work. Build and test later.",
            "linux-clang-development"),
    Persona("application", "Application developer",
            "Prepare one native preset for building applications with Ludus.",
            "linux-clang-development", preset_only=True),
    Persona("browser", "Browser developer",
            "Install the pinned Emscripten tools and configure one browser preset.",
            "web-emscripten-development", preset_only=True),
    Persona("validation", "Full validation",
            "Prepare native tools, then build, test, check, run sanitizers and test the SDK.",
            "linux-clang-development", validate=True),
)


class GuiUnavailable(RuntimeError):
    """The optional Tk window cannot be opened on this host."""


def apply_defaults(args: argparse.Namespace) -> argparse.Namespace:
    result = copy.copy(args)
    persona = next(persona for persona in PERSONAS if persona.key == args.persona)
    result.preset = args.preset or persona.preset
    if not args.all_presets and not args.preset_only:
        result.preset_only = persona.preset_only or result.preset.startswith("web-emscripten-")
    result.validate = args.validate or persona.validate
    return result


def validate_options(args: argparse.Namespace, engine) -> None:
    if args.all_presets and args.preset_only:
        raise engine.EngineError("--all-presets and --preset-only describe conflicting initialization scopes")
    if args.preset.startswith("web-emscripten-"):
        if args.with_rad_debugger:
            raise engine.EngineError("RAD is native tooling; run ./scripts/setup-rad-debugger separately from browser init")
        from web_build import PRESETS
        if args.preset not in PRESETS:
            raise engine.EngineError("Unknown browser preset: " + args.preset)
        if args.all_presets or args.validate or args.ci:
            raise engine.EngineError("Use web init --preset-only, then web build/test/check separately")
    else:
        engine.ensure_known_preset(args.preset)
    from init_editor import validate_editor_options
    validate_editor_options(args, engine)


def wants_gui(args: argparse.Namespace) -> bool:
    if args.cli:
        return False
    if args.gui:
        return True
    if args.ci or os.environ.get("CI") or not sys.stdin.isatty() or not sys.stdout.isatty():
        return False
    return sys.platform != "linux" or bool(os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY"))


def prepare_init(args: argparse.Namespace, engine) -> argparse.Namespace | None:
    args = apply_defaults(args)
    validate_options(args, engine)
    if wants_gui(args):
        try:
            args = select_options(args, engine)
        except GuiUnavailable as error:
            if args.gui:
                raise engine.EngineError(str(error)) from error
            print(f"Graphical setup unavailable: {error}\nContinuing with CLI setup.", file=sys.stderr)
    if args is not None:
        validate_options(args, engine)
    return args


def select_options(args: argparse.Namespace, engine) -> argparse.Namespace | None:
    try:
        import tkinter as tk
        from tkinter import messagebox, ttk
    except ImportError as error:
        raise GuiUnavailable(
            "Tkinter is missing. On Ubuntu install python3-tk, or use --cli."
        ) from error
    try:
        window = tk.Tk()
    except tk.TclError as error:
        raise GuiUnavailable("Cannot open a desktop display. Use --cli, or check your display connection.") from error

    selection = None
    try:
        window.title("Ludus setup")
        window.minsize(660, 560)
        panel = ttk.Frame(window, padding=24)
        panel.grid(sticky="nsew")
        window.columnconfigure(0, weight=1)
        window.rowconfigure(0, weight=1)
        panel.columnconfigure(1, weight=1)
        ttk.Label(panel, text="Set up Ludus", font=("", 20, "bold")).grid(
            row=0, column=0, columnspan=2, sticky="w")
        ttk.Label(panel, text="Choose the tools and workflow you need.").grid(
            row=1, column=0, columnspan=2, sticky="w", pady=(4, 16))

        persona = tk.StringVar(value=next(item.label for item in PERSONAS if item.key == args.persona))
        preset = tk.StringVar(value=args.preset)
        description = tk.StringVar()
        summary = tk.StringVar()
        flags = {name: tk.BooleanVar(value=getattr(args, name)) for name in (
            "preset_only", "no_system_install", "with_rad_debugger", "with_editor", "validate",
            "skip_checks", "skip_sanitizers", "skip_sdk", "ci",
        )}

        ttk.Label(panel, text="Workflow").grid(row=2, column=0, sticky="w", padx=(0, 16))
        workflow = ttk.Combobox(panel, textvariable=persona, state="readonly",
                               values=[item.label for item in PERSONAS])
        workflow.grid(row=2, column=1, sticky="ew")
        ttk.Label(panel, textvariable=description, wraplength=600).grid(
            row=3, column=0, columnspan=2, sticky="w", pady=(8, 16))
        from web_build import PRESETS
        ttk.Label(panel, text="Build preset").grid(row=4, column=0, sticky="w")
        presets = ttk.Combobox(panel, textvariable=preset, state="readonly",
                              values=[*engine.PRESET_BUILD_TYPES, *PRESETS])
        presets.grid(row=4, column=1, sticky="ew")
        controls = {}
        labels = (
            ("preset_only", "Prepare only the selected preset"),
            ("no_system_install", "Use existing system packages (skip automatic apt installation)"),
            ("with_rad_debugger", "Build optional RAD Debugger (Linux x64)"),
            ("with_editor", "Build Ludus editor (Linux x64; installs optional Qt 6 packages)"),
            ("validate", "Build and run full validation after setup"),
            ("skip_checks", "Skip formatting and static analysis during validation"),
            ("skip_sanitizers", "Skip sanitizer build and tests during validation"),
            ("skip_sdk", "Skip SDK consumer test during validation"),
            ("ci", "Use CI validation policy (warnings as errors; no local hooks)"),
        )

        def current_args():
            result = copy.copy(args)
            result.persona = next(item.key for item in PERSONAS if item.label == persona.get())
            result.preset = preset.get()
            for name, value in flags.items():
                setattr(result, name, value.get())
            result.all_presets = not result.preset_only
            return result

        def refresh():
            browser = preset.get().startswith("web-")
            if browser:
                flags["preset_only"].set(True)
                for name in ("with_rad_debugger", "with_editor", "validate", "ci"):
                    flags[name].set(False)
            from init_editor import SUPPORTED_PRESETS
            editor_supported = preset.get() in SUPPORTED_PRESETS
            if not editor_supported:
                flags["with_editor"].set(False)
            controls["with_editor"].configure(state="normal" if editor_supported else "disabled")
            validating = flags["validate"].get() or flags["ci"].get()
            for name in ("preset_only", "with_rad_debugger", "validate", "ci"):
                controls[name].configure(state="disabled" if browser else "normal")
            for name in ("skip_checks", "skip_sanitizers", "skip_sdk"):
                controls[name].configure(state="normal" if validating else "disabled")
            # Native validation prepares every preset regardless of --preset-only.
            scope = "one browser preset" if browser else (
                "one native preset" if flags["preset_only"].get() and not validating else "all native presets")
            action = "Setup, engine builds and validation" if validating else "Tool and dependency setup"
            if flags["with_editor"].get() and not validating:
                action = "Tool and dependency setup plus the optional editor build"
            summary.set(f"{action} for {scope}.\n"
                        "Progress and any sudo password prompt appear in the launching terminal.")

        def change_workflow(_event=None):
            chosen = next(item for item in PERSONAS if item.label == persona.get())
            description.set(chosen.description)
            preset.set(chosen.preset)
            flags["preset_only"].set(chosen.preset_only)
            flags["validate"].set(chosen.validate)
            flags["ci"].set(False)
            refresh()

        for row, (name, label) in enumerate(labels, start=5):
            controls[name] = ttk.Checkbutton(panel, text=label, variable=flags[name], command=refresh)
            controls[name].grid(row=row, column=0, columnspan=2, sticky="w", pady=3)
        workflow.bind("<<ComboboxSelected>>", change_workflow)
        presets.bind("<<ComboboxSelected>>", lambda _event: refresh())
        description.set(next(item.description for item in PERSONAS if item.key == args.persona))
        separator_row = 5 + len(labels)
        ttk.Separator(panel).grid(row=separator_row, column=0, columnspan=2, sticky="ew", pady=14)
        ttk.Label(panel, textvariable=summary, wraplength=600).grid(
            row=separator_row + 1, column=0, columnspan=2, sticky="w")
        ttk.Label(panel, text=f"Repository: {engine.repo_root()}", wraplength=600).grid(
            row=separator_row + 2, column=0, columnspan=2, sticky="w", pady=(8, 16))

        def accept():
            nonlocal selection
            candidate = current_args()
            try:
                validate_options(candidate, engine)
            except engine.EngineError as error:
                messagebox.showerror("Check setup options", str(error), parent=window)
                return
            selection = candidate
            window.destroy()

        buttons = ttk.Frame(panel)
        buttons.grid(row=separator_row + 3, column=0, columnspan=2, sticky="e")
        ttk.Button(buttons, text="Cancel", command=window.destroy).pack(side="left", padx=(0, 8))
        ttk.Button(buttons, text="Initialize", command=accept).pack(side="left")
        window.protocol("WM_DELETE_WINDOW", window.destroy)
        window.bind("<Escape>", lambda _event: window.destroy())
        refresh()
        window.mainloop()
    finally:
        # Also release Tk when a callback/building the form fails.
        try:
            window.destroy()
        except tk.TclError:
            pass
    return selection
