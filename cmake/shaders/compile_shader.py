#!/usr/bin/env python3
"""Offline, pinned Slang CLI driver for installed Ludus consumers."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys


def run(command):
    print('+ ' + ' '.join(command), flush=True)
    result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(result.stdout)
    return result.stdout


def entry(path):
    data = path.read_bytes()
    words = struct.unpack('<' + 'I' * (len(data) // 4), data)
    index = 5
    names = []
    while index < len(words):
        count, op = words[index] >> 16, words[index] & 65535
        if count == 0 or index + count > len(words):
            raise RuntimeError('Malformed SPIR-V instruction')
        if op == 15:
            names.append(struct.pack('<' + 'I' * (count - 3), *words[index + 3:index + count]).split(b'\0')[0].decode())
        index += count
    if len(names) != 1:
        raise RuntimeError('Expected one entry per SPIR-V module')
    return names[0]


def uniform_size(path):
    parameters = json.loads(path.read_text())["parameters"]
    if not parameters:
        return 0
    if len(parameters) != 1:
        raise RuntimeError("Fullscreen API supports only one uniform resource")
    resource = parameters[0]
    binding = resource["binding"]
    if (binding["kind"] != "descriptorTableSlot" or binding["index"] != 0 or
            binding.get("space", 0) != 0 or resource["type"]["kind"] != "constantBuffer"):
        raise RuntimeError("Fullscreen API requires a constant buffer at set/group 0 binding 0")
    size = resource["type"]["elementVarLayout"]["binding"]["size"]
    if size < 1 or size > 4096:
        raise RuntimeError("Uniform exceeds fullscreen API bounds")
    return size


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('source', 'output', 'name', 'vertex', 'fragment', 'compiler', 'validator', 'lock'):
        parser.add_argument('--' + key, required=True)
    parser.add_argument('--include', action='append', default=[])
    parser.add_argument('--define', action='append', default=[])
    args = parser.parse_args()
    lock = json.loads(Path(args.lock).read_text())
    if run([args.compiler, '-version']).strip() != lock['slang']['version']:
        raise RuntimeError('Slang version differs from SDK pin')
    if hashlib.sha256(Path(args.validator).read_bytes()).hexdigest() != lock['spirv_tools']['val_sha256']:
        raise RuntimeError('SPIR-V validator differs from SDK pin')
    if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*', args.name):
        raise RuntimeError('Invalid shader name')
    output = Path(args.output); output.mkdir(parents=True, exist_ok=True)
    extra = [part for path in args.include for part in ('-I', path)] + ['-D' + value for value in args.define]
    commands = []; dependencies = []
    entries = {}
    for stage, source_entry in (('vertex', args.vertex), ('fragment', args.fragment)):
        binary = output / f'{args.name}.{stage}.spv'
        depfile = output / f'{stage}.d'
        command = [args.compiler, args.source, '-target', 'spirv', '-profile', 'spirv_1_3', '-entry', source_entry,
                   '-stage', stage, '-o', str(binary), '-reflection-json', str(output / f'{stage}.reflection.json'), '-depfile', str(depfile), *extra]
        commands.append(command); run(command)
        run([args.validator, '--target-env', 'vulkan1.1', str(binary)])
        dependencies.append(depfile.read_text().split(':', 1)[1].strip())
        entries[stage] = entry(binary)
    wgsl = output / f'{args.name}.wgsl'; depfile = output / 'wgsl.d'
    command = [args.compiler, args.source, '-target', 'wgsl', '-profile', 'sm_6_0', '-entry', args.vertex,
               '-entry', args.fragment, '-o', str(wgsl), '-reflection-json', str(output / 'wgsl.reflection.json'), '-depfile', str(depfile), *extra]
    commands.append(command); run(command); dependencies.append(depfile.read_text().split(':', 1)[1].strip())
    sizes = {stage: uniform_size(output / f'{stage}.reflection.json') for stage in ('vertex', 'fragment', 'wgsl')}
    code = wgsl.read_text()
    bindings = re.findall(r'@binding\((\d+)\)\s+@group\((\d+)\)\s+var<(\w+)>', code)
    if any(binding != ('0', '0', 'uniform') for binding in bindings) or len(bindings) > 1:
        raise RuntimeError('Emitted WGSL resources do not fit the fullscreen binding contract')
    wgsl_entries = dict(re.findall(r'@(vertex|fragment)\s+fn\s+(\w+)', code))
    if set(wgsl_entries) != {'vertex', 'fragment'}:
        raise RuntimeError('Expected one vertex and one fragment WGSL entry')
    header = '#pragma once\n#include <ludus/foundation/base/config.h>\n#include <ludus/graphics/rhi/render.h>\n'
    header += f'namespace ludus::shaders::{args.name} {{\n#if !defined(LUDUS_PLATFORM_WEB)\n'
    for stage in ('vertex', 'fragment'):
        data = (output / f'{args.name}.{stage}.spv').read_bytes()
        words = struct.unpack('<' + 'I' * (len(data) // 4), data)
        header += f'inline constexpr foundation::uint32 {stage.upper()}_SPIRV[] = {{\n'
        header += ',\n'.join('    ' + ', '.join(f'0x{x:08x}U' for x in words[i:i+8]) for i in range(0, len(words), 8)) + '\n};\n'
    if ')LUDUS_WGSL"' in code:
        raise RuntimeError('WGSL conflicts with header delimiter')
    header += '#else\ninline constexpr char WGSL[] = R"LUDUS_WGSL(' + code + ')LUDUS_WGSL";\n#endif\n'
    for stage in ('vertex', 'fragment'):
        header += f'inline graphics::rhi::ShaderDescription {stage.title()}() noexcept {{\n graphics::rhi::ShaderDescription result; result.Stage = graphics::rhi::ShaderStage::{stage.title()};\n'
        wgsl_size = sizes['wgsl']
        header += f'#if defined(LUDUS_PLATFORM_WEB)\n result.UniformSize = {wgsl_size}; result.Wgsl = WGSL; result.WgslEntry = "{wgsl_entries[stage]}";\n#else\n result.UniformSize = {sizes[stage]}; result.Spirv = {stage.upper()}_SPIRV; result.SpirvEntry = "{entries[stage]}";\n#endif\n return result;\n}}\n'
    header += '}\n'
    target = output / f'{args.name}.h'
    # Header last: failed compiler/validator never presents a completed build output.
    target.write_text(header)
    escaped_target = str(target).replace(' ', '\\ ').replace('#', '\\#')
    (output / 'shader.d').write_text(escaped_target + ': ' + ' '.join(dependencies) + '\n')
    (output / 'manifest.json').write_text(json.dumps({'compiler': lock['slang'], 'commands': commands,
        'uniform_sizes': sizes, 'spirv_entries': entries, 'wgsl_entries': wgsl_entries, 'profiles': {'spirv': 'spirv_1_3', 'wgsl': 'sm_6_0'},
        'sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in output.iterdir() if p.suffix in ('.spv', '.wgsl')},
        'layout': 'See separate per-target reflection JSON; never assume packing equality.'}, indent=2) + '\n')

if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, ValueError, KeyError) as error:
        print('Ludus shader build: ' + str(error), file=sys.stderr)
        sys.exit(1)
