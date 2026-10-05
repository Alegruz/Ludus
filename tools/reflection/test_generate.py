import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('reflection_generate', Path(__file__).with_name('generate.py'))
generate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(generate)
ROOT = Path(__file__).resolve().parents[2]


class GeneratorTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.data = json.loads((ROOT / 'examples/live-edit-game/body.schema.json').read_text())
        self.data['header'] = 'body.h'
        (self.root / 'body.h').write_text('struct Body {};\n')
        self.schema = self.root / 'body.schema.json'

    def load(self, data):
        self.schema.write_text(json.dumps(data))
        return generate.load(self.schema)

    def test_rejects_ambiguous_and_executable_manifests(self):
        changes = [
            ('id', 0), ('id', 45058), ('id', True), ('key', 'bounces'),
            ('member', 'Bounces'), ('member', 'Speed; dangerous()'), ('type', 'float'),
            ('default', 9), ('default', float('nan')), ('min', 2), ('max', -1),
            ('editable', 1), ('label', '\x00'), ('label', '\u00e9' * 33),
        ]
        for key, value in changes:
            with self.subTest(key=key, value=value):
                data = copy.deepcopy(self.data)
                data['fields'][0][key] = value
                with self.assertRaises(ValueError):
                    self.load(data)
        for key, value in [('version', 0), ('schema', '00000000-0000-0000-0000-000000000000'),
                           ('header', '../body.h'), ('header', '/body.h'), ('reserved', [45057]),
                           ('namespace', 'bad; injected'), ('fields', self.data['fields'] * 33)]:
            with self.subTest(key=key):
                data = copy.deepcopy(self.data)
                data[key] = value
                with self.assertRaises(ValueError):
                    self.load(data)
        self.schema.write_text('{"schema":1,"schema":2}')
        with self.assertRaises(ValueError):
            generate.load(self.schema)

    def test_released_wire_baseline_freezes_semantics_and_reserves_ids(self):
        baseline = self.load(self.data)
        changed = copy.deepcopy(baseline)
        changed['fields'][0]['label'] = 'Movement speed'
        generate.check_baseline(changed, baseline)
        for key, value in [('default', 2), ('key', 'renamed'), ('max', 9), ('type', 'float64')]:
            changed = copy.deepcopy(baseline)
            changed['fields'][0][key] = value
            with self.assertRaises(ValueError):
                generate.check_baseline(changed, baseline)
        changed = copy.deepcopy(baseline)
        changed['fields'].pop()
        with self.assertRaises(ValueError):
            generate.check_baseline(changed, baseline)
        changed['reserved'] = [45058]
        generate.check_baseline(changed, baseline)
        changed['version'] = 2
        with self.assertRaises(ValueError):
            generate.check_baseline(changed, baseline)

    def test_deterministic_source_with_compile_checked_members(self):
        data = self.load(self.data)
        header, source, json_source = generate.generate(data, 'body_schema.hpp', 'body.schema.json')
        self.assertIn('decltype(Body::Speed)', source)
        self.assertIn('static_cast<float32>(values[0].Real)', source)
        self.assertNotIn('offsetof', source)
        self.assertNotIn(str(self.root), source)
        self.assertIn('ReadBodyJson', header)
        self.assertEqual(generate.generate(data, 'body_schema.hpp', 'body.schema.json'), (header, source, json_source))
        destination = self.root / 'generated.cpp'
        generate.write_changed(destination, source)
        stamp = destination.stat().st_mtime_ns
        generate.write_changed(destination, source)
        self.assertEqual(destination.stat().st_mtime_ns, stamp)

    def test_released_float_contract_preserves_signed_zero(self):
        for kind in ('float32', 'float64'):
            data = copy.deepcopy(self.data)
            field = data['fields'][0]
            field['type'] = kind
            for key in ('default', 'min', 'max'):
                field[key] = -0.0
            baseline = self.load(data)
            for key in ('default', 'min', 'max'):
                with self.subTest(kind=kind, key=key):
                    changed = copy.deepcopy(data)
                    changed['fields'][0][key] = 0.0
                    with self.assertRaises(ValueError):
                        generate.check_baseline(self.load(changed), baseline)
            # Integer zero and positive real zero describe the same float bits.
            positive = copy.deepcopy(data)
            for key in ('default', 'min', 'max'):
                positive['fields'][0][key] = 0
            baseline = self.load(positive)
            for key in ('default', 'min', 'max'):
                positive['fields'][0][key] = 0.0
            generate.check_baseline(self.load(positive), baseline)

    @unittest.skipUnless(shutil.which('clang++-18'), 'pinned Clang 18 not available')
    def test_target_compiler_rejects_a_native_type_mismatch(self):
        data = self.load(self.data)
        header, source, _ = generate.generate(data, 'body_schema.hpp', 'body.schema.json')
        (self.root / 'body_schema.hpp').write_text(header)
        (self.root / 'body.cpp').write_text(source)
        (self.root / 'body.h').write_text('#include <ludus/foundation/base/types.h>\n'
            'namespace ludus::sample { struct Body { ludus::foundation::float64 Speed; ludus::foundation::int32 Bounces; }; }\n')
        command = ['clang++-18', '-std=c++23', '-fno-exceptions', '-fsyntax-only', str(self.root / 'body.cpp')]
        for module in ('base', 'memory', 'reflection', 'serialization'):
            command += ['-I', str(ROOT / 'modules/foundation' / module / 'include')]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('manifest/native type mismatch', result.stderr)

    def test_failure_preserves_prior_outputs_and_has_actionable_location(self):
        self.schema.write_text('{invalid')
        paths = [self.root / 'a.hpp', self.root / 'a.cpp', self.root / 'a.json', self.root / 'a_json.cpp']
        for path in paths:
            path.write_text('prior')
        result = subprocess.run([sys.executable, str(Path(__file__).with_name('generate.py')),
            '--schema', str(self.schema), '--header', str(paths[0]), '--source', str(paths[1]),
            '--manifest', str(paths[2]), '--json-source', str(paths[3])], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(str(self.schema), result.stderr)
        self.assertIn('line 1 column', result.stderr)
        self.assertTrue(all(path.read_text() == 'prior' for path in paths))


if __name__ == '__main__':
    unittest.main()
