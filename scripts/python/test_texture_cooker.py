"""Independent color, footprint, normal and bounded coverage mip regressions."""
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'cmake/shaders'))
from cook_texture import cook
class TextureCookerTests(unittest.TestCase):
    def test_srgb_average_is_linear_light_and_alpha_is_linear(self):
        source={'width':2,'height':2,'format':'Rgba8Srgb','semantic':'color','pixels':[255,255,255,255,0,0,0,255]*2}
        blob,levels=cook(source)
        self.assertEqual(levels[1]['offset'],16)
        self.assertEqual(blob[16:20],bytes([188,188,188,255]))
        source['format']='Rgba8Unorm'
        self.assertEqual(cook(source)[0][16:20],bytes([128,128,128,255]))
    def test_odd_edges_and_rg_padding_are_included(self):
        source={'width':3,'height':1,'format':'Rg8Unorm','semantic':'data','pixels':[0,0,0,0,255,255]}
        blob,levels=cook(source)
        self.assertEqual(blob,bytes([0,0,0,0,255,255,0,0,85,85,0,0]))
        self.assertEqual([(x['width'],x['height'],x['row_pitch']) for x in levels],[(3,1,8),(1,1,4)])
    def test_normals_are_renormalized_and_opposites_use_declared_axis(self):
        source={'width':2,'height':1,'format':'Rgba8Unorm','semantic':'normal','pixels':[255,128,128,255,128,255,128,255]}
        blob,levels=cook(source)
        self.assertTrue(216<=blob[8]<=219)
        self.assertTrue(216<=blob[9]<=219)
        self.assertEqual(blob[11],255)
        source['pixels']=[255,255,255,255,0,0,0,255]
        self.assertEqual(cook(source)[0][8:12],bytes([128,128,255,255]))
    def test_mask_coverage_uses_nearest_attainable_discrete_count(self):
        source={'width':4,'height':1,'format':'R8Unorm','semantic':'mask','cutoff':.5,'pixels':[255,255,255,0]}
        blob,levels=cook(source)
        self.assertEqual(levels[0]['next_coverage_error'],.25)
        self.assertEqual(len(blob),12)
        self.assertGreaterEqual(blob[8],128)
    def test_invalid_input_and_semantics_reject(self):
        source={'width':1,'height':1,'format':'Rgba8Srgb','semantic':'data','pixels':[1,2,3,4]}
        with self.assertRaises(ValueError):cook(source)
        source.update(format='R8Unorm',semantic='mask',pixels=[1],cutoff=0)
        with self.assertRaises(ValueError):cook(source)
        source.update(cutoff=.5,width=True)
        with self.assertRaises(ValueError):cook(source)

    def test_odd_resampling_preserves_area_and_straight_alpha_avoids_bleeding(self):
        source={'width':5,'height':1,'format':'R8Unorm','semantic':'data','pixels':[0,0,0,0,255]}
        blob,levels=cook(source)
        self.assertEqual(blob[levels[-1]['offset']],51)
        source={'width':2,'height':1,'format':'Rgba8Srgb','semantic':'color','pixels':[255,0,0,255,0,0,255,0]}
        self.assertEqual(cook(source)[0][8:12],bytes([255,0,0,128]))

    def test_complete_chain_budget_rejects_before_pixel_processing(self):
        with self.assertRaisesRegex(ValueError,'64 MiB'):
            cook({'width':4096,'height':4096,'format':'Rgba8Unorm','semantic':'color','pixels':[]})
