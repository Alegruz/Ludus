"""Reinstall an SDK without Behavior and verify its owned payload is removed."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('preset'); args=parser.parse_args()
    cmake=ROOT/'out/host-tools/venv/bin/cmake'
    def run(*arguments):subprocess.run([str(cmake),*map(str,arguments)],cwd=ROOT,check=True)
    with tempfile.TemporaryDirectory(dir=ROOT/'out',prefix='behavior-disabled-') as temporary:
        prefix=Path(temporary)/'sdk'
        shutil.copytree(ROOT/'out/install'/args.preset,prefix,symlinks=True)
        sentinel=prefix/'unrelated.txt';sentinel.write_text('preserve')
        try:
            run('--preset',args.preset,'-DLUDUS_BUILD_BEHAVIOR=OFF','-DLUDUS_BUILD_BEHAVIOR_ACCEPTANCE=OFF')
            run('--install',ROOT/'out/build'/args.preset,'--prefix',prefix)
            for relative in ('include/ludus/runtime/behavior','lib/Ludus/behavior','share/Ludus/behavior','share/Ludus/licenses/Luau','lib/cmake/Ludus/LudusBehavior.cmake','lib/libludus_runtime_behavior.a'):
                if (prefix/relative).exists():raise AssertionError('stale optional payload: '+relative)
            manifest=json.loads((prefix/'share/Ludus/LudusSdkManifest.json').read_text())
            if 'Behavior' in manifest['components'] or any(dep['name']=='Luau' for dep in manifest['dependencies']):raise AssertionError('disabled dependency remained in manifest')
            consumer=Path(temporary)/'consumer';consumer.mkdir()
            (consumer/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.28)\nproject(NoBehavior LANGUAGES CXX)\nfind_package(Ludus REQUIRED COMPONENTS Behavior)\n')
            result=subprocess.run([str(cmake),'-S',str(consumer),'-B',str(consumer/'out'),'-G','Ninja','-DCMAKE_BUILD_TYPE=RelWithDebInfo','-DCMAKE_CXX_COMPILER='+str(ROOT/'out/host-tools/bin/clang++'),'-DCMAKE_MAKE_PROGRAM='+str(ROOT/'out/host-tools/venv/bin/ninja'),'-DCMAKE_PREFIX_PATH='+str(prefix)],capture_output=True,text=True)
            if result.returncode==0 or "Behavior' is unavailable" not in result.stderr:raise AssertionError('missing Behavior component did not fail explicitly: '+result.stderr)
            if sentinel.read_text()!='preserve':raise AssertionError('unrelated SDK content was removed')
            print('S4 optional component disabled/reinstall pruning PASS')
        finally:
            run('--preset',args.preset,'-DLUDUS_BUILD_BEHAVIOR=ON','-DLUDUS_BUILD_BEHAVIOR_ACCEPTANCE=ON')
if __name__=='__main__':main()
