"""Bounded portable raster artifacts/reflection; selected by --raster in the SDK driver."""
import hashlib
import json
from pathlib import Path
import re
import struct

# Thanks to the Slang team, "Using the Reflection API", User Guide,
# https://docs.shader-slang.org/en/latest/external/slang/docs/user-guide/09-reflection.html
# Reflect each target independently. No cross-target CPU packing is inferred.
# This profile rejects storage, resource arrays, extra groups and unsupported inputs.
def raster_interface(document, stage, metal=False):
    points = [x for x in document['entryPoints'] if x['stage'] == stage]
    if len(points) != 1:
        raise RuntimeError('Expected one entry per raster stage')
    point = points[0]
    used = {}
    for resource in point.get('bindings', []):
        bindings = resource.get('bindings', [resource.get('binding', {})])
        if any('used' not in b for b in bindings):
            raise RuntimeError('Raster stage usage is missing; compile stages separately')
        used[resource['name']] = any(b['used'] for b in bindings)
    entries = []
    for resource in document.get('parameters', []):
        if not used.get(resource['name'], False):
            continue
        bindings = resource.get('bindings', [resource.get('binding', {})])
        descriptors = [b for b in bindings if b.get('kind') == 'descriptorTableSlot']
        if metal and not descriptors:
            descriptors = [b for b in bindings if b.get('kind') in ('constantBuffer', 'shaderResource', 'samplerState')]
        if len(descriptors) != 1 or descriptors[0].get('space', 0) != 0:
            raise RuntimeError('Raster resources require one descriptor in group/set zero')
        index = descriptors[0]['index']
        if not 0 <= index < 8 or any(e['binding'] == index for e in entries):
            raise RuntimeError('Raster binding is duplicated or outside the bounded profile')
        kind = resource['type']['kind']
        size = 0
        if kind == 'constantBuffer':
            kind = 'UniformBuffer'
            size = resource['type']['elementVarLayout']['binding']['size']
            if not 0 < size <= 16384:
                raise RuntimeError('Uniform occupied range exceeds the raster profile')
        elif kind == 'samplerState':
            if resource['type'].get('isComparison', False) or 'comparison' in str(resource['type'].get('flavor', '')).lower():
                raise RuntimeError('Comparison samplers are outside the raster profile')
            kind = 'Sampler'
        elif kind == 'resource' and resource['type'].get('baseShape') == 'texture2D':
            if resource['type'].get('access', 'read') not in ('read', 'readOnly') or resource['type'].get('multisample', False):
                raise RuntimeError('Raster textures require non-multisampled read-only sampling')
            result = resource['type'].get('resultType', {})
            if result.get('elementType', {}).get('scalarType') != 'float32':
                raise RuntimeError('Raster textures require float sampling')
            kind = 'Texture2D'
        else:
            raise RuntimeError('Unsupported raster resource shape: ' + kind)
        if metal:
            native_kind = {'UniformBuffer': 'constantBuffer', 'Texture2D': 'shaderResource', 'Sampler': 'samplerState'}[kind]
            native = [b for b in bindings if b.get('kind') == native_kind]
            if len(native) != 1 or native[0]['index'] != index:
                raise RuntimeError('Metal binding remapping is outside this raster profile')
        value = {'binding': index, 'kind': kind, 'size': size, 'name': resource['name']}
        if kind == 'UniformBuffer':
            value['offsets'] = {field['name']: field['binding']['offset'] for field in resource['type']['elementType']['fields']}
        entries.append(value)
    inputs = []
    def input_field(field):
        binding = field.get('binding', {})
        if 'semanticName' in field and field['semanticName'].startswith('SV_'):
            return
        ty = field['type']
        if ty['kind'] == 'struct':
            for child in ty['fields']:
                input_field(child)
            return
        if binding.get('kind') != 'varyingInput':
            raise RuntimeError('Unreflected vertex input')
        count = ty.get('elementCount')
        if ty['kind'] != 'vector' or count not in (2, 3, 4) or ty['elementType'].get('scalarType') != 'float32':
            raise RuntimeError('Vertex input lies outside float2/3/4 profile')
        location = binding['index']
        if not 0 <= location < 8 or any(x['location'] == location for x in inputs):
            raise RuntimeError('Vertex input location is duplicated or out of range')
        inputs.append({'location': location, 'format': 'Float' + str(count), 'name': field['name']})
    if stage == 'vertex':
        for parameter in point.get('parameters', []):
            input_field(parameter)
    return {'entries': entries, 'inputs': inputs}


def glsl_interface(code, reflected, cross_reflection):
    from compile_shader import glsl_es_layout
    if not code.lstrip().startswith('#version 300 es') or re.search(r'\bvoid\s+main\s*\(', code) is None:
        raise RuntimeError('Raster GLSL ES needs a version 300 main entry')
    for token in ('gl_BaseVertex', 'gl_BaseInstance', 'GL_ARB_shader_draw_parameters'):
        if token in code:
            raise RuntimeError('Raster GLSL ES exceeds portable draw semantics')
    blocks = re.findall(r'(layout\(std140\)\s+uniform\s+(\w+)\s*\{[^}]+\}\s*\w*\s*;)', code)
    names = {}
    for entry in reflected['entries']:
        if entry['kind'] != 'UniformBuffer':
            continue
        resources = [x for x in cross_reflection.get('ubos', []) if x.get('binding') == entry['binding']]
        if len(resources) != 1:
            raise RuntimeError('GLSL ES uniform reflection is missing or ambiguous')
        name = resources[0]['name']
        matches = [block for block, block_name in blocks if block_name == name]
        if len(matches) != 1:
            raise RuntimeError('GLSL ES std140 block is missing')
        wrappers = '\n'.join(re.findall(r'struct\s+\w+\s*\{[^}]+\}\s*;', code))
        derived = glsl_es_layout(wrappers + '\n' + matches[0])
        # The parser derives this emitted target's occupied size independently.
        if derived['offsets'] != entry['offsets']:
            raise RuntimeError('GLSL ES emitted uniform offsets differ from target reflection')
        entry['size'] = derived['size']
        names[entry['binding']] = name
    images = cross_reflection.get('separate_images', [])
    samplers = cross_reflection.get('separate_samplers', [])
    textures = {}
    for name in re.findall(r'uniform\s+(?:highp\s+|mediump\s+|lowp\s+)?sampler2D\s+(\w+)\s*;', code):
        pairs = [(image, sampler) for image in images for sampler in samplers
                 if name == 'SPIRV_Cross_Combined' + image['name'] + sampler['name']]
        if len(pairs) != 1:
            raise RuntimeError('Cannot verify GLSL ES combined texture/sampler identity')
        image, sampler = pairs[0]
        if image['binding'] in textures:
            raise RuntimeError('One texture with multiple samplers in one stage is outside this profile')
        textures[image['binding']] = {'name': name, 'sampler': sampler['binding']}
    return names, textures


def wgsl_contract(code, reflected, stage):
    """Verify emitted uniform packing and normalize bounded vertex input locations.

    Slang 2026.1.2 emits WGSL input locations differently from its JSON for some
    semantic combinations. Match every named float input in the selected entry's
    flat input struct, then use that target's reflected locations explicitly.
    Inter-stage outputs and builtins are never rewritten.
    """
    if stage == 'vertex' and reflected['inputs']:
        match = re.search(r'@vertex\s+fn\s+\w+\s*\(\s*\w+\s*:\s*(\w+)\s*\)', code)
        if match is None:
            raise RuntimeError('WGSL vertex input requires one reflected flat struct')
        structure = re.search(r'struct\s+' + re.escape(match[1]) + r'\s*\{([^}]+)\}', code)
        if structure is None:
            raise RuntimeError('WGSL vertex input struct is missing')
        seen = set()
        def location(field):
            original, name, width = field.groups()
            stem = re.sub(r'_\d+$', '', name)
            matches = [x for x in reflected['inputs'] if x['name'] == stem and x['format'] == 'Float' + width]
            if len(matches) != 1 or stem in seen:
                raise RuntimeError('WGSL emitted vertex input differs from reflection')
            seen.add(stem)
            return f"@location({matches[0]['location']}) {name} : vec{width}<f32>"
        body = re.sub(r'@location\((\d+)\)\s+(\w+)\s*:\s*vec([234])<f32>', location, structure[1])
        if seen != {x['name'] for x in reflected['inputs']} or len(re.findall(r'@location', body)) != len(seen):
            raise RuntimeError('WGSL vertex interface is incomplete')
        code = code[:structure.start(1)] + body + code[structure.end(1):]
    for entry in reflected['entries']:
        if entry['kind'] != 'UniformBuffer':
            continue
        declaration = re.search(r'@binding\(' + str(entry['binding']) + r'\)\s+@group\(0\)\s+var<uniform>\s+\w+\s*:\s*(\w+)\s*;', code)
        if declaration is None:
            raise RuntimeError('WGSL emitted uniform binding is missing')
        structure = re.search(r'struct\s+' + re.escape(declaration[1]) + r'\s*\{([^}]+)\}', code)
        if structure is None:
            raise RuntimeError('WGSL emitted uniform struct is missing')
        occupied, alignment, offsets = 0, 1, {}
        for member in structure[1].split(','):
            if not member.strip():
                continue
            field = re.fullmatch(r'\s*(?:@align\((\d+)\)\s*)?(\w+)\s*:\s*(f32|vec[234]<f32>)\s*', member)
            if field is None:
                raise RuntimeError('Browser raster uniforms require flat float/vector members')
            explicit, name, kind = field.groups()
            size, natural = {'f32':(4,4),'vec2<f32>':(8,8),'vec3<f32>':(12,16),'vec4<f32>':(16,16)}[kind]
            align = max(int(explicit or 1), natural)
            if align & (align-1):
                raise RuntimeError('WGSL uniform alignment is invalid')
            alignment = max(alignment, align)
            occupied = (occupied+align-1)//align*align
            offsets[re.sub(r'_\d+$', '', name)] = occupied
            occupied += size
        size = (occupied+alignment-1)//alignment*alignment
        if size != entry['size'] or offsets != entry['offsets']:
            raise RuntimeError('WGSL emitted uniform packing differs from target reflection')
    return code


def glsl_varyings(code, reflection, stage):
    """ES 3.00 links varying identifiers rather than Vulkan location decorations."""
    direction = 'outputs' if stage == 'vertex' else 'inputs'
    qualifier = 'out' if stage == 'vertex' else 'in'
    interface = []
    for value in reflection.get(direction, []):
        location, kind = value.get('location'), value.get('type')
        if not isinstance(location, int) or kind not in ('float', 'vec2', 'vec3', 'vec4') or value.get('array'):
            raise RuntimeError('GLSL ES varyings require bounded scalar/vector locations')
        old = re.sub(r'[^A-Za-z_0-9]', '_', value['name'])
        new = f'ludusVarying{location}'
        if re.search(r'\b' + qualifier + r'\s+(?:highp\s+|mediump\s+|lowp\s+)?' + kind + r'\s+' + re.escape(old) + r'\s*;', code) is None:
            raise RuntimeError('GLSL ES emitted varying differs from location reflection')
        if old != new and re.search(r'\b' + new + r'\b', code):
            raise RuntimeError('GLSL ES canonical varying identifier conflicts with source')
        code = re.sub(r'\b' + re.escape(old) + r'\b', new, code)
        interface.append((location, kind))
    return code, sorted(interface)


def compile_raster(args, lock):
    from compile_shader import entry, run
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    extra = [part for path in args.include for part in ('-I', path)] + ['-D' + value for value in args.define]
    commands, dependencies, artifacts, interfaces = [], [], {}, {}
    if not args.metal:
        if not args.validator or hashlib.sha256(Path(args.validator).read_bytes()).hexdigest() != lock['spirv_tools']['val_sha256']:
            raise RuntimeError('Raster SPIR-V validator differs from SDK pin')
    if args.spirv_cross:
        pin = json.loads(Path(args.spirv_cross_lock).read_text())['spirv_cross']
        tool = Path(args.spirv_cross).resolve()
        build = json.loads(Path(str(tool) + '.build.json').read_text())
        if any(build.get(key) != pin[key] for key in ('tag', 'commit', 'source_sha256')) or build.get('binary_sha256') != hashlib.sha256(tool.read_bytes()).hexdigest():
            raise RuntimeError('Raster SPIRV-Cross identity differs from verified pin')
    target = 'metal' if args.metal else 'spirv'
    for stage, name in (('vertex', args.vertex), ('fragment', args.fragment)):
        artifact = output / f'{args.name}.{stage}.{"metal" if args.metal else "spv"}'
        reflection, depfile = output / f'{stage}.reflection.json', output / f'{stage}.d'
        command = [args.compiler, args.source, '-target', target, '-profile', 'sm_6_0' if args.metal else 'spirv_1_3',
                   '-entry', name, '-stage', stage, '-o', str(artifact), '-reflection-json', str(reflection), '-depfile', str(depfile), *extra]
        commands.append(command); run(command)
        dependencies.append(depfile.read_text().split(':', 1)[1].strip())
        interfaces[(target, stage)] = raster_interface(json.loads(reflection.read_text()), stage, args.metal)
        if args.metal:
            code = artifact.read_text()
            names = re.findall(r'\[\[' + stage + r'\]\]\s+\w+\s+(\w+)\s*\(', code)
            if len(names) != 1:
                raise RuntimeError('Expected one emitted Metal raster entry')
            artifacts[(target, stage)] = (code, names[0])
        else:
            run([args.validator, '--target-env', 'vulkan1.1', str(artifact)])
            artifacts[(target, stage)] = (artifact.read_bytes(), entry(artifact))
            if args.spirv_cross:
                essl = output / f'{args.name}.{stage}.essl'
                command = [args.spirv_cross, str(artifact), '--version', '300', '--es', '--fixup-clipspace', '--output', str(essl)]
                commands.append(command); run(command)
                cross = json.loads(run([args.spirv_cross, str(artifact), '--reflect']))
                interface = raster_interface(json.loads(reflection.read_text()), stage)
                code, varyings = glsl_varyings(essl.read_text(), cross, stage)
                essl.write_text(code)
                interface['varyings'] = varyings
                names, textures = glsl_interface(code, interface, cross)
                interface['blocks'], interface['textures'] = names, textures
                interfaces[('glsl', stage)] = interface
                artifacts[('glsl', stage)] = (essl.read_text(), 'main')
    if not args.metal:
        for stage, source_entry in (('vertex', args.vertex), ('fragment', args.fragment)):
            artifact = output / f'{args.name}.{stage}.wgsl'
            reflection, depfile = output / f'{stage}.wgsl.reflection.json', output / f'{stage}.wgsl.d'
            command = [args.compiler, args.source, '-target', 'wgsl', '-profile', 'sm_6_0', '-entry', source_entry,
                       '-stage', stage, '-o', str(artifact), '-reflection-json', str(reflection), '-depfile', str(depfile), *extra]
            commands.append(command); run(command)
            dependencies.append(depfile.read_text().split(':', 1)[1].strip())
            interface = raster_interface(json.loads(reflection.read_text()), stage)
            code = wgsl_contract(artifact.read_text(), interface, stage)
            names = re.findall(r'@' + stage + r'\s+fn\s+(\w+)', code)
            if len(names) != 1:
                raise RuntimeError('Expected one emitted WGSL raster entry')
            artifact.write_text(code)
            artifacts[('wgsl', stage)] = (code, names[0])
            interfaces[('wgsl', stage)] = interface
    if args.spirv_cross and interfaces[('glsl', 'vertex')]['varyings'] != interfaces[('glsl', 'fragment')]['varyings']:
        raise RuntimeError('GLSL ES raster stage varying interfaces differ')
    header = '#pragma once\n#include <ludus/foundation/base/config.h>\n#include <ludus/graphics/rhi/raster.h>\n'
    header += f'namespace ludus::shaders::{args.name} {{\n'
    for (backend, stage), (data, name) in artifacts.items():
        label = backend.upper() + '_' + stage.upper()
        interface = interfaces[(backend, stage)]
        if isinstance(data, bytes):
            words = struct.unpack('<' + 'I' * (len(data) // 4), data)
            header += f'inline constexpr foundation::uint32 {label}[] = {{' + ','.join(f'0x{x:08x}U' for x in words) + '};\n'
        else:
            delimiter = 'R_' + label
            if f'){delimiter}"' in data:
                raise RuntimeError('Raster source conflicts with raw string delimiter')
            header += f'inline constexpr char {label}[] = R"{delimiter}(' + data + f'){delimiter}";\n'
        if interface['entries']:
            header += f'inline constexpr graphics::rhi::RasterBinding {label}_BINDINGS[] = {{\n'
            for binding in interface['entries']:
                header += '{' + f'{binding["binding"]},graphics::rhi::RasterBindingKind::{binding["kind"]},graphics::rhi::RasterVisibility::{stage.title()},{binding["size"]}' + '},\n'
            header += '};\n'
        if interface['inputs']:
            header += f'inline constexpr graphics::rhi::RasterShaderInput {label}_INPUTS[] = {{\n'
            for value in interface['inputs']:
                header += '{' + f'{value["location"]},graphics::rhi::RasterVertexFormat::{value["format"]}' + '},\n'
            header += '};\n'
        header += f'inline graphics::rhi::RasterShaderDescription {backend.title()}{stage.title()}() noexcept {{\n graphics::rhi::RasterShaderDescription result;\n'
        header += f' result.Artifact.Stage = graphics::rhi::ShaderStage::{stage.title()};\n'
        field = {'metal': 'Msl', 'spirv': 'Spirv', 'wgsl': 'Wgsl', 'glsl': 'GlslEs'}[backend]
        header += f' result.Artifact.{field} = {label}; result.Artifact.{field}Entry = "{name}";\n'
        if interface['entries']:
            header += f' result.Bindings = {label}_BINDINGS;\n'
        if interface['inputs']:
            header += f' result.Inputs = {label}_INPUTS;\n'
        for binding, block in interface.get('blocks', {}).items():
            header += f' result.UniformBlocks[{binding}] = "{block}";\n'
        for binding, texture in interface.get('textures', {}).items():
            header += f' result.TextureNames[{binding}] = "{texture["name"]}"; result.TextureSamplers[{binding}] = {texture["sampler"]};\n'
        header += ' return result;\n}\n'
    for stage in ('vertex', 'fragment'):
        if args.metal:
            header += f'inline auto {stage.title()}() noexcept {{ return Metal{stage.title()}(); }}\n'
        else:
            header += f'inline auto {stage.title()}() noexcept {{\n#if defined(LUDUS_PLATFORM_WEB)\n auto result = Wgsl{stage.title()}();\n'
            if args.spirv_cross:
                header += f' auto glsl = Glsl{stage.title()}(); result.Artifact.GlslEs = glsl.Artifact.GlslEs; result.Artifact.GlslEsEntry = glsl.Artifact.GlslEsEntry;\n'
                # Web source/metadata is selected after startup, so packing is
                # verified equal here; native target packing remains independent.
                a, b = interfaces[('wgsl', stage)], interfaces[('glsl', stage)]
                if [(x['binding'], x['kind'], x['size'], x.get('offsets')) for x in a['entries']] != [(x['binding'], x['kind'], x['size'], x.get('offsets')) for x in b['entries']] or a['inputs'] != b['inputs']:
                    raise RuntimeError('Browser raster uniform/layout packing differs between WGSL and GLSL ES')
                header += ' for (foundation::usize i = 0; i < 8; ++i) { result.UniformBlocks[i] = glsl.UniformBlocks[i]; result.TextureNames[i] = glsl.TextureNames[i]; result.TextureSamplers[i] = glsl.TextureSamplers[i]; }\n'
            header += f' return result;\n#else\n return Spirv{stage.title()}();\n#endif\n}}\n'
    header += '}\n'
    manifest = {'compiler': lock['slang'], 'commands': commands, 'interfaces': {backend + '_' + stage: info for (backend, stage), info in interfaces.items()},
                'sha256': {backend + '_' + stage: hashlib.sha256(data if isinstance(data, bytes) else data.encode()).hexdigest() for (backend, stage), (data, _) in artifacts.items()},
                'layout': 'See separate target reflection JSON. CPU packing must match the selected target; no native cross-target equality is assumed.'}
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    dest = output / f'{args.name}.h'
    escaped = str(dest).replace(' ', '\\ ').replace('#', '\\#')
    (output / 'shader.d').write_text(escaped + ': ' + ' '.join(dependencies) + '\n')
    dest.write_text(header)
