"""Verify a copied independent project with a relocated, opt-in installed SDK.

Owned temporary paths/presets live under out/. No project loading path changes,
implicit downloads or private engine includes are involved.
"""
import argparse
import json
import platform
import shlex
from pathlib import Path
import shutil
import subprocess
import tempfile
import sys
import zipfile
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/"scripts/python"))
from ludus_tools.package_native import validate_native
from ludus_tools.package_verify import inventory

ROOT=Path(__file__).resolve().parents[2]

def run(command,cwd):
    print('+ '+' '.join(map(str,command)),flush=True)
    subprocess.run(list(map(str,command)),cwd=cwd,check=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sdk',type=Path)
    args=parser.parse_args()
    tools=ROOT/'out/host-tools'
    cmake=tools/'venv/bin/cmake'; ninja=tools/'venv/bin/ninja'; compiler=tools/'bin/clang++'
    with tempfile.TemporaryDirectory(dir=ROOT/'out',prefix='behavior-independent-') as temporary:
        directory=Path(temporary)
        sdk=directory/'sdk'; shutil.copytree(args.sdk,sdk,symlinks=True)
        project=directory/'project'; project.mkdir()
        shutil.copytree(Path(__file__).with_name('consumer'),project/'consumer')
        shutil.copytree(Path(__file__).with_name('fixtures'),project/'fixtures')
        source=project/'consumer'
        presets={'version':6,'configurePresets':[{'name':'behavior','generator':'Ninja','binaryDir':'${sourceDir}/out/build',
            'cacheVariables':{'CMAKE_BUILD_TYPE':json.loads((sdk/'share/Ludus/LudusSdkManifest.json').read_text())['build_type'],'CMAKE_CXX_COMPILER':str(compiler),'CMAKE_MAKE_PROGRAM':str(ninja),'CMAKE_PREFIX_PATH':str(sdk)}}],
            'buildPresets':[{'name':'behavior','configurePreset':'behavior'}],
            'testPresets':[{'name':'behavior','configurePreset':'behavior','output':{'outputOnFailure':True}}]}
        cache = presets['configurePresets'][0]['cacheVariables']
        if platform.system() == 'Darwin':
            cache.update(CMAKE_OSX_SYSROOT=str(tools/'macos-sdk'), CMAKE_OSX_DEPLOYMENT_TARGET='14.0',
                         CMAKE_CXX_FLAGS=f'-stdlib=libc++ -nostdinc++ -isystem "{tools / "libcxx-include"}"')
        # The SDK's generic toolchain escape hatch must not bypass Behavior's
        # actual target admission. This is a real find_package configuration.
        rejected = subprocess.run([str(cmake), '-S', str(source), '-B', str(directory/'rejected'),
            '-G', 'Ninja', '-DCMAKE_SYSTEM_NAME=Generic', '-DLUDUS_SKIP_TOOLCHAIN_CHECK=ON',
            *[f'-D{key}={value}' for key, value in cache.items()]], capture_output=True, text=True)
        if rejected.returncode == 0 or 'no admitted interpreter profile' not in rejected.stderr:
            raise AssertionError('installed profile gate bypassed: '+rejected.stderr)
        # Compile the installed header against a deliberately mislabeled CPU.
        # No linker or compiler-configuration assumption can hide that mismatch.
        mismatch = directory/'mismatch.cpp'
        mismatch.write_text('#include <ludus/runtime/behavior/behavior.h>\n')
        wrong_arch = 'LUDUS_CPU_X86_64' if platform.machine().lower() in ('arm64', 'aarch64') else 'LUDUS_CPU_ARM64'
        flags = shlex.split(cache.get('CMAKE_CXX_FLAGS', ''))
        if 'CMAKE_OSX_SYSROOT' in cache:
            flags += ['-isysroot', cache['CMAKE_OSX_SYSROOT']]
        rejected = subprocess.run([str(compiler), '-std=c++23', '-fsyntax-only',
            '-DLUDUS_BEHAVIOR_EXPECTED_ARCH='+wrong_arch, '-I'+str(sdk/'include'), *flags, str(mismatch)],
            capture_output=True, text=True)
        if rejected.returncode == 0 or 'Behavior SDK architecture mismatch' not in rejected.stderr:
            raise AssertionError('installed header accepted wrong CPU: '+rejected.stderr)
        (source/'CMakeUserPresets.json').write_text(json.dumps(presets,indent=2)+'\n')
        for kind in ('configure','build','test'): run([cmake,'--list-presets='+kind],source)
        run([cmake,'--preset','behavior'],source)
        run([cmake,'--build','--preset','behavior'],source)
        run([tools/'venv/bin/ctest','--preset','behavior','--no-tests=error'],source)
        # Install only the player component, inspect its runtime closure, archive
        # it without SDK/compiler/source payload, then execute clean extraction.
        payload=directory/'player'
        run([cmake,'--install','out/build','--prefix',payload,'--component','GameRelease','--strip'],source)
        if platform.system() == 'Darwin':
            linked = subprocess.run(['otool', '-L', str(payload/'bin/behavior_consumer')],
                                    capture_output=True, text=True, check=True).stdout
            for line in linked.splitlines()[1:]:
                dependency = line.strip().split()[0]
                if not dependency.startswith(('/usr/lib/', '/System/Library/')):
                    raise AssertionError('unshipped Mach-O dependency: '+dependency)
        else:
            validate_native(payload,'bin/behavior_consumer')
        records=inventory(payload)
        if not (payload/'licenses/Ludus/Luau/LICENSE.txt').is_file() or not (payload/'licenses/Ludus/Luau/lua_LICENSE.txt').is_file():raise AssertionError('missing player runtime notices')
        archive=directory/'player.zip'
        with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED) as writer:
            for record in records:writer.write(payload/record['path'],record['path'])
        extracted=directory/'extracted';extracted.mkdir()
        with zipfile.ZipFile(archive) as reader:reader.extractall(extracted)
        for record in records:(extracted/record['path']).chmod(record['mode'])
        if inventory(extracted)!=records:raise AssertionError('player archive changed content')
        run([extracted/'bin/behavior_consumer'],extracted)
        # A second build reuses only completed artifacts, then strict failure
        # retains the published cook and the existing shipping executable.
        run([cmake,'--build','--preset','behavior'],source)
        current=source/'out/build/encounter/current.json'
        before=current.read_bytes()
        script=project/'fixtures/door.luau'; script.write_text(script.read_text()+'\nlocal invalid: number = "wrong"\n')
        failed=subprocess.run([str(cmake),'--build','--preset','behavior'],cwd=source,capture_output=True)
        if failed.returncode==0 or current.read_bytes()!=before: raise AssertionError('failed cook replaced active artifact')
        run([tools/'venv/bin/ctest','--preset','behavior','--no-tests=error'],source)
        print('S6 relocated SDK, target rejection, paired cook, selectable presets and shipping archive/extraction execution PASS')
if __name__=='__main__':main()
