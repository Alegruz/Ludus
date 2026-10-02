"""Enable ``python -m ludus_tools`` as the CLI entry point."""

from __future__ import annotations

from .cli import main

if __name__ == "__main__":
    raise SystemExit(main())
