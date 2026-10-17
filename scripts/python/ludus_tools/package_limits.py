"""Inclusive native package admission budgets, independent of engine scene limits.

Encoded ZIP metadata, executable command tables and tool diagnostic output have
separate policies even when their current byte ceilings happen to match.
"""

MAX_MANIFEST_BYTES = 4 * 1024 * 1024
MAX_CENTRAL_DIRECTORY_BYTES = 8 * 1024 * 1024
MAX_MACHO_LOAD_COMMAND_BYTES = 8 * 1024 * 1024
MAX_NATIVE_INSPECTION_OUTPUT_BYTES = 8 * 1024 * 1024
