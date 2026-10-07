"""Paired strict Luau acquisition for the S3 owner; layout never affects code keys.

Thanks to Roblox Corporation, "Sandboxing", Bytecode section
(https://luau.org/sandbox/): only the paired compiler supplies admitted bytecode.
See docs/architecture/visual-s3.md for this private authoring boundary.
"""
import hashlib
import json
from pathlib import Path
import runpy
import subprocess
import tempfile

import model

ROOT = Path(__file__).resolve().parents[2]
S1 = runpy.run_path(str(ROOT / 'scripts/script-interaction'), run_name='s1_tool')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def compile_document(document, revision, cache):
    model.integer(revision, 1, 2147483647)
    S1['verify']()
    source, maps = model.lower(document)
    return compile_source(source, maps, document['graph'], model.executable(document), revision, cache)


def compile_source(source, maps, graph, semantic, revision, cache):
    definitions = (S1['WORK'] / 'cooked/contract.d.luau').read_text()
    pin = sha((ROOT / 'config/luau_toolchain.json').read_bytes())
    contract = sha(S1['MANIFEST'].read_bytes())
    key = sha(json.dumps({'semantic': semantic, 'generator': sha((ROOT/'scripts/python/ludus_tools/behavior_graph.py').read_bytes()),
                         'cook': sha(Path(__file__).read_bytes()), 'tools': json.loads((S1['WORK'] / 'host.json').read_text()),
                         'contract': contract, 'definitions': sha(definitions.encode()), 'pin': pin, 'profile': 'interpreter-o1-g2'},
                        sort_keys=True, separators=(',', ':')).encode())
    cache.mkdir(parents=True, exist_ok=True)
    cached = cache / (key + '.json')
    if cached.is_file():
        record = json.loads(cached.read_text(), object_pairs_hook=model.unique)
        code = bytes.fromhex(record['code'])
        model.require(record['key'] == key and sha(code) == record['code_hash'], 'modified compiler cache')
    else:
        with tempfile.TemporaryDirectory(dir=cache) as temporary:
            path = Path(temporary) / 'encounter.luau'
            path.write_text(definitions + source)
            subprocess.run([str(S1['ANALYZER']), '--mode=strict', str(path)], check=True, timeout=30,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            code = subprocess.run([str(S1['COMPILER']), '--binary', '-O1', '-g2', str(path)], check=True, timeout=30,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout
            model.require(code and code[0] != 0 and len(code) <= 262144, 'invalid paired compiler output')
            # Atomic cache publication after strict analysis and paired compilation.
            record = {'key': key, 'code': code.hex(), 'code_hash': sha(code)}
            target = Path(temporary) / 'artifact.json'
            target.write_text(json.dumps(record))
            target.replace(cached)
    model.require(code and code[0] != 0 and len(code) <= 262144, 'compiler cache capacity/version')
    first = definitions.count('\n') + 1
    return {'version': 1, 'key': key, 'pin': pin, 'contract': contract, 'revision': revision,
            'name': '@visual/' + graph, 'code': code.hex(), 'first_line': first}, {
        'key': key, 'semantic': semantic, 'source': source,
        'spans': [dict(span, compiled_line=span['line'] + first - 1) for span in maps]}
