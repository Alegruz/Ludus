"""Deterministic bounded raw-texel mip cooker; no image codec or runtime compiler.

Input JSON: width, height, format (R8Unorm/Rg8Unorm/Rgba8Unorm/Rgba8Srgb),
semantic (color/data/normal/mask), pixels (top-down tightly packed integer bytes).
Optional mask cutoff (0,1] preserves nearest attainable threshold coverage per mip.
Output: portable immutable upload header and inspectable source/level manifest.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re

# Thanks to Khronos, "KTX File Format Specification", 2.0, section 3.10.2
# "Use of Transfer Functions": decode -> scale -> encode for sRGB mips.
# https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html#_use_of_transfer_functions
# This owns an uncompressed raw-texel profile, not a KTX reader/transcoder.
def decode(value):
    v = value / 255
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def encode(value):
    return quantize((12.92 * value if value <= 0.0031308 else 1.055 * value ** (1 / 2.4) - 0.055) * 255)


def quantize(value):
    return max(0, min(255, math.floor(value + 0.5)))


def cook(document):
    formats = {'R8Unorm': 1, 'Rg8Unorm': 2, 'Rgba8Unorm': 4, 'Rgba8Srgb': 4}
    width, height, fmt = document.get('width'), document.get('height'), document.get('format')
    semantic, pixels = document.get('semantic'), document.get('pixels')
    if type(width) is not int or type(height) is not int or not 1 <= width <= 4096 or not 1 <= height <= 4096:
        raise ValueError('Texture dimensions must be integer 1..4096')
    if fmt not in formats or semantic not in ('color', 'data', 'normal', 'mask'):
        raise ValueError('Unknown portable format/semantic')
    channels = formats[fmt]
    probe_width, probe_height, footprint = width, height, 0
    while True:
        footprint += (probe_width * channels + 3) // 4 * 4 * probe_height
        if probe_width == probe_height == 1:
            break
        probe_width, probe_height = max(1, probe_width // 2), max(1, probe_height // 2)
    if footprint > 64 * 1024 * 1024:
        raise ValueError('Complete mip upload exceeds the 64 MiB portable profile')
    if (fmt == 'Rgba8Srgb' and semantic != 'color') or (semantic in ('color', 'normal') and channels != 4):
        raise ValueError('sRGB is color only; color/normal require RGBA8')
    if not isinstance(pixels, list) or len(pixels) != width * height * channels or any(type(v) is not int or not 0 <= v <= 255 for v in pixels):
        raise ValueError('Source must contain complete tightly packed byte texels')
    cutoff = document.get('cutoff')
    if cutoff is not None and (semantic != 'mask' or channels not in (1, 4) or type(cutoff) not in (int, float) or not math.isfinite(cutoff) or not 0 < cutoff <= 1):
        raise ValueError('Coverage cutoff requires R8/RGBA8 mask and finite (0,1] threshold')
    alpha = channels - 1
    target = sum(pixels[i] / 255 >= cutoff for i in range(alpha, len(pixels), channels)) / (width * height) if cutoff is not None else 0
    blob, levels = bytearray(), []
    while True:
        pitch = (width * channels + 3) // 4 * 4
        level = {'width': width, 'height': height, 'offset': len(blob), 'row_pitch': pitch}
        for row in range(height):
            blob.extend(pixels[row * width * channels:(row + 1) * width * channels])
            blob.extend(bytes(pitch - width * channels))
        levels.append(level)
        if width == height == 1:
            break
        next_width, next_height = max(1, width // 2), max(1, height // 2)
        reduced = []
        # Exact box footprints include fractional odd edges with equal output area.
        for y in range(next_height):
            for x in range(next_width):
                left, right = x * width / next_width, (x + 1) * width / next_width
                top, bottom = y * height / next_height, (y + 1) * height / next_height
                samples = [(pixels[(sy * width + sx) * channels:(sy * width + sx + 1) * channels],
                            (min(right, sx + 1) - max(left, sx)) * (min(bottom, sy + 1) - max(top, sy)))
                           for sy in range(math.floor(top), math.ceil(bottom))
                           for sx in range(math.floor(left), math.ceil(right))]
                area = sum(weight for _, weight in samples)
                alpha_weight = sum(p[3] / 255 * weight for p, weight in samples) if semantic == 'color' else 0
                value = []
                for channel in range(channels):
                    if semantic == 'color' and channel < 3:
                        # Filter associated color to avoid transparent RGB bleeding;
                        # the output remains straight alpha for the material contract.
                        average = sum((decode(p[channel]) if fmt == 'Rgba8Srgb' else p[channel] / 255) * p[3] / 255 * weight
                                      for p, weight in samples) / alpha_weight if alpha_weight > 0 else 0
                    else:
                        average = sum(p[channel] / 255 * weight for p, weight in samples) / area
                    value.append(encode(average) if fmt == 'Rgba8Srgb' and channel < 3 else quantize(average * 255))
                if semantic == 'normal':
                    normal = [sum((p[c] / 127.5 - 1) * weight for p, weight in samples) / area for c in range(3)]
                    length = math.sqrt(sum(v * v for v in normal))
                    normal = [v / length for v in normal] if length > 1e-12 else [0, 0, 1]
                    value[:3] = [quantize((v + 1) * 127.5) for v in normal]
                reduced.extend(value)
        if cutoff is not None:
            # Discrete coverage cannot always equal the parent (e.g. one texel).
            # Choose the nearest count attainable by a single monotone alpha scale.
            values = reduced[alpha::channels]
            scales = [0, 1] + [(math.ceil(cutoff * 255) - 0.5 + 1e-9) / v for v in set(values) if v > 0]
            def score(scale):
                coverage = sum(quantize(v * scale) / 255 >= cutoff for v in values) / len(values)
                return abs(coverage - target), abs(scale - 1), scale
            scale = min(scales, key=score)
            for i in range(alpha, len(reduced), channels):
                reduced[i] = quantize(reduced[i] * scale)
            level['next_coverage_error'] = score(scale)[0]
        width, height, pixels = next_width, next_height, reduced
    return bytes(blob), levels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--name', required=True)
    args = parser.parse_args()
    if re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', args.name) is None:
        raise ValueError('Texture name must be a C++ identifier')
    source_path = Path(args.source)
    if source_path.stat().st_size > 512 * 1024 * 1024:
        raise ValueError('Source JSON exceeds bounded cooker profile')
    source = source_path.read_bytes()
    document = json.loads(source)
    blob, levels = cook(document)
    out = Path(args.output)
    out.mkdir(parents=True, exist_ok=True)
    prefix = '#pragma once\n#include <ludus/graphics/rhi/raster.h>\n'
    prefix += f'namespace ludus::textures::{args.name} {{\n'
    prefix += 'inline constexpr foundation::uint8 BYTES[] = {' + ','.join(map(str, blob)) + '};\n'
    prefix += 'inline constexpr graphics::rhi::TextureMipUpload MIPS[] = {' + ','.join('{' + f'{v["offset"]},{v["row_pitch"]}' + '}' for v in levels) + '};\n'
    prefix += 'inline graphics::rhi::TextureDescription Description() noexcept { return {' + f'{document["width"]},{document["height"]},graphics::rhi::RasterFormat::{document["format"]},false,{len(levels)},true' + '}; }\n'
    prefix += 'inline graphics::rhi::TextureUpload Upload() noexcept { return {BYTES,0,MIPS}; }\n}\n'
    (out / f'{args.name}.h').write_text(prefix)
    (out / 'manifest.json').write_text(json.dumps({'source_sha256': hashlib.sha256(source).hexdigest(), 'upload_sha256': hashlib.sha256(blob).hexdigest(), 'semantic': document['semantic'], 'format': document['format'], 'levels': levels}, indent=2) + '\n')


if __name__ == '__main__':
    main()
