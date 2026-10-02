#pragma once

// Example usage of the Ludus::Text CPU font API (modules/text) for the smoke
// app. This demonstrates the full shaped-run + cold-rasterization flow end to
// end: create a FontSystem, load a real font from bytes, shape a UTF-8 string
// into positioned glyphs, read its measured metrics, and rasterize a glyph into
// an R8 coverage bitmap.
//
// Ludus::Text is a CPU contract (it produces glyph positions and coverage
// masks); it does not touch the GPU. There is no GraphicsText / atlas module in
// the tree yet, so this example turns the coverage mask into an ASCII-art
// preview through the logger rather than uploading a texture. That keeps the
// example self-contained and buildable on the native target that links
// Ludus::Text (the Emscripten/web target does not link it yet).

namespace ludus::smoke::text_demo
{
// Runs the font example exactly once (guarded internally). Safe to call every
// frame; subsequent calls are no-ops. Returns true if the demo ran successfully
// (or had already run), false if the font API reported a failure.
bool RunOnce() noexcept;
} // namespace ludus::smoke::text_demo
