# Reference reading paths

References explain the ideas behind a workflow; Ludus source and tests define
its actual behavior. Choose the path that matches the problem you are solving.
The ordering below is a practical learning recommendation, not a claim that a
book implements Ludus or that every catalogued chapter has been read.

## 1. Learn tool UX

**David Lightbown, _Designing the User Experience of Game Development Tools_
(CRC Press, 2015)** — start with chapter 4, Analysis; chapter 5, Design; then
chapter 6, Evaluation. These explain how to observe work, organize tasks and
evaluate whether the resulting tool helps people complete them.

The consulted editor-design excerpts informed task-based work areas, visible
boundaries and progressive disclosure. Thanks to David Lightbown for those
ideas; the implementation retains Ludus's own Qt/controller/process boundaries.

[Author's site](https://www.uxofgametools.com/) ·
[Ludus excerpt review and adaptations](https://github.com/Alegruz/Ludus/blob/main/docs/architecture/editor-design-review.md)

## 2. Understand responsive tools

**Graham Wihlidal, _Game Engine Toolset Development_ (Thomson Course Technology,
2006)** — chapter 37, Responsive UI During Intensive Processing, pp. 423–430.
Read it when exploring progress, cancellation and background work.

Thanks to Graham Wihlidal: the consulted chapter reinforces keeping intensive
work outside the UI thread. Its historical Windows Forms/exception examples
are not Ludus code; Ludus uses its own supervised adapters and exception-free
engine boundaries.

[Author's projects](https://www.wihlidal.com/projects/) ·
[Ludus project-operation design](https://github.com/Alegruz/Ludus/blob/main/docs/architecture/project-sdk-workflow.md)

## 3. Explore document commands

**Robert Nystrom, _Game Programming Patterns_, Command** — a focused reading
candidate for reversible edits and command ownership. The publisher/author's
online chapter is available below. S2 will evaluate document undo and focused
save routing; this recommendation does not claim those features already ship.

[Read the author's Command chapter](https://gameprogrammingpatterns.com/command.html) ·
[S2 editor architecture](https://github.com/Alegruz/Ludus/blob/main/docs/architecture/editor-architecture.md)

## 4. Read the build tools' own contracts

- [CMake presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html):
  configure/build/test profiles, inheritance and selectable presets.
- [Qt Widgets](https://doc.qt.io/qt-6/qtwidgets-index.html): native widget/layout
  facilities; Ludus targets Qt 6.4-compatible APIs rather than assuming the
  newest documented API exists in its minimum build.
- [Emscripten filesystem](https://emscripten.org/docs/api_reference/Filesystem-API.html):
  browser storage differs from desktop folders and requires explicit persistence.
- [GitHub Pages](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages):
  the static artifact/deployment contract used by this documentation site.

## 5. Reproduce random choices

Start with the [deterministic randomness guide](../guides/randomness.md) to
choose ordered PCG streams or addressed Philox samples. Its source list credits
Salmon et al., O'Neill, Lemire and Vigna, plus the consulted Gems chapters on
independent regeneration, playback isolation and rejection sampling.

The [RNG chapter review](https://github.com/Alegruz/Ludus/blob/main/docs/architecture/randomness-gems-review.md)
explains which ideas were adopted or deferred and corrects claims that would
otherwise mislead implementation. The guide identifies shipped APIs separately
from future policies, persistence, GPU and editor work.

## 6. Own text and identify names

Start with [strings and names](../guides/strings.md) and [hashing](../guides/hashing.md)
for the shipped contracts. The
[strings chapter review](https://github.com/Alegruz/Ludus/blob/main/docs/architecture/strings-gems-review.md)
records consulted sources, exact locators and adaptations.

Thanks to **Stefan Reinalter**, *Compile-Time String Hashing in C++*, Game Engine
Gems 3, chapter 14, pp. 197–205, for precomputing literal metadata; Ludus keeps
exact spellings and binds table-qualified IDs explicitly. Thanks to **James
Boer**, *A Flexible Text Parsing System*, Game Programming Gems 2, §1.17,
pp. 112–117, for compiled token dictionaries, and **Jason Hughes**, *Pointer
Patching Assets*, Game Engine Gems 2, chapter 20, pp. 345–357, for packed
storage inspiration. The cooker/dictionary ideas remain planned; runtime
interning and freezing do not implement deterministic persistence.

The review distinguishes adopted ideas from deferred suggestions and Bloom
filters. Its local PDF links are source locators in an ignored reference library,
not downloadable wiki material. No chapter code is copied into the guide examples.

## Explore the larger local catalog

The [ranked editor reading guide](https://github.com/Alegruz/Ludus/blob/main/docs/architecture/editor-reference-reading-guide.md)
groups 246 catalogued articles/chapters by task and priority. Its PDF/TOC links
refer to a separately supplied, ignored local reference library. They are not
public wiki downloads. Use the bibliographic titles to obtain the sources through
their authors, publishers or libraries; the wiki publishes original explanations
and source links.

When adopting an idea, credit the exact author/title/chapter near affected code,
state what was adapted, and record the detailed review. See
[contribution rules](../contribute/index.md).
