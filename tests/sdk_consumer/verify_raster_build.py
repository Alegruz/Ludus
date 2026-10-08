"""Verify the installed portable raster helper and public-only native consumer."""
import json
from pathlib import Path
import shutil
import tempfile


def verify_raster(args, run, metal=False):
    with tempfile.TemporaryDirectory(prefix='ludus raster sdk-') as temporary:
        source = Path(temporary)/'source'; source.mkdir()
        shutil.copy2(Path(__file__).with_name('raster.cpp'),source/'raster.cpp')
        shutil.copy2(Path(__file__).with_name('ordered_frame.h'),source/'ordered_frame.h')
        shutil.copy2(Path(__file__).with_name('shaders')/'raster.slang',source/'raster.slang')
        shutil.copy2(Path(__file__).with_name('shaders')/'cull.slang',source/'cull.slang')
        (source/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.29)
project(RasterSdkContract LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Ludus CONFIG REQUIRED COMPONENTS GraphicsRhi)
add_executable(raster_consumer raster.cpp)
target_link_libraries(raster_consumer PRIVATE Ludus::GraphicsRhi)
target_compile_options(raster_consumer PRIVATE -Wall -Wextra -Wpedantic -Werror -fno-exceptions)
ludus_compile_shader(TARGET raster_consumer NAME cull SOURCE cull.slang COMPUTE computeMain)
ludus_compile_shader(TARGET raster_consumer NAME raster RASTER SOURCE raster.slang VERTEX vertexMain FRAGMENT fragmentMain)
''')
        build=Path(temporary)/'build'
        command=[args.cmake,'-S',source,'-B',build,'-G','Ninja',
                 '-DCMAKE_MAKE_PROGRAM='+args.ninja,'-DCMAKE_CXX_COMPILER='+args.cxx,
                 '-DCMAKE_BUILD_TYPE=RelWithDebInfo','-DCMAKE_PREFIX_PATH='+str(Path(args.sdk).resolve())+';'+args.conan,
                 '-DLUDUS_SLANG_COMPILER='+args.slang]
        if metal:
            command += ['-DCMAKE_OSX_SYSROOT='+args.sysroot,'-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0',
                        '-DCMAKE_CXX_FLAGS=-stdlib=libc++ -nostdinc++ -isystem "'+args.libcxx+'"']
        else:
            command += ['-DLUDUS_SPIRV_VALIDATOR='+args.validator]
            if args.spirv_cross: command += ['-DLUDUS_SPIRV_CROSS='+args.spirv_cross]
        run(command);run([args.cmake,'--build',build]);run([build/'raster_consumer'])
        output=build/'ludus-shaders/raster_consumer/raster'
        manifest=json.loads((output/'manifest.json').read_text())
        targets=('metal',) if metal else ('spirv','wgsl') + (('glsl',) if args.spirv_cross else ())
        for target in targets:
            assert manifest['interfaces'][target+'_vertex']['entries']==[]
            assert [(x['location'],x['format']) for x in manifest['interfaces'][target+'_vertex']['inputs']]==[(0,'Float3'),(1,'Float2'),(2,'Float2')]
            assert [(x['binding'],x['kind'],x['size']) for x in manifest['interfaces'][target+'_fragment']['entries']]==[(0,'UniformBuffer',16),(1,'Texture2D',0),(2,'Sampler',0)]
        compute_output=build/'ludus-shaders/raster_consumer/cull'
        compute_manifest=json.loads((compute_output/'manifest.json').read_text())
        for target in (('metal',) if metal else ('spirv','wgsl')):
            interface=compute_manifest['interfaces'][target+'_compute']
            assert interface['workgroup']==[1,1,1]
            assert [(x['binding'],x['kind'],x['size']) for x in interface['entries']]==[(0,'StorageRead',8),(1,'StorageReadWrite',8),(2,'StorageReadWrite',4)]
        compute_header=compute_output/'cull.h';compute_stamp=compute_header.stat().st_mtime_ns
        header=output/'raster.h';stamp=header.stat().st_mtime_ns
        run([args.cmake,'--build',build]);assert header.stat().st_mtime_ns==stamp
        assert compute_header.stat().st_mtime_ns==compute_stamp
        with (source/'cull.slang').open('a') as file:file.write('\n// compute dependency regression\n')
        run([args.cmake,'--build',build]);assert compute_header.stat().st_mtime_ns!=compute_stamp
        compute_original=(source/'cull.slang').read_text()
        (source/'cull.slang').write_text(compute_original.replace('[numthreads(1, 1, 1)]','[numthreads(257, 1, 1)]'))
        failure=run([args.cmake,'--build',build],success=False)
        assert 'workgroup' in failure or 'thread group' in failure, failure
        (source/'cull.slang').write_text(compute_original)
        with (source/'raster.slang').open('a') as file:file.write('\n// raster dependency regression\n')
        run([args.cmake,'--build',build]);assert header.stat().st_mtime_ns!=stamp
        original=(source/'raster.slang').read_text()
        (source/'raster.slang').write_text(original.replace('vk::binding(1, 0)','vk::binding(1, 1)').replace('register(t1)','register(t9)'))
        failure=run([args.cmake,'--build',build],success=False)
        assert 'group/set zero' in failure or 'bounded profile' in failure or 'remapping' in failure, failure
    print('Installed portable raster: reflected stages, native draws, retained snapshots, rebuild and malformed binding rejection passed')
