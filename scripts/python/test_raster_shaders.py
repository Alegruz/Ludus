"""Regressions for per-target portable raster reflection and browser packing."""
import copy
import importlib.util
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'cmake/shaders'))
from compile_raster import raster_interface, wgsl_contract, glsl_interface, glsl_varyings

class RasterReflectionTests(unittest.TestCase):
    def fixture(self):
        scalar={'kind':'scalar','scalarType':'float32'}
        return {'parameters':[{'name':'settings','binding':{'kind':'descriptorTableSlot','index':0},
                  'type':{'kind':'constantBuffer','elementType':{'fields':[{'name':'color','binding':{'offset':0}}]},
                          'elementVarLayout':{'binding':{'size':16}}}}],
                'entryPoints':[{'stage':'fragment','bindings':[{'name':'settings','binding':{'used':1}}]}]}
    def test_missing_stage_usage_is_rejected(self):
        data=self.fixture();data['entryPoints'][0]['bindings'][0]['binding']={}
        with self.assertRaisesRegex(RuntimeError,'compile stages separately'):raster_interface(data,'fragment')
    def test_stage_usage_and_descriptor_identity(self):
        data=self.fixture();self.assertEqual(raster_interface(data,'fragment')['entries'][0]['offsets'],{'color':0})
        for change in ({'space':1},{'index':8}):
            invalid=copy.deepcopy(data);invalid['parameters'][0]['binding'].update(change)
            with self.assertRaises(RuntimeError):raster_interface(invalid,'fragment')
        data['entryPoints'][0]['bindings'][0]['binding']['used']=0
        self.assertEqual(raster_interface(data,'fragment')['entries'],[])
    def test_equal_total_size_does_not_hide_wrong_member_offsets(self):
        value=raster_interface(self.fixture(),'fragment')
        code='@binding(0) @group(0) var<uniform> settings_0 : Tint;\nstruct Tint { @align(16) color_0 : vec4<f32>, };'
        self.assertEqual(wgsl_contract(code,value,'fragment'),code)
        wrong=copy.deepcopy(value);wrong['entries'][0]['offsets']['color']=4
        with self.assertRaisesRegex(RuntimeError,'packing differs'):wgsl_contract(code,wrong,'fragment')
        essl='#version 300 es\nlayout(std140) uniform Tint { highp vec4 color; } settings;\nvoid main() {}'
        reflection={'ubos':[{'binding':0,'name':'Tint'}]}
        self.assertEqual(glsl_interface(essl,value,reflection)[0],{0:'Tint'})
        with self.assertRaisesRegex(RuntimeError,'offsets differ'):glsl_interface(essl,wrong,reflection)
    def test_location_normalization_matches_named_input_and_leaves_outputs(self):
        value={'entries':[],'inputs':[{'name':'position','location':0,'format':'Float3'}, {'name':'uv','location':1,'format':'Float2'}]}
        code='struct Input { @location(1) position_0 : vec3<f32>, @location(0) uv_0 : vec2<f32>, };\nstruct Output { @location(3) uv_1 : vec2<f32>, };\n@vertex fn vertexMain(input : Input) -> Output {}'
        normalized=wgsl_contract(code,value,'vertex')
        self.assertIn('@location(0) position_0',normalized);self.assertIn('@location(1) uv_0',normalized)
        self.assertIn('@location(3) uv_1',normalized)
        with self.assertRaisesRegex(RuntimeError,'differs from reflection'):wgsl_contract(code.replace('uv_0','unknown_0'),value,'vertex')

    def test_essl_stage_names_are_linked_by_verified_location(self):
        vertex='out vec2 vertex_uv; void main() { vertex_uv=vec2(0); }'
        fragment='in vec2 input_uv; void main() { vec2 value=input_uv; }'
        a,iface_a=glsl_varyings(vertex,{'outputs':[{'name':'vertex.uv','type':'vec2','location':0}]},'vertex')
        b,iface_b=glsl_varyings(fragment,{'inputs':[{'name':'input.uv','type':'vec2','location':0}]},'fragment')
        self.assertEqual(iface_a,iface_b);self.assertIn('out vec2 ludusVarying0;',a);self.assertIn('in vec2 ludusVarying0;',b)
        with self.assertRaisesRegex(RuntimeError,'differs from location reflection'):
            glsl_varyings(vertex,{'outputs':[{'name':'absent','type':'vec2','location':0}]},'vertex')

    def compute_fixture(self):
        return {'parameters':[{'name':'records','binding':{'kind':'descriptorTableSlot','index':0},
                  'type':{'kind':'resource','baseShape':'structuredBuffer','access':'readWrite',
                          'resultType':{'kind':'vector','elementCount':2,
                                        'elementType':{'kind':'scalar','scalarType':'float32'}}}}],
                'entryPoints':[{'stage':'compute','threadGroupSize':[64,1,1],
                                'bindings':[{'name':'records','binding':{'used':1}}]}]}

    def test_compute_storage_access_stride_and_workgroup(self):
        data=self.compute_fixture()
        value=raster_interface(data,'compute')
        self.assertEqual(value['workgroup'],[64,1,1])
        self.assertEqual(value['entries'][0]['kind'],'StorageReadWrite')
        self.assertEqual(value['entries'][0]['size'],8)
        data['parameters'][0]['type']['access']='read'
        self.assertEqual(raster_interface(data,'compute')['entries'][0]['kind'],'StorageRead')
        for group in ([0,1,1],[257,1,1],[16,17,1],[1,1,65],[1,1],None):
            invalid=copy.deepcopy(data);invalid['entryPoints'][0]['threadGroupSize']=group
            with self.assertRaisesRegex(RuntimeError,'workgroup'):raster_interface(invalid,'compute')

    def test_compute_rejects_unportable_elements_and_extra_storage(self):
        data=self.compute_fixture()
        for change in ({'elementCount':3},{'elementType':{'kind':'scalar','scalarType':'float64'}},
                       {'kind':'struct','fields':[]}):
            invalid=copy.deepcopy(data);invalid['parameters'][0]['type']['resultType'].update(change)
            with self.assertRaisesRegex(RuntimeError,'scalar32'):raster_interface(invalid,'compute')
        for i in range(1,5):
            resource=copy.deepcopy(data['parameters'][0]);resource['name']=f'records{i}';resource['binding']['index']=i
            data['parameters'].append(resource)
            data['entryPoints'][0]['bindings'].append({'name':resource['name'],'binding':{'used':1}})
        with self.assertRaisesRegex(RuntimeError,'four storage'):raster_interface(data,'compute')

if __name__=='__main__':unittest.main()
