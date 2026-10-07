import copy
import json
from pathlib import Path
import unittest

import model

FIXTURE = Path(__file__).with_name('fixtures') / 'encounter.json'


class ModelTests(unittest.TestCase):
    def setUp(self):
        self.doc = model.parse(FIXTURE.read_text())
        self.root = self.doc['root']
        self.condition = next(n['id'] for n in self.doc['nodes'] if n['kind'] == 'If')

    def test_layout_and_record_order_do_not_change_program(self):
        before, spans = model.lower(self.doc)
        moved = model.apply(self.doc, {'kind': 'move', 'node': self.condition, 'x': 500, 'y': 220})
        moved['nodes'].reverse()
        self.assertEqual(model.executable(self.doc), model.executable(moved))
        self.assertEqual(model.lower(moved), (before, spans))
        self.assertEqual(model.diff(self.doc, moved)['semantic'], [])
        self.assertTrue(model.diff(self.doc, moved)['layout'])
        reordered = copy.deepcopy(self.doc)
        reordered['variables'].reverse()
        reordered['nodes'].reverse()
        reordered['layout']['positions'].reverse()
        self.assertEqual(model.diff(self.doc, reordered), {'semantic': [], 'layout': False})
        self.assertEqual(model.merge(self.doc, self.doc, reordered), json.loads(model.canonical(self.doc)))

    def test_rejection_preserves_draft_and_history(self):
        document = model.Document(self.doc)
        before = copy.deepcopy(document.draft)
        for command in ({'kind':'literal','node':self.condition,'value':True},
                        {'kind':'literal','node':self.condition,'value':11},
                        {'kind':'remove','node':self.root}, {'kind':'literal','node':self.condition,'value':2,'extra':0},
                        {'kind':'literal','node':[],'value':2},
                        {'kind':'order','node':self.root,'children':[{},None]}):
            with self.assertRaises(model.Invalid): document.command(command)
            self.assertEqual(document.draft, before)
            self.assertEqual(document.revision, 1)
            self.assertFalse(document.undo)
        document.command({'kind':'literal','node':self.condition,'value':3})
        document.history()
        self.assertEqual(document.draft, before)
        document.history(True)
        self.assertEqual(next(n for n in document.draft['nodes'] if n['id']==self.condition)['threshold'], 3)

    def test_cycles_missing_duplicate_and_unknown_fields(self):
        variants=[]
        for mutate in (lambda d:d['nodes'][0]['children'].append(self.root),
                       lambda d:d['nodes'][0]['children'].append('0000000000000999'),
                       lambda d:d['nodes'][1]['ports'].update(d['nodes'][0]['ports']),
                       lambda d:d.update(extra=0), lambda d:d['variables'][0].update(field=True),
                       lambda d:d['layout']['positions'][0].update(node=[])):
            doc=copy.deepcopy(self.doc);mutate(doc);variants.append(doc)
        for doc in variants:
            with self.assertRaises(model.Invalid): model.validate(doc)
        text=FIXTURE.read_text().replace('"version": 1', '"version": 1, "version": 1',1)
        with self.assertRaises(model.Invalid):model.parse(text)

    def test_paste_fresh_identities_and_execution_order(self):
        pasted=model.apply(self.doc,{'kind':'paste','parent':self.root,'node':self.condition})
        old={n['id'] for n in self.doc['nodes']}
        new={n['id'] for n in pasted['nodes']} - old
        self.assertEqual(len(new),2)
        self.assertNotEqual(model.executable(pasted),model.executable(self.doc))
        children=next(n for n in pasted['nodes'] if n['id']==self.root)['children']
        ordered=model.apply(pasted,{'kind':'order','node':self.root,'children':children[::-1]})
        self.assertTrue(any(c['field']=='execution order' for c in model.diff(pasted,ordered)['semantic']))
        with self.assertRaises(model.Invalid):model.apply(pasted,{'kind':'paste','parent':self.root,'node':self.condition})

    def test_merge_disjoint_fields_layout_deletion_and_conflicts(self):
        local=model.apply(self.doc,{'kind':'literal','node':self.condition,'value':3})
        remote=model.apply(self.doc,{'kind':'move','node':self.condition,'x':450,'y':60})
        merged=model.merge(self.doc,local,remote)
        self.assertEqual(model.executable(merged),model.executable(local))
        self.assertEqual(merged['layout'],remote['layout'])
        conflict=model.apply(self.doc,{'kind':'literal','node':self.condition,'value':4})
        with self.assertRaises(model.Invalid):model.merge(self.doc,local,conflict)
        removed=model.apply(self.doc,{'kind':'remove','node':self.condition})
        self.assertEqual(model.merge(self.doc,self.doc,removed),removed)
        with self.assertRaises(model.Invalid):model.merge(self.doc,local,removed)

    def test_repeat_command_bound_and_early_return(self):
        doc=model.apply(self.doc,{'kind':'add','parent':self.root,'type':'Repeat'})
        repeat=next(n['id'] for n in doc['nodes'] if n['kind']=='Repeat')
        doc=model.apply(doc,{'kind':'add','parent':repeat,'type':'Return'})
        source,_=model.lower(doc)
        self.assertIn('for _ = 1, 2 do',source)
        self.assertIn('do return end',source)
        with self.assertRaises(model.Invalid):model.apply(doc,{'kind':'add','parent':repeat,'type':'DoorOpen'})


if __name__=='__main__':unittest.main()
