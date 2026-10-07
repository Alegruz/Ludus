import copy
import json
from pathlib import Path
import shutil
import sys
from unittest.mock import patch
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts/python'))
from ludus_tools import behavior_cook as cook
FIXTURE=Path(__file__).with_name('fixtures')
class CookTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.root=Path(self.temp.name)
        for path in FIXTURE.iterdir():shutil.copy(path,self.root/path.name)
    def tearDown(self):self.temp.cleanup()
    def test_closed_schema_and_numeric_boundaries(self):
        source=json.loads((self.root/'contract.json').read_text())
        for change in (lambda x:x.update(extra=1),lambda x:x['state'][0].update(min=True),
                       lambda x:x['operations'][0].update(capability=0),lambda x:x['state'][0].update(name='class'),
                       lambda x:x['operations'][0]['arguments'].append(x['operations'][0]['arguments'][0])):
            data=copy.deepcopy(source);change(data)
            (self.root/'contract.json').write_text(json.dumps(data))
            with self.assertRaises(ValueError):cook.contract(self.root/'contract.json')
    def test_imports_ignore_comments_and_require_literal_catalogs(self):
        with self.assertRaisesRegex(ValueError,'backtick'):cook.imports('local text = `require(id)`')
        self.assertEqual(cook.imports('-- require("0000000000000010")\nlocal x="require(123)"'),[])
        with self.assertRaises(ValueError):cook.imports('local id="0000000000000010"; require(id)')
        self.assertEqual(cook.imports('local x=require("0000000000000010")')[0][0],'0000000000000010')
    def test_source_escape_missing_import_and_cycles_are_rejected(self):
        source=json.loads((self.root/'package.json').read_text())
        source['programs'][0]['source']='../outside.luau'
        (self.root/'package.json').write_text(json.dumps(source))
        with self.assertRaises(ValueError):cook.package(self.root/'package.json')
        source=json.loads((FIXTURE/'package.json').read_text())
        source['programs'][0]['imports']=['0000000000000999']
        (self.root/'door.luau').write_text('--!strict\nlocal dependency=require("0000000000000999")\nreturn function() end\n')
        (self.root/'package.json').write_text(json.dumps(source))
        with self.assertRaises(ValueError):cook.package(self.root/'package.json')
        source=json.loads((FIXTURE/'package.json').read_text())
        source['programs']=[dict(asset='0000000000000010',revision=1,source='cycle_a.luau',entrypoint=False,imports=['0000000000000020']),dict(asset='0000000000000020',revision=1,source='cycle_b.luau',entrypoint=False,imports=['0000000000000010']),dict(asset='0000000000000030',revision=1,source='entry.luau',entrypoint=True,imports=[])]
        for filename,asset in [('cycle_a.luau','0000000000000020'),('cycle_b.luau','0000000000000010')]:
            (self.root/filename).write_text('--!strict\nreturn require("'+asset+'")\n')
        (self.root/'entry.luau').write_text('--!strict\nreturn function() end\n')
        (self.root/'package.json').write_text(json.dumps(source))
        with self.assertRaisesRegex(ValueError,'cycle'):cook.package(self.root/'package.json')
    def test_generator_has_multiple_native_wrappers_and_no_vm_headers(self):
        schema=cook.contract(FIXTURE/'contract.json')
        digest,header,definitions=cook.generate(schema)
        self.assertEqual(len(digest),64)
        self.assertIn('transaction.Request(200',header);self.assertIn('transaction.Request(201',header)
        self.assertNotIn('lua.h',header);self.assertNotIn('lua_State',header)
        self.assertIn('RequestSignal: (boolean)',definitions)
    def test_cook_cache_dependency_invalidation_and_failed_publication(self):
        compiler=ROOT/'out/luau-probe/host-compiler/luau-compile'
        analyzer=compiler.with_name('luau-analyze')
        if not compiler.is_file() or not analyzer.is_file():self.skipTest('explicit paired tool preparation required')
        profile={'version':1,'profile':cook.sha((ROOT/'config/luau_toolchain.json').read_bytes()),'compiler':cook.sha(compiler.read_bytes()),'analyzer':cook.sha(analyzer.read_bytes())}
        profile_path=self.root/'profile.json';profile_path.write_text(json.dumps(profile))
        output=self.root/'cooked'
        args=(self.root/'contract.json',self.root/'package.json',output,profile_path,compiler,analyzer)
        first=cook.cook(*args)
        with patch.object(cook.subprocess,'run',side_effect=AssertionError('cache hit invoked tools')):
            self.assertEqual(cook.cook(*args),first)
        evidence=cook.read_json(first/'evidence.json')
        helper=self.root/'helper.luau';helper.write_text(helper.read_text().replace('Offset = 0','Offset = 1'))
        second=cook.cook(*args)
        self.assertNotEqual(first,second)
        self.assertNotEqual(evidence['programs'],cook.read_json(second/'evidence.json')['programs'])
        before={name:(output/name).read_bytes() for name in ('current.json','contract.h','package.h')}
        script=self.root/'door.luau';script.write_text(script.read_text().replace('event.Amount','"wrong"'))
        with self.assertRaises(subprocess.CalledProcessError):cook.cook(*args)
        self.assertEqual(before,{name:(output/name).read_bytes() for name in before})
        profile['compiler']='0'*64;profile_path.write_text(json.dumps(profile))
        with self.assertRaisesRegex(ValueError,'paired host tools'):cook.cook(*args)
        self.assertEqual(before,{name:(output/name).read_bytes() for name in before})
    def test_cli_reports_controlled_manifest_errors(self):
        program=ROOT/'scripts/ludus'
        result=subprocess.run([str(program),'scripts','cook','--contract',str(self.root/'contract.json'),'--package',str(self.root/'package.json'),'--output',str(self.root/'cooked'),'--profile',str(self.root/'absent.json'),'--compiler',str(self.root/'absent-compiler'),'--analyzer',str(self.root/'absent-analyzer')],capture_output=True,text=True)
        self.assertNotEqual(result.returncode,0)
        self.assertNotIn('Traceback',result.stderr)
if __name__=='__main__':unittest.main()
