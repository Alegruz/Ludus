#!/usr/bin/env python3
"""Reproducibly build the static Hangul fixture face for the Ludus text tests.

The upstream Noto Sans KR is distributed as a large variable font (a `wght`
axis, ~10 MiB). The text/font rendering design (section 2) requires a *static*
face and permits subsetting "with a recorded reproducible tool/script that
retains the needed OpenType shaping tables". This script:

  1. instances the variable font to a single static Regular weight (wght=400),
  2. subsets it to the Hangul syllables used by the fixtures (plus a space),
     retaining the GSUB/GPOS/GDEF layout tables so shaping behaviour is intact.

Inputs/outputs and the exact fonttools version are recorded in
docs/architecture/text-font-rendering-evidence/dependency-manifest.md. Rerun with
the pinned fonttools to regenerate byte-identical output.

Usage:
    python3 make_hangul_fixture.py <NotoSansKR[wght].ttf> <out.ttf>
"""

import sys

from fontTools import varLib
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont
from fontTools.subset import Subsetter, Options

# The Hangul text exercised by the fixtures: "한글 안녕" (hangeul annyeong) and
# the individual syllables used in metrics/shaping assertions.
FIXTURE_TEXT = "한글 안녕하세요"


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    src, dst = sys.argv[1], sys.argv[2]

    font = TTFont(src)
    if "fvar" in font:
        instantiateVariableFont(font, {"wght": 400}, inplace=True)

    options = Options()
    options.layout_features = ["*"]  # keep all shaping features
    options.name_IDs = ["*"]
    options.recalc_bounds = True
    options.drop_tables = []
    options.notdef_outline = True  # keep a real .notdef glyph
    subsetter = Subsetter(options=options)
    subsetter.populate(text=FIXTURE_TEXT)
    subsetter.subset(font)

    font.save(dst)
    print(f"wrote {dst}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
