"""Compatibility import for the S3 workbench; the CLI/SDK own the shared model."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]/"scripts/python"))
from ludus_tools.behavior_graph import *
