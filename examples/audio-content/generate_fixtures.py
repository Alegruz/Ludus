#!/usr/bin/env python3
"""Generate original PCM tones and v1 documents; no external licensed media."""
import argparse
import json
import math
from pathlib import Path
import struct
import subprocess
import wave


def generate(root, long_seconds=2, flac=False):
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True)
    def tone(name, rate, frames, frequency, channels=1):
        with wave.open(str(root / name), "wb") as wav:
            wav.setparams((channels, 2, rate, frames, "NONE", "not compressed"))
            for offset in range(0, frames, 4096):
                wav.writeframes(b"".join(struct.pack("<h", int(10000 * math.sin(2 * math.pi * frequency * f / rate)))
                                         for f in range(offset, min(offset + 4096, frames)) for channel in range(channels)))
    tone("impact.wav", 44100, 11025, 880)
    tone("theme.wav", 48000, 48000 * long_seconds, 220, 2)
    loop = {"enabled": False, "begin_frame": 0, "end_frame": 0}
    sound = {"version": 1, "id": "sound/impact", "bus": "sfx", "group": "impacts", "priority": 4,
             "gain": 0.7, "rate": 1, "spatial": {"mode": "none", "min_distance": 1, "max_distance": 100},
             "loop": loop, "policy": {"cooldown_ms": 100, "suppress_while_active": False}, "variations": ["source/impact"]}
    music = {"version": 1, "id": "music/theme", "source": "source/theme", "bus": "music", "priority": 6,
             "gain": 0.5, "loop": {"enabled": True, "begin_frame": 0, "end_frame": 48000 * long_seconds}}
    catalog = {"version": 1, "resources": [
        {"id": "source/impact", "kind": "audio-source", "path": "impact.wav"},
        {"id": "source/theme", "kind": "audio-source", "path": "theme.wav"},
        {"id": "sound/impact", "kind": "sound", "path": "impact.json"},
        {"id": "music/theme", "kind": "music", "path": "theme.json"}]}
    if flac:
        for name in ("impact", "theme"):
            subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin", "-y", "-i",
                            str(root / f"{name}.wav"), str(root / f"{name}.flac")], check=True)
            (root / f"{name}.wav").unlink()
        for entry in catalog["resources"]:
            if entry["kind"] == "audio-source":
                entry["path"] = entry["path"].replace(".wav", ".flac")
    for name, document in (("impact.json", sound), ("theme.json", music), ("catalog.json", catalog)):
        (root / name).write_text(json.dumps(document, sort_keys=True, indent=2) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("output")
    parser.add_argument("--long-seconds", type=int, default=2)
    parser.add_argument("--flac", action="store_true", help="Use development-host ffmpeg to encode original tones")
    args = parser.parse_args()
    if not 2 <= args.long_seconds <= 600:
        parser.error("--long-seconds must be between 2 and 600")
    generate(args.output, args.long_seconds, args.flac)
