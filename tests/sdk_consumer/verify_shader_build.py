#!/usr/bin/env python3
"""Exercise the installed shader helper's real compiler/dependency contract."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def run(command, success=True):
    result = subprocess.run(list(map(str, command)), text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if (result.returncode == 0) != success:
        raise RuntimeError(result.stdout)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('sdk', 'cmake', 'ninja', 'cxx', 'slang', 'validator', 'conan'):
        parser.add_argument('--' + name, required=True)
    args = parser.parse_args()
    sdk = Path(args.sdk).resolve()
    with tempfile.TemporaryDirectory(prefix='ludus shader build-') as temporary:
        root = Path(temporary); source = root / 'source'; source.mkdir()
        for path in Path(__file__).with_name('shaders').iterdir():
            shutil.copy2(path, source / path.name)
        (source / 'main.cpp').write_text('#include "sample.h"\nint main() { return ludus::shaders::sample::Fragment().UniformSize == 48 ? 0 : 1; }\n')
        (source / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.29)
project(ShaderBuildContract LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Ludus CONFIG REQUIRED)
add_executable(sample main.cpp)
target_link_libraries(sample PRIVATE Ludus::GraphicsRhi)
ludus_compile_shader(TARGET sample NAME sample SOURCE diagnostic.slang VERTEX vertexMain FRAGMENT fragmentMain DEFINES ${SHADER_DEFINES})
''')
        build = root / 'build'
        command = [args.cmake, '-S', source, '-B', build, '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=RelWithDebInfo', '-DCMAKE_MAKE_PROGRAM=' + args.ninja, '-DCMAKE_CXX_COMPILER=' + args.cxx,
            '-DCMAKE_PREFIX_PATH=' + str(sdk) + ';' + args.conan,
            '-DLUDUS_SLANG_COMPILER=' + args.slang, '-DLUDUS_SPIRV_VALIDATOR=' + args.validator]
        run(command); run([args.cmake, '--build', build]); run([build / 'sample'])
        header = build / 'ludus-shaders/sample/sample/sample.h'
        shader = header.with_name('sample.fragment.spv')
        stamp = header.stat().st_mtime_ns
        run([args.cmake, '--build', build]); assert header.stat().st_mtime_ns == stamp, 'no-op rebuild changed shader'
        for name in ('uniforms.slang', 'diagnostic.slang'):
            with (source / name).open('a') as file: file.write('\n// dependency check\n')
            run([args.cmake, '--build', build]); new_stamp = header.stat().st_mtime_ns
            assert new_stamp != stamp, name + ' did not trigger shader rebuild'; stamp = new_stamp
        before = hashlib.sha256(shader.read_bytes()).hexdigest()
        run(command + ['-DSHADER_DEFINES=DIAGNOSTIC_SCALE=0.5']); run([args.cmake, '--build', build])
        assert hashlib.sha256(shader.read_bytes()).hexdigest() != before, 'option did not alter compiled artifact'
        manifest = json.loads(header.with_name('manifest.json').read_text())
        assert manifest['uniform_sizes'] == {'vertex':48,'fragment':48,'wgsl':48}
        assert manifest['spirv_entries'] == {'vertex':'main','fragment':'main'}
        assert manifest['wgsl_entries'] == {'vertex':'vertexMain','fragment':'fragmentMain'}
        contract = [('resolution', 0, 8), ('elapsedTime', 8, 4), ('direction', 16, 12), ('tint', 32, 16)]
        for target in ('vertex', 'fragment', 'wgsl'):
            resource = json.loads(header.with_name(target + '.reflection.json').read_text())['parameters'][0]
            fields = resource['type']['elementType']['fields']
            assert [(field['name'], field['binding']['offset'], field['binding']['size']) for field in fields] == contract
        # Derive WGSL layout separately from emitted types/alignments, without
        # borrowing the SPIR-V offsets or trusting reflection packing equality.
        code = header.with_name('sample.wgsl').read_text()
        body = re.search(r'struct DiagnosticUniforms[^\{]*\{([^}]+)\}', code).group(1)
        members = re.findall(r'@align\((\d+)\)\s+(\w+)\s*:\s*(vec[234]<f32>|f32)', body)
        occupied = 0; alignment = 1; offsets = []
        for declared, name, kind in members:
            size, natural = {'f32':(4,4), 'vec2<f32>':(8,8), 'vec3<f32>':(12,16), 'vec4<f32>':(16,16)}[kind]
            align = max(int(declared), natural); alignment = max(alignment, align)
            occupied = (occupied + align - 1) // align * align
            offsets.append((re.sub(r'_\d+$', '', name), occupied, size)); occupied += size
        assert offsets == contract
        assert alignment == 16 and (occupied + alignment - 1) // alignment * alignment == 48
        original = (source / 'diagnostic.slang').read_text()
        (source / 'diagnostic.slang').write_text(original.replace('vk::binding(0, 0)', 'vk::binding(0, 1)'))
        failure = run([args.cmake, '--build', build], success=False)
        assert 'binding contract' in failure or 'group 0 binding 0' in failure, failure
        (source / 'diagnostic.slang').write_text(original)
        bad_validator = root / 'bad-validator'; shutil.copy2(args.validator, bad_validator)
        with bad_validator.open('ab') as file: file.write(b'changed')
        run(command + ['-DLUDUS_SPIRV_VALIDATOR=' + str(bad_validator)])
        failure = run([args.cmake, '--build', build], success=False)
        assert 'validator differs' in failure
    print('Installed shader helper: no-op, source/include/option rebuild, reflected contracts, binding and integrity rejection passed')

if __name__ == '__main__': main()
