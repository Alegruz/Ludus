"""RG2 lowering/admission and dependency regressions; native pixels qualify shaders."""
import copy
import json
from pathlib import Path
import re
import unittest

import test_render_graph_build as fixtures

ROOT = fixtures.ROOT
from ludus_tools import render_graph as graph
from ludus_tools import render_graph_build as build
from ludus_tools import render_graph_execution as execution
from ludus_tools.errors import ToolingError


class ExecutionTests(unittest.TestCase):
    run_build = fixtures.BuildTests.run_build
    value = fixtures.BuildTests.value
    edit = fixtures.BuildTests.edit

    def setUp(self):
        fixtures.BuildTests.setUp(self)
        self.graph = graph.new_workspace(self.root / 'static')
        self.manifest = build.new_build_sources(self.graph, profiles=('metal', 'wgsl'), renderer_static=True)

    def stage(self, command, cancel):
        fixtures.BuildTests.stage(self, command, cancel)
        output = Path(command[command.index('--output') + 1])
        descriptor = json.loads((output / 'stage.json').read_text())
        if descriptor['stage'] == 'vertex' and self.value()['id'] == 'RendererStaticBuild':
            descriptor['interface']['inputs'] = [
                {'location': i, 'format': 'Float3' if i == 0 else 'Float4', 'name': name}
                for i, name in enumerate(('PositionMesh', 'MeshToClipRow0', 'MeshToClipRow1',
                                         'MeshToClipRow2', 'MeshToClipRow3', 'Color'))]
        (output / 'stage.json').write_bytes(build.encoded(descriptor))

    def package(self):
        return build.verify_package(Path(self.run_build()['package']))

    def test_numeric_plan_and_static_artifacts_do_not_synthesize_programs(self):
        report = self.run_build()
        destination = self.root / 'graph.h'
        result = execution.emit_runtime(self.output, destination, name='RasterGraph')
        self.assertEqual(result['passes'], 1)
        text = destination.read_text()
        self.assertIn('format::Technique::ColorAndDepth', text)
        self.assertNotIn('result.DepthVertex', text)
        self.assertEqual(build.verify_package(Path(report['package']))['counts']['artifacts'], 6)
        self.assertIn('result.ColorVertex.Artifact.Msl', text)
        self.assertIn('result.ColorVertex.Artifact.Wgsl', text)
        self.assertIn('result.CompositeVertex = compositeVertex', text)
        self.assertNotIn('std::', text)

    def test_depth_only_export_has_explicit_technique_and_private_backing(self):
        self.graph = graph.new_workspace(self.root / 'depth', output='depth')
        self.manifest = build.new_build_sources(self.graph, profiles=('metal', 'wgsl'), renderer_static=True)
        package = self.package()
        _, resources, passes, _ = execution.lower_plan(package, 'Default')
        self.assertEqual(resources[0]['Kind'], 'Depth')
        self.assertEqual(resources[0]['Backing'], 0)
        self.assertEqual(resources[0]['IsExported'], 1)
        self.assertEqual(passes[0]['Program'], 'DepthOnly')
        self.assertEqual(passes[0]['Color'], execution.ABSENT)
        self.assertEqual(passes[0]['ColorStore'], 'Discard')

    def test_admission_rejects_unknown_state_import_history_format_and_samples(self):
        package = self.package()
        for field, value in [('cull', 'Back'), ('depth_write', False), ('blend', 'Premultiplied')]:
            changed = copy.deepcopy(package)
            for recipe in changed['recipes']:
                recipe['state'][field] = value
            with self.subTest(field=field), self.assertRaisesRegex(ToolingError, 'admitted static Renderer state'):
                execution.lower_plan(changed, 'Default')
        for field, value in [('initialized', True), ('samples', 4), ('format', 'RGBA16Float')]:
            changed = copy.deepcopy(package)
            resource = next(iter(changed['plans'][0]['resources'].values()))
            resource[field] = value
            with self.subTest(field=field), self.assertRaisesRegex(ToolingError, 'unqualified'):
                execution.lower_plan(changed, 'Default')

    def test_distinct_logical_depths_cannot_alias_one_color_owner(self):
        package = self.package()
        plan = package['plans'][0]
        depth = next(r for r, value in plan['resources'].items() if value['type'] == 'DepthTexture')
        plan['resources']['SecondDepth'] = copy.deepcopy(plan['resources'][depth])
        operation = copy.deepcopy(plan['passes'][0])
        operation['attachments']['depth']['resource'] = 'SecondDepth'
        plan['passes'].insert(1, operation)
        with self.assertRaisesRegex(ToolingError, 'distinct logical images'):
            execution.lower_plan(package, 'Default')

    def test_integrity_failure_preserves_generated_header(self):
        self.run_build()
        destination = self.root / 'graph.h'
        execution.emit_runtime(self.output, destination)
        original = destination.read_bytes()
        (self.output / 'current.json').write_text('{"version":1,"generation":"../bad"}')
        with self.assertRaisesRegex(ToolingError, 'pointer'):
            execution.emit_runtime(self.output, destination)
        self.assertEqual(destination.read_bytes(), original)

    def test_generic_rg1_interface_requires_explicit_runtime_profile(self):
        self.graph = graph.new_workspace(self.root / 'generic')
        self.manifest = build.new_build_sources(self.graph, profiles=('metal', 'wgsl'))
        self.run_build()
        with self.assertRaisesRegex(ToolingError, 'renderer-static'):
            execution.emit_runtime(self.output, self.root / 'generic.h')

    def test_neutral_format_limits_match_offline_lowering(self):
        header = (ROOT / 'modules/content/render_graph_format/include/ludus/content/render_graph_format/plan.h').read_text()
        for name in ('MAX_PASSES', 'MAX_RESOURCES', 'MAX_CAMERAS', 'VERSION'):
            self.assertEqual(int(re.search(r'\b' + name + r' = (\d+);', header).group(1)),
                             getattr(execution, 'FORMAT_VERSION' if name == 'VERSION' else name))

    def test_nested_source_graph_preserves_package_relative_include_paths(self):
        nested = self.root / 'nested sources'
        self.graph = graph.new_workspace(nested / 'graphs')
        document = json.loads(self.graph.read_text())
        document['configurations'][0]['nodes'][4]['asset'] = 'graphs/rasterize.lurender.json'
        self.graph.write_text(json.dumps(document))
        self.manifest = build.new_build_sources(self.graph, profiles=('metal', 'wgsl'), root=nested)
        self.assertTrue(self.value()['programs'][0]['source'].startswith('graphs/main_shaders/'))
        report = self.run_build(root=nested)
        self.assertEqual(build.verify_package(Path(report['package']))['source_map']['build_manifest'],
                         'graphs/main.lushader.json')

    def test_packer_concatenation_collision_rejects_instead_of_overwriting(self):
        schema = {'S': {'fields': []}}
        layout = {'schema': 'S', 'size': 4, 'fields': [], 'binding': 'B_metal_C'}
        other = dict(layout, binding='C')
        with self.assertRaisesRegex(ToolingError, 'packer name collision'):
            build.cpp_header(schema, {('A', 'metal'): {'layouts': [layout]},
                                     ('A_metal_B', 'metal'): {'layouts': [other]}})

    def test_depth_wrapper_declares_void_contract_before_author_code(self):
        # Inspect a complete emitted package's readable wrapper, not a synthetic regex on author code.
        package = Path(self.run_build()['package'])
        document = build.verify_package(package)
        depth = next(p for p in document['programs'] if p['id'] == 'Depth')
        wrapper = (package / 'programs' / depth['key'] / 'wrapper.slang').read_text()
        self.assertLess(wrapper.index('void DepthMain('), wrapper.index('#include "'))


if __name__ == '__main__':
    unittest.main()
