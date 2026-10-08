"""Explicit real acquisition and harmless version acceptance for an installed CLI.

Runs without login/upload credentials. CI qualifies Linux x64 and both Mac CPUs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', required=True, type=Path)
    args = parser.parse_args()
    cli = args.cli.absolute()
    with tempfile.TemporaryDirectory(prefix='ludus-butler-acceptance-') as temporary:
        root = Path(temporary)
        destination = root / 'tools with spaces'
        env = {'PATH': os.defpath, 'HOME': str(root)}
        installed = subprocess.run([str(cli), '--json', 'tools', 'install-butler', str(destination)],
            cwd=root, env=env, capture_output=True, text=True, check=True, timeout=180)
        result = json.loads(installed.stdout)
        binary = Path(result['executable'])
        assert binary == destination / 'butler'
        assert sorted(p.name for p in destination.iterdir()) == ['butler']
        before = hashlib.sha256(binary.read_bytes()).hexdigest()
        version = subprocess.run([str(binary), 'version'], cwd=root, env=env,
                                 capture_output=True, text=True, check=True, timeout=30)
        assert version.stdout.startswith('v' + result['version'] + ','), version.stdout
        retry = subprocess.run([str(cli), '--json', 'tools', 'install-butler', str(destination)],
                               cwd=root, env=env, capture_output=True, text=True, timeout=30)
        assert retry.returncode != 0
        assert json.loads(retry.stdout)['error']['code'] == 'Conflict'
        assert hashlib.sha256(binary.read_bytes()).hexdigest() == before
        print(result['platform'], version.stdout.strip(), 'verified install, no-overwrite and clean-environment execution PASS')


if __name__ == '__main__':
    main()
