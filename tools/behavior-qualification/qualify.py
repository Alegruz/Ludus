"""S7 installed-SDK reference evidence; timings are not game or device certification."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import shutil
import shlex
import subprocess
import sys
import tempfile
import time
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/python'))
from ludus_tools import behavior_workspace
from ludus_tools.package_native import validate_native
from ludus_tools.package_verify import inventory
from ludus_tools.errors import ToolingError

CASES = ('native.empty', 'luau.empty', 'native.scalar', 'luau.scalar', 'native.operation', 'luau.operation',
         'native.encounter', 'luau.encounter', 'graph.encounter', 'native.dormant', 'luau.dormant', 'graph.dormant')
PROFILE_KEYS = {'version', 'workload', 'scope', 'active_encounters', 'inactive_encounters', 'samples',
                'warmup_samples', 'heap_limit_bytes', 'safepoints', 'proposed_tick_budget_ns', 'timing_policy', 'unqualified'}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def unique(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'duplicate JSON field: ' + key)
        result[key] = value
    return result


def document(path):
    require(path.stat().st_size <= 16 * 1024 * 1024, 'oversized evidence input')
    return parse(path.read_text())


def parse(text):
    return json.loads(text, object_pairs_hook=unique,
                      parse_constant=lambda value: (_ for _ in ()).throw(ValueError('nonfinite JSON: ' + value)))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def integer(value, low=0, high=(1 << 64) - 1):
    require(type(value) is int and low <= value <= high, 'integer outside qualification bounds')
    return value


def profile(path):
    value = document(path)
    require(type(value) is dict and set(value) == PROFILE_KEYS, 'unknown or missing qualification fields')
    integer(value['version'], 1, 1)
    require(value['workload'] == 'scripted-game-reference-1' and value['scope'] == 'reference-fixture-not-game-capacity', 'unsupported workload')
    integer(value['samples'], 100, 1000)
    integer(value['warmup_samples'], 1, 1000)
    integer(value['active_encounters'], 1, 4096)
    integer(value['inactive_encounters'], 0, 4095)
    require(value['active_encounters'] + value['inactive_encounters'] <= 4096, 'encounter capacity')
    integer(value['heap_limit_bytes'], 65536, 64 * 1024 * 1024)
    integer(value['safepoints'], 1, 4294967295)
    integer(value['proposed_tick_budget_ns'], 1)
    require(value['timing_policy'] == 'report-only', 'reference profile cannot assert a product timing budget')
    require(value['unqualified'] == ['game-derived-budgets', '10000-waiting-tasks', 'gc-pause-distribution',
                                    'human-designer-usability', 'physical-device-release'], 'qualification gaps changed')
    return value


def measurements(value, workload, instrumented, target):
    require(type(value) is dict and set(value) == {'version', 'instrumented', 'target', 'samples', 'warmup', 'active',
                                                   'inactive', 'contract', 'package', 'cases', 'equivalence', 'live_after_close'}, 'measurement envelope')
    require(value['version'] == 1 and type(value['version']) is int and value['instrumented'] is instrumented, 'measurement version/probe mismatch')
    require(value['equivalence'] is True and integer(value['live_after_close']) == 0, 'provider retirement/equivalence failed')
    require(value['target'] == target and set(value['target']) == {'os', 'arch', 'pointer_bits'}, 'executed target mismatch')
    for field, source in [('samples', 'samples'), ('warmup', 'warmup_samples'), ('active', 'active_encounters'), ('inactive', 'inactive_encounters')]:
        require(type(value[field]) is int and value[field] == workload[source], 'compiled workload mismatch: ' + field)
    for field in ('contract', 'package'):
        require(type(value[field]) is str and len(value[field]) == 64 and all(c in '0123456789abcdef' for c in value[field]), 'invalid cook identity')
    require(type(value['cases']) is list and len(value['cases']) == len(CASES), 'case inventory')
    keys = {'name', 'ready', 'dormant', 'commands', 'interactions', 'opened', 'entity_checks', 'aligned_requests',
            'aligned_requested_bytes', 'boundary_live_max_bytes', 'samples_ns', 'p50_ns', 'p95_ns', 'p99_ns', 'worst_ns'}
    for item, expected in zip(value['cases'], CASES):
        require(type(item) is dict and set(item) == keys and item['name'] == expected, 'missing/duplicate/reordered case')
        ready = 0 if expected.endswith('.dormant') else workload['active_encounters'] if expected.endswith('.encounter') else 1
        require(integer(item['ready']) == ready and integer(item['dormant']) == workload['active_encounters'] + workload['inactive_encounters'] - ready, 'ready/dormant mismatch')
        for name in keys - {'name', 'samples_ns'}:
            integer(item[name])
        require(type(item['samples_ns']) is list and len(item['samples_ns']) == workload['samples'], 'sample count mismatch')
        samples = sorted(integer(sample) for sample in item['samples_ns'])
        for percentile in (50, 95, 99):
            require(item[f'p{percentile}_ns'] == samples[math.ceil(len(samples) * percentile / 100) - 1], 'incorrect percentile')
        require(item['worst_ns'] == samples[-1], 'incorrect maximum')
        require(item['boundary_live_max_bytes'] <= workload['heap_limit_bytes'], 'VM boundary bytes exceed cap')
        if expected.startswith('native.') or ready == 0:
            require(item['aligned_requests'] == 0 and item['aligned_requested_bytes'] == 0, 'native/dormant VM allocation')
        if not instrumented:
            require(item['aligned_requests'] == 0 and item['aligned_requested_bytes'] == 0, 'timed executable has a counting probe')
        interactions = ready * ((workload['samples'] - 1) % 100 + 1) if expected.endswith(('.scalar', '.encounter')) else 0
        opened = ready if expected.endswith('.encounter') and (workload['samples'] - 1) % 100 + 1 >= 2 else 0
        commands = workload['samples'] * ready if expected.endswith('.operation') else (workload['samples'] + 98) // 100 * ready if expected.endswith('.encounter') else 0
        require((item['commands'], item['interactions'], item['opened']) == (commands, interactions, opened), 'unexpected reference outcome')
        if ready == 0:
            require(item['commands'] == item['interactions'] == item['opened'] == item['entity_checks'] == 0, 'dormant execution')
    return value


def paired(timing, counting):
    require(timing['contract'] == counting['contract'] and timing['package'] == counting['package'], 'counting/timing cook mismatch')
    for left, right in zip(timing['cases'], counting['cases']):
        for key in ('name', 'ready', 'dormant', 'commands', 'interactions', 'opened', 'entity_checks'):
            require(left[key] == right[key], 'counting changed semantics: ' + key)
    for names in [('native.empty', 'luau.empty'), ('native.scalar', 'luau.scalar'), ('native.operation', 'luau.operation'),
                  ('native.encounter', 'luau.encounter', 'graph.encounter')]:
        cases = [next(item for item in timing['cases'] if item['name'] == name) for name in names]
        require(all((item['commands'], item['interactions'], item['opened']) ==
                    (cases[0]['commands'], cases[0]['interactions'], cases[0]['opened']) for item in cases), 'native/text/sequence measured outcome mismatch')


def run(command, cwd, label, timeout=180):
    start = time.perf_counter_ns()
    result = subprocess.run(list(map(str, command)), cwd=cwd, capture_output=True, text=True, timeout=timeout)
    elapsed = time.perf_counter_ns() - start
    (cwd / (label + '.log')).write_text(result.stdout + result.stderr)
    require(result.returncode == 0, label + ' failed:\n' + (result.stdout + result.stderr)[-8192:])
    return result.stdout, elapsed


def atomic(path, content):
    with tempfile.NamedTemporaryFile(mode='w', dir=path.parent, prefix='.' + path.name, delete=False) as handle:
        handle.write(content)
        temporary = Path(handle.name)
    try:
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def cook_pointer(directory):
    pointer = document(directory / 'current.json')
    require(set(pointer) == {'version', 'key', 'directory'} and type(pointer['version']) is int and pointer['version'] == 1, 'invalid cook pointer')
    key = pointer['key']
    require(type(key) is str and len(key) == 64 and all(c in '0123456789abcdef' for c in key) and pointer['directory'] == key, 'invalid cook directory')
    return pointer['key']


def iteration(work, source, sdk, python, observer):
    project = work / 'iteration'
    shutil.copytree(source, project, ignore=shutil.ignore_patterns('out', 'CMakeUserPresets.json'))
    resource = sdk / 'share/Ludus/behavior'
    command = [python, resource / 'behavior_cook.py', *behavior_workspace.arguments(project, work, sdk)]
    # The project's metadata target is encounter; keep this independent from
    # the already-built and packaged baseline project.
    output = work / 'encounter'
    run(command, work, 'iteration-baseline')
    baseline = cook_pointer(output)
    script = project / 'behaviors/encounter.luau'
    text = script.read_text()
    require(text.count('>= 2') == 1, 'reference edit is ambiguous')
    start = time.perf_counter_ns()
    atomic(script, text.replace('>= 2', '>= 3'))
    _, cook_ns = run(command, work, 'iteration-cook')
    key = cook_pointer(output)
    require(key != baseline, 'meaningful edit reused baseline')
    package = output / key / 'behavior.lupack'
    _, observe_ns = run([observer, '--observe-edit', package], work, 'iteration-observe')
    elapsed = time.perf_counter_ns() - start
    before = (output / 'current.json').read_bytes()
    atomic(script, script.read_text() + '\nlocal invalid: number = "wrong"\n')
    rejected = subprocess.run(list(map(str, command)), cwd=work, capture_output=True, text=True, timeout=180)
    require(rejected.returncode != 0 and (output / 'current.json').read_bytes() == before, 'failed edit replaced published cook')
    # Existing immutable candidate still executes the saved behavior.
    run([observer, '--observe-edit', package], work, 'iteration-retained')
    atomic(script, text.replace('>= 2', '>= 3'))
    graph = project / 'behaviors/encounter.json'
    value = document(graph)
    value['layout']['positions'][0]['x'] += 10
    atomic(graph, json.dumps(value, indent=2) + '\n')
    _, layout_ns = run(command, work, 'iteration-layout')
    require(cook_pointer(output) == key, 'layout-only edit invalidated bytecode')
    return {'metric': 'save-cook-fresh-process-provider-observation', 'save_to_observed_ns': elapsed,
            'cook_ns': cook_ns, 'observe_process_ns': observe_ns, 'baseline_key': baseline, 'edited_key': key,
            'edited_package_sha256': digest(package), 'failed_candidate_retained': True, 'layout_key_reused': True,
            'layout_cook_ns': layout_ns}


def release(work, source, cmake, build, manifest):
    payload = work / 'payload'
    run([cmake, '--install', build, '--prefix', payload, '--component', 'GameRelease', '--strip'], work, 'install-player')
    require(manifest['build_flavor'] == 'Release' and not any(manifest['sanitizers'].values()), 'release must use a nonsanitized Release SDK')
    require(manifest['target_os'] == 'Linux' and manifest['target_arch'] == 'x86_64', 'first S7 packaging policy is Linux x86_64 only')
    external = validate_native(payload, 'bin/scripted_game')
    for executable in ('scripted_game', 'scripted_game_release_verify'):
        symbols, _ = run(['nm', '-C', build / ('scripted_game' if executable == 'scripted_game' else 'benchmarks/scripted_game_release_verify')], work, 'symbols-' + executable)
        require('ludus::' in symbols, 'unstripped symbol audit is empty')
        require(not any(marker in symbols for marker in ('Luau::compile(', 'Luau::Compiler', 'Luau::Ast', 'Luau::CodeGen', 'QApplication', 'QWidget')), 'unselected compiler/codegen/Qt closure')
    require((payload / 'licenses/Ludus/Luau/LICENSE.txt').is_file() and (payload / 'licenses/Ludus/Luau/lua_LICENSE.txt').is_file(), 'Luau player notices missing')
    records = inventory(payload)
    allowed = {'bin/scripted_game', 'bin/scripted_game_release_verify', 'NOTICE.txt'}
    require(all(item['path'] in allowed or item['path'].startswith('licenses/Ludus/') for item in records), 'unexpected player file')
    archive = work / 'scripted-game-release.zip'
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as writer:
        for item in records:
            writer.write(payload / item['path'], item['path'])
    extracted = work / 'extracted'
    extracted.mkdir()
    with zipfile.ZipFile(archive) as reader:
        reader.extractall(extracted)
    for item in records:
        (extracted / item['path']).chmod(item['mode'])
    require(inventory(extracted) == records, 'archive changed player content')
    # Execute with only the declared system runtime available. Build/project/SDK
    # files are hidden during execution, not just omitted from the archive.
    hidden = []
    for path in (source, work / 'sdk'):
        moved = path.with_name(path.name + '.hidden')
        path.rename(moved)
        hidden.append((path, moved))
    try:
        env = {key: value for key, value in os.environ.items() if key not in {'LD_LIBRARY_PATH', 'LD_PRELOAD', 'LUDUS_SDK_PREFIX', 'PYTHONPATH'}}
        env['PATH'] = '/usr/bin:/bin'
        executions = []
        for args in [('scripted_game', '--headless', '--max-frames', '2'), ('scripted_game_release_verify',)]:
            result = subprocess.run([str(extracted / 'bin' / args[0]), *args[1:]], cwd=extracted,
                                    env=env, capture_output=True, text=True, timeout=60)
            require(result.returncode == 0, 'extracted player failed: ' + result.stderr[-8192:])
            executions.append({'entry': 'bin/' + args[0], 'arguments': list(args[1:]), 'exit_code': result.returncode})
    finally:
        for path, moved in reversed(hidden):
            moved.rename(path)
    return {'policy': 'linux-x64-headless-reference-release-1', 'archive_sha256': digest(archive),
            'archive_bytes': archive.stat().st_size, 'files': records, 'system_libraries': external,
            'executions': executions, 'game_state_verified': True}


def check_sources(source, tools, work):
    from formatting import format_source
    formatter = tools / 'bin/clang-format'
    tidy = tools / 'bin/clang-tidy'
    require(formatter.is_file() and tidy.is_file(), 'prepare pinned format/tidy tools')
    for path in sorted((source / 'benchmarks').glob('*')):
        if path.suffix in ('.cpp', '.h'):
            require(path.read_text() == format_source(path.read_text(), path, str(formatter)), 'format mismatch: ' + str(path))
    entries = document(source / 'out/build/compile_commands.json')
    for index, entry in enumerate(entries):
        flags = shlex.split(entry['command'])[1:]
        clean = []
        cursor = 0
        while cursor < len(flags):
            flag = flags[cursor]
            if flag == '-o':
                cursor += 2
                continue
            if flag not in ('-c', entry['file']) and not flag.startswith('@'):
                clean.append(flag)
            cursor += 1
        run([tidy, entry['file'], '--quiet', '--warnings-as-errors=*', '--', *clean], work, 'tidy-' + str(index))


def cpu_model():
    path = Path('/proc/cpuinfo')
    if path.is_file():
        for line in path.read_text().splitlines():
            if line.startswith('model name') and ':' in line:
                return line.split(':', 1)[1].strip()
    return platform.processor() or 'unavailable'


def qualify(args):
    workload = profile(args.profile)
    sdk = args.sdk.resolve()
    manifest_path = sdk / 'share/Ludus/LudusSdkManifest.json'
    manifest = document(manifest_path)
    require('Behavior' in manifest['components'], 'opt-in Behavior SDK required')
    require(manifest['target_os'] == 'Linux' and manifest['target_arch'] == 'x86_64', 'first S7 reference runner supports Linux x86_64')
    if not args.verify_only:
        require(manifest['build_flavor'] == 'Release' and not any(manifest['sanitizers'].values()), 'timing/release qualification requires a nonsanitized Release SDK')
    tools = ROOT / 'out/host-tools'
    cmake, ninja, python, compiler = (tools / path for path in ('venv/bin/cmake', 'venv/bin/ninja', 'venv/bin/python', 'bin/clang++'))
    require(all(path.is_file() for path in (cmake, ninja, python, compiler)), 'prepare pinned host tools explicitly')
    work = args.output.resolve()
    require(work.is_relative_to(ROOT / 'out') and work != ROOT / 'out', 'output must be a task-owned directory under out/')
    output = work
    output.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(dir=output, prefix='run-'))
    evidence = work / 'evidence.json'
    copied_sdk = work / 'sdk'
    shutil.copytree(sdk, copied_sdk, symlinks=True)
    source = work / 'project'
    shutil.copytree(ROOT / 'examples/scripted-game', source, ignore=shutil.ignore_patterns('out', 'CMakeUserPresets.json'))
    sources = {str(path.relative_to(source)): digest(path) for path in sorted(source.rglob('*')) if path.is_file()}
    cache = {'CMAKE_BUILD_TYPE': manifest['build_type'], 'CMAKE_CXX_COMPILER': str(compiler), 'CMAKE_MAKE_PROGRAM': str(ninja),
             'CMAKE_PREFIX_PATH': str(copied_sdk), 'CMAKE_EXPORT_COMPILE_COMMANDS': 'ON', 'SCRIPTED_GAME_QUALIFICATION': 'ON'}
    for setting, field in [('SAMPLES', 'samples'), ('WARMUP', 'warmup_samples'), ('ACTIVE', 'active_encounters'),
                           ('INACTIVE', 'inactive_encounters'), ('HEAP', 'heap_limit_bytes'), ('SAFEPOINTS', 'safepoints')]:
        cache['SCRIPTED_GAME_' + setting] = str(workload[field])
    presets = {'version': 6, 'configurePresets': [{'name': 'qualification', 'generator': 'Ninja', 'binaryDir': '${sourceDir}/out/build', 'cacheVariables': cache}],
               'buildPresets': [{'name': 'qualification', 'configurePreset': 'qualification'}],
               'testPresets': [{'name': 'qualification', 'configurePreset': 'qualification', 'output': {'outputOnFailure': True}}]}
    (source / 'CMakeUserPresets.json').write_text(json.dumps(presets, indent=2) + '\n')
    for kind in ('configure', 'build', 'test'):
        run([cmake, '--list-presets=' + kind], source, 'presets-' + kind)
    _, configure_ns = run([cmake, '--preset', 'qualification'], source, 'configure')
    _, build_ns = run([cmake, '--build', '--preset', 'qualification', '--parallel', '2'], source, 'build')
    tests_text, _ = run([tools / 'venv/bin/ctest', '--preset', 'qualification', '--show-only=json-v1'], source, 'test-inventory')
    tests = [item['name'] for item in parse(tests_text)['tests']]
    require(tests == ['scripted_game_static', 'scripted_game_equivalence', 'scripted_game_allocations', 'scripted_game_release_semantics'], 'acceptance test inventory changed')
    run([tools / 'venv/bin/ctest', '--preset', 'qualification', '--no-tests=error'], source, 'test')
    build = source / 'out/build'
    commands = document(build / 'compile_commands.json')
    require(commands and all([flag for flag in shlex.split(entry['command']) if flag in ('-fexceptions', '-fno-exceptions')][-1:] == ['-fno-exceptions'] for entry in commands), 'consumer exceptions policy mismatch')
    if args.check:
        check_sources(source, tools, work)
    report = {'version': 1, 'status': 'passed', 'scope': workload['scope'], 'profile_sha256': digest(args.profile), 'profile': workload,
              'host': {'system': platform.platform(), 'machine': platform.machine(), 'processor': cpu_model()},
              'sdk_manifest_sha256': digest(manifest_path), 'sdk': manifest, 'sources': sources,
              'build': {'configure_ns': configure_ns, 'build_ns': build_ns, 'exception_free': True, 'compile_commands_sha256': digest(build / 'compile_commands.json')}, 'correctness_only': args.verify_only, 'format_tidy_checked': args.check,
              'acceptance': {'tests': tests, 'test_log_sha256': digest(source / 'test.log')}}
    if not args.verify_only:
        target = {'os': manifest['target_os'], 'arch': manifest['target_arch'], 'pointer_bits': 64}
        timing_text, _ = run([build / 'benchmarks/scripted_game_qualification', '--measure'], work, 'timing')
        counting_text, _ = run([build / 'benchmarks/scripted_game_allocations', '--measure'], work, 'counting')
        timing = measurements(parse(timing_text), workload, False, target)
        counting = measurements(parse(counting_text), workload, True, target)
        paired(timing, counting)
        cook = build / 'benchmarks/qualification'
        key = cook_pointer(cook)
        require(timing['package'] == key, 'executed measurement has stale cook')
        cook_evidence = document(cook / key / 'evidence.json')
        require(cook_evidence['key'] == key and cook_evidence['outputs'] == {p.name: digest(p) for p in (cook / key).iterdir() if p.name != 'evidence.json'}, 'benchmark cook artifact integrity')
        report['cook'] = cook_evidence
        report['timing'] = timing
        report['allocation_counts'] = counting
        report['proposed_budget_results'] = {case['name']: case['p99_ns'] <= workload['proposed_tick_budget_ns'] for case in timing['cases'] if case['name'].endswith('.encounter')}
        report['artifacts'] = {str(path.relative_to(work)): {'sha256': digest(path), 'bytes': path.stat().st_size} for path in
            (build / 'benchmarks/scripted_game_qualification', build / 'benchmarks/scripted_game_allocations', build / 'scripted_game')}
        report['release'] = release(work, source, cmake, build, manifest)
        report['iteration'] = iteration(work, source, copied_sdk, python, build / 'benchmarks/scripted_game_qualification')
    atomic(evidence, json.dumps(report, sort_keys=True, indent=2) + '\n')
    atomic(output / 'current.json', json.dumps({'version': 1, 'run': work.name, 'evidence_sha256': digest(evidence)}) + '\n')
    print('S7 reference qualification PASS: ' + str(evidence))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--profile', type=Path, default=ROOT / 'config/behavior_qualification.json')
    parser.add_argument('--verify-only', action='store_true')
    parser.add_argument('--check', action='store_true', help='Check sample formatting and every consumer translation unit with pinned tidy')
    args = parser.parse_args()
    try:
        qualify(args)
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError, ToolingError) as error:
        print('S7 qualification failed: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
