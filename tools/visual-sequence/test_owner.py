"""Ownership, file preservation and preview identity tests need the real S3 binary."""
import copy
import os
from pathlib import Path
import tempfile
import unittest

import model
from server import Owner

ROOT=Path(__file__).resolve().parents[2]
EXE=Path(os.environ.get('LUDUS_S3_TEST_EXECUTABLE',ROOT/'out/build/linux-clang-development/tools/visual-sequence/ludus_visual_sequence'))


@unittest.skipUnless(EXE.is_file(),'build the opt-in S3 bridge for owner integration tests')
class OwnerTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.path=Path(self.temp.name)/'encounter.json'
        self.path.write_text(Path(__file__).with_name('fixtures').joinpath('encounter.json').read_text())
        self.owner=Owner(self.path,EXE,Path(self.temp.name)/'cache')
        self.condition=next(n['id'] for n in self.owner.document.draft['nodes'] if n['kind']=='If')

    def tearDown(self):
        self.owner.close()
        self.temp.cleanup()

    def action(self,action,**extra):
        request={'action':action,'revision':self.owner.document.revision,**extra}
        if action in ('cook','interact','breakpoint','continue','restart'):
            request['cursor']=copy.deepcopy(self.owner.cursor())
        return self.owner.act(request)

    def test_atomic_save_external_edit_and_symlink_rejection(self):
        self.action('command',command={'kind':'literal','node':self.condition,'value':3})
        os.chmod(self.path,0o640)
        self.action('save')
        self.assertEqual(self.path.stat().st_mode & 0o777,0o640)
        self.assertEqual(model.parse(self.path.read_text()),self.owner.document.draft)
        self.action('command',command={'kind':'literal','node':self.condition,'value':4})
        self.path.write_text(self.path.read_text()+' ')
        before=self.path.read_text()
        with self.assertRaises(model.Invalid):self.action('save')
        self.assertEqual(self.path.read_text(),before)
        target=self.path.with_name('target.json');self.path.rename(target);self.path.symlink_to(target)
        with self.assertRaises(OSError):self.action('save')
        self.assertEqual(target.read_text(),before)

    def test_layout_no_reload_and_stale_preview_or_draft(self):
        self.action('cook')
        cursor=copy.deepcopy(self.owner.cursor())
        old_key=self.owner.preview['runtime']['package']
        self.action('command',command={'kind':'move','node':self.condition,'x':410,'y':110})
        self.action('cook')
        self.assertEqual(self.owner.cursor(),cursor)
        self.assertEqual(self.owner.preview['runtime']['package'],old_key)
        self.action('command',command={'kind':'literal','node':self.condition,'value':3})
        self.action('cook')
        with self.assertRaises(model.Invalid):
            self.owner.act({'action':'interact','revision':self.owner.document.revision,'cursor':cursor,'instance':0,'amount':1})
        with self.assertRaises(model.Invalid):self.owner.act({'action':'undo','revision':1})
        before=copy.deepcopy(self.owner.cursor())
        self.action('restart');self.action('cook')
        self.assertNotEqual(self.owner.cursor()['epoch'],before['epoch'])
        with self.assertRaises(model.Invalid):
            self.owner.act({'action':'interact','revision':self.owner.document.revision,'cursor':before,'instance':0,'amount':1})

    def test_external_branch_merge_updates_disk_base_without_losing_draft(self):
        base=copy.deepcopy(self.owner.document.saved)
        self.action('command',command={'kind':'literal','node':self.condition,'value':3})
        remote=model.apply(base,{'kind':'move','node':self.condition,'x':400,'y':75})
        self.path.write_text(model.canonical(remote))
        self.action('merge',remote=remote)
        self.action('save')
        saved=model.parse(self.path.read_text())
        self.assertEqual(saved['layout'],remote['layout'])
        self.assertEqual(next(n for n in saved['nodes'] if n['id']==self.condition)['threshold'],3)

    def test_paused_edits_not_saved_play_state_and_reset_retirement(self):
        self.action('cook')
        self.action('breakpoint',node=self.condition,enabled=True)
        self.action('interact',instance=1,amount=1)
        self.assertTrue(self.owner.preview['runtime']['partial'])
        self.action('command',command={'kind':'literal','node':self.condition,'value':3})
        before=self.owner.preview['runtime']['package']
        with self.assertRaises(model.Invalid):self.action('cook')
        self.assertEqual(self.owner.preview['runtime']['package'],before)
        self.action('save')
        self.assertNotIn('open_requested',self.path.read_text())
        self.action('restart')
        self.assertIsNone(self.owner.preview)
        self.action('cook')
        self.assertEqual(self.owner.preview['runtime']['states'][0]['interactions'],0)


if __name__=='__main__':unittest.main()
