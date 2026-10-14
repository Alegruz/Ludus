"""Cook verified RG1 packages into typed, numeric Renderer execution headers.

The neutral records belong to Ludus::RenderGraphFormat, not this tool or Renderer.
Only the static opaque six-location profile is admitted; runtime scene/camera
values remain separate. Unsupported resource topology and recipes reject before
writing the output. No shader configurations are synthesized.
"""
from __future__ import annotations

import json
from pathlib import Path
import struct

from . import render_graph_build as build

MAX_PASSES = 16
MAX_RESOURCES = 32
MAX_CAMERAS = 8
FORMAT_VERSION = 1
ABSENT = 0xFFFFFFFF


def resolve_generation(path):
    path = Path(path).resolve()
    if (path / 'current.json').is_file():
        pointer = build.document(build.read(path / 'current.json'), 'current generation')
        build.keys(pointer, ('version', 'generation'), 'current generation')
        build.require(pointer['version'] == 1 and type(pointer['generation']) is str and
                      len(pointer['generation']) == 64 and all(c in '0123456789abcdef' for c in pointer['generation']),
                      'Package', str(path), 'invalid generation pointer')
        path = path / 'generations' / pointer['generation']
    return path


def lower_plan(package, configuration):
    plans = [p for p in package['plans'] if p['configuration'] == configuration]
    build.require(len(plans) == 1, 'Configuration', configuration, 'one declared root configuration required')
    plan = plans[0]
    resources = sorted(plan['resources'])
    rasters = [p for p in plan['passes'] if p['kind'] == 'Raster']
    cameras = sorted({p['view'] for p in rasters})
    build.require(all(p['kind'] in ('Raster', 'Export', 'Present') for p in plan['passes']) and
                  all(p['configuration'] in ('ColorAndDepth', 'DepthOnly') and
                      p['technique'] == 'DefaultStaticOpaque.' + p['configuration'] and
                      p['vertex_layout'] == 'RendererStatic' and p['material'] == 'DefaultOpaque' for p in rasters),
                  'Configuration', configuration, 'unqualified operation/layout/material/technique')
    build.require(0 < len(rasters) <= MAX_PASSES and 0 < len(resources) <= MAX_RESOURCES and len(cameras) <= MAX_CAMERAS,
                  'Capacity', configuration, 'Renderer execution limits exceeded')
    build.require(len({p['scene'] for p in rasters}) == 1, 'Configuration', configuration, 'one World extraction per execution profile')
    backing = {r: r for r in resources}
    for operation in rasters:
        attachments = operation['attachments']
        depth = attachments['depth']['resource']
        if 'color' in attachments:
            color = attachments['color']['resource']
            build.require(backing[depth] in (depth, color), 'Configuration', depth, 'independent/shared depth attachment topology unsupported')
            backing[depth] = color
    depth_owners = [backing[r] for r in resources if plan['resources'][r]['type'] == 'DepthTexture']
    surface_owners = {backing[r] for r in resources if plan['resources'][backing[r]]['presentation']}
    build.require(len(depth_owners) == len(set(depth_owners)) and len(surface_owners) <= 1,
                  'Configuration', configuration, 'distinct logical images cannot alias one private depth/surface')
    output_resources = {r['resource'] for r in plan['roots'] if r['kind'] == 'Export'}
    resource_records = []
    for resource in resources:
        value = plan['resources'][resource]
        build.require(value['format'] in ('RGBA8Unorm', 'Depth32Float') and value['samples'] == 1 and
                      not value['initialized'] and (not value['imported'] or value['presentation']),
                      'Configuration', resource, 'unqualified format/sample/import/history profile')
        parent = plan['resources'][backing[resource]]
        resource_records.append({'Width': value['extent'][0], 'Height': value['extent'][1],
                                 'Kind': 'Color' if value['type'] == 'ColorTexture' else 'Depth',
                                 'Backing': resources.index(backing[resource]), 'IsSurface': int(parent['presentation']),
                                 'IsExported': int(resource in output_resources)})
    pass_records = []
    color_defined = {r: False for r in resources}
    for operation in rasters:
        choices = [r for r in package['recipes'] if r['technique'] == operation['technique']]
        build.require(choices and all(r['state']['color'] == ('RGBA8Unorm' if operation['configuration'] == 'ColorAndDepth' else 'None') and
                      r['state']['depth'] == 'Depth32Float' and r['state']['samples'] == 1 and r['state']['topology'] == 'TriangleList' and r['state']['cull'] == 'None' and r['state']['front'] == 'CounterClockwise' and
                      r['state']['depth_compare'] == 'Less' and r['state']['depth_write'] and r['state']['blend'] == 'Opaque'
                      for r in choices), 'Configuration', operation['id'], 'recipe differs from admitted static Renderer state')
        attachment = operation['attachments']
        depth = attachment['depth']['resource']
        color = attachment.get('color')
        physical = backing[depth]
        color_load = color['load'] if color else ('Load' if color_defined[physical] else 'Discard')
        color_store = color['store'] if color else ('Store' if color_defined[physical] else 'Discard')
        color_defined[physical] = color_store == 'Store' and (color_load == 'Clear' or color_load == 'Load' and color_defined[physical])
        pass_records.append({'Program': operation['configuration'], 'Camera': cameras.index(operation['view']),
                             'Color': resources.index(color['resource']) if color else ABSENT,
                             'Depth': resources.index(depth), 'ColorLoad': color_load, 'ColorStore': color_store,
                             'DepthLoad': attachment['depth']['load'], 'DepthStore': attachment['depth']['store'],
                             'AllowsRepeatedCoverage': 0})
    return plan, resource_records, pass_records, cameras


def _emit_runtime(path, destination, *, configuration='Default', name='DefaultGraph'):
    """Atomically emit a compiled header from a fully integrity-checked generation.

    The header has static artifact storage and neutral numeric plan records. It
    supports every admitted Metal/SPIR-V/WGSL/GLSL representation present in the
    package. Callers supply the installed renderer_composite recipe descriptions;
    no built-in source path/entry is guessed or duplicated. Existing destination
    is replaced only after the complete package and execution contract validate.
    """
    build.name(name, 'generated namespace')
    path = resolve_generation(path)
    package = build.verify_package(path)
    build.require(package['id'] == 'RendererStaticBuild', 'Configuration', package['id'],
                  'use an explicitly authored --renderer-static build manifest')
    plan, resources, passes, cameras = lower_plan(package, configuration)
    stages = {}
    for technique in plan['techniques']:
        mode = technique.split('.')[1]
        recipes = [r for r in package['recipes'] if r['technique'] == technique]
        for recipe in recipes:
            for stage in ('vertex', 'fragment'):
                matches = [p for p in package['programs'] if p['key'] == recipe[stage] and p['profile'] == recipe['profile']]
                build.require(len(matches) == 1, 'Package', technique, 'stage identity is ambiguous')
                key = (mode, stage, recipe['profile'])
                build.require(key not in stages or stages[key]['key'] == matches[0]['key'],
                              'Configuration', technique, 'multiple stage programs for one static role/profile')
                stages[key] = matches[0]
    if 'DefaultStaticOpaque.ColorAndDepth' in plan['techniques'] and 'DefaultStaticOpaque.DepthOnly' in plan['techniques']:
        build.require(all(stages[('ColorAndDepth', 'vertex', t)]['key'] == stages[('DepthOnly', 'vertex', t)]['key']
                          for t in package['profiles']), 'Configuration', name, 'geometry programs must match exactly')
    lines = ['#pragma once', '#include <ludus/foundation/base/compiler.h>',
             '#include <ludus/foundation/containers/static_array.hpp>',
             '#include <ludus/graphics/renderer/render_graph.hpp>', '', 'namespace ludus::render_graph_packages::' + name, '{',
             'namespace format = content::render_graph_format;', 'namespace renderer = graphics::renderer;', 'namespace rhi = graphics::rhi;',
             f'inline constexpr foundation::usize RESOURCE_COUNT = {len(resources)};',
             f'inline constexpr foundation::usize PASS_COUNT = {len(passes)};',
             f'inline constexpr foundation::usize CAMERA_COUNT = {len(cameras)};']
    for label, values, ty in [('RESOURCES', resources, 'Resource'), ('PASSES', passes, 'Pass')]:
        lines += [f'inline constexpr foundation::StaticArray<format::{ty}, {"RESOURCE_COUNT" if label == "RESOURCES" else "PASS_COUNT"}> {label}', '{', '    {']
        for value in values:
            cells = []
            for field, item in value.items():
                if isinstance(item, str):
                    kind = {'Kind': 'Aspect', 'Program': 'Technique', 'ColorLoad': 'Load', 'DepthLoad': 'Load', 'ColorStore': 'Store', 'DepthStore': 'Store'}[field]
                    cells.append('format::' + kind + '::' + item)
                else:
                    cells.append('format::ABSENT' if item == ABSENT else str(item))
            lines.append('        {' + ', '.join(cells) + '},')
        lines += ['    },', '};']
    camera_targets = []
    for camera in cameras:
        choices = [c for c in plan['cameras'] if c['id'] == camera and c['active']]
        build.require(len(choices) == 1, 'Configuration', camera, 'active camera target required')
        camera_targets.append(sorted(plan['resources']).index(choices[0]['target']))
    lines += ['inline constexpr foundation::StaticArray<format::Camera, CAMERA_COUNT> CAMERAS', '{', '    {']
    lines += ['        {' + str(target) + '},' for target in camera_targets]
    lines += ['    },', '};', '/// Return numeric plan and borrowed immutable record tables.',
              '[[nodiscard]] LUDUS_INLINE renderer::RenderGraphPlan MakePlan() noexcept', '{',
              f'    return {{{{format::VERSION, RESOURCE_COUNT, PASS_COUNT, {len(cameras)}}},',
              '        {RESOURCES.GetData(), RESOURCES.GetSize()}, {CAMERAS.GetData(), CAMERAS.GetSize()}, {PASSES.GetData(), PASSES.GetSize()}};', '}']
    artifact_labels = {}
    for ordinal, (key, program) in enumerate(sorted(stages.items())):
        mode, stage, target = key
        descriptor = program['descriptor']
        build.require(not descriptor['interface']['entries'], 'Configuration', program['id'], 'runtime static profile has no shader bindings')
        expected = [{'location': i, 'format': 'Float3' if i == 0 else 'Float4'} for i in range(6)] if stage == 'vertex' else []
        build.require([{k: v for k, v in x.items() if k != 'name'} for x in descriptor['interface']['inputs']] == expected,
                      'Configuration', program['id'], 'static vertex/instance interface mismatch')
        label = 'PROGRAM_' + str(ordinal)
        artifact_labels[key] = (label, descriptor)
        key = program['key']
        artifact_name = Path(descriptor['artifact'])
        build.require(len(key) == 64 and all(c in '0123456789abcdef' for c in key) and
                      not artifact_name.is_absolute() and '..' not in artifact_name.parts,
                      'Package', label, 'invalid stage/artifact identity')
        artifact = build.read(path / 'programs' / key / artifact_name, 2 * 1024 * 1024)
        if target == 'spirv':
            build.require(len(artifact) % 4 == 0, 'Package', label, 'SPIR-V word size mismatch')
            words = struct.unpack('<' + 'I' * (len(artifact) // 4), artifact)
            lines += [f'inline constexpr foundation::usize {label}_WORD_COUNT = {len(words)};',
                      f'inline constexpr foundation::StaticArray<foundation::uint32, {label}_WORD_COUNT> {label}', '{', '    {']
            for i in range(0, len(words), 8):
                lines.append('        ' + ', '.join(hex(w) for w in words[i:i + 8]) + ',')
            lines += ['    },', '};']
        else:
            text = artifact.decode('utf-8')
            delimiter = 'RG' + str(ordinal)
            build.require(')' + delimiter + '"' not in text, 'Package', label, 'raw-string delimiter collision')
            lines.append('inline constexpr char ' + label + '[] = R"' + delimiter + '(' + text + ')' + delimiter + '";')
    lines += ['/// Return cooked stage descriptions backed by immutable static storage.',
              '[[nodiscard]] LUDUS_INLINE renderer::RenderGraphPrograms MakePrograms(',
              '    const rhi::RasterShaderDescription& compositeVertex,',
              '    const rhi::RasterShaderDescription& compositeFragment) noexcept', '{', '    renderer::RenderGraphPrograms result;']
    for mode in ('ColorAndDepth', 'DepthOnly'):
        for stage in ('vertex', 'fragment'):
            member = ('Color' if mode == 'ColorAndDepth' else 'Depth') + stage.title()
            keys = [key for key in artifact_labels if key[0] == mode and key[1] == stage]
            if not keys:
                continue
            lines.append('    result.' + member + '.Artifact.Stage = rhi::ShaderStage::' + stage.title() + ';')
            if stage == 'vertex':
                constant = member.upper() + '_INPUT_COUNT'
                lines += [f'    static constexpr foundation::usize {constant} = 6;',
                          f'    static constexpr foundation::StaticArray<rhi::RasterShaderInput, {constant}> {member}Inputs', '{', '        {']
                lines += ['            {' + str(i) + ', rhi::RasterVertexFormat::' + ('Float3' if i == 0 else 'Float4') + '},' for i in range(6)]
                lines += ['        },', '    };', f'    result.{member}.Inputs = {member}Inputs.AsSpan();']
            for key in sorted(keys):
                label, descriptor = artifact_labels[key]
                field = {'metal': 'Msl', 'spirv': 'Spirv', 'wgsl': 'Wgsl', 'glsl': 'GlslEs'}[key[2]]
                lines += [f'    result.{member}.Artifact.{field} = ' + (label + '.AsSpan();' if key[2] == 'spirv' else label + ';'),
                          f'    result.{member}.Artifact.{field}Entry = ' + json.dumps(descriptor['entry']) + ';']
    lines += ['    result.CompositeVertex = compositeVertex;', '    result.CompositeFragment = compositeFragment;', '    return result;', '}', '}']
    destination = Path(destination).absolute()
    destination.parent.mkdir(parents=True, exist_ok=True)
    build.atomic(destination, ('\n'.join(lines) + '\n').encode())
    return {'header': str(destination), 'stage': 'RG2', 'configuration': configuration,
            'resources': len(resources), 'passes': len(passes), 'cameras': cameras,
            'scene': next(p['scene'] for p in plan['passes'] if p['kind'] == 'Raster')}


def emit_runtime(path, destination, *, configuration='Default', name='DefaultGraph'):
    """Emit only a complete qualified plan; preserve destination on malformed input.

    See the shared numeric format and Renderer contract. No compiler/GPU calls.
    """
    try:
        return _emit_runtime(path, destination, configuration=configuration, name=name)
    except (KeyError, TypeError, ValueError, IndexError, UnicodeError, struct.error) as error:
        build.fail('Package', str(path), 'malformed execution metadata: ' + str(error))
