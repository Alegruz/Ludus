"""Owned loopback S3 workbench. Only this paired cooker supplies VM bytecode.

The browser sends typed authoring/debug actions, never process arguments, paths,
source text or bytecode. A bearer token, exact Host/Origin and no CORS admit the
one local workspace. Native stdout is a bounded data protocol, not diagnostics.
"""
import copy
import http.server
import json
import os
from pathlib import Path
import secrets
import selectors
import stat
import subprocess
import tempfile

import cook
import model

WEB = Path(__file__).with_name('web')


class Native:
    def __init__(self, executable):
        self.temp = tempfile.TemporaryDirectory(prefix='ludus-s3-')
        self.log = open(Path(self.temp.name) / 'runtime.log', 'w+b')
        command = [os.environ.get('LUDUS_S3_NODE') or 'node', str(Path(__file__).with_name('wasm-driver.mjs')), str(executable)] if Path(executable).suffix == '.js' else [str(executable)]
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.log)
        self.buffer = bytearray()

    def request(self, value):
        model.require(self.process.poll() is None, 'runtime retired; restart preview')
        data = json.dumps(dict(version=1, **value), separators=(',', ':')).encode() + b'\n'
        model.require(len(data) <= 655360, 'runtime request capacity')
        self.process.stdin.write(data)
        self.process.stdin.flush()
        selector = selectors.DefaultSelector()
        try:
            selector.register(self.process.stdout, selectors.EVENT_READ)
            while b'\n' not in self.buffer:
                model.require(selector.select(15), 'runtime reply deadline')
                chunk = os.read(self.process.stdout.fileno(), 8192)
                model.require(chunk, 'runtime disconnected')
                self.buffer.extend(chunk)
                model.require(len(self.buffer) <= 16384, 'runtime reply capacity')
            line, _, remaining = self.buffer.partition(b'\n')
            self.buffer = bytearray(remaining)
            return json.loads(line, object_pairs_hook=model.unique)
        finally:
            selector.close()

    def close(self):
        try:
            if self.process.poll() is None:
                result = self.request({'action': 'close'})
                model.require(result['ok'] and result['heap'] == 0, 'VM retirement failed')
                self.process.stdin.close()
                model.require(self.process.wait(timeout=5) == 0, 'runtime retirement failed')
        finally:
            if self.process.poll() is None:
                self.process.terminate()
                try:
                    self.process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=5)
            if not self.process.stdin.closed: self.process.stdin.close()
            self.process.stdout.close()
            self.log.close()
            self.temp.cleanup()


def read_document(path):
    flags = os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0)
    descriptor = os.open(path, flags)
    with os.fdopen(descriptor, 'r', encoding='utf-8') as file:
        text = file.read(model.MAX_BYTES + 1)
    return model.parse(text), text


class Owner:
    def __init__(self, path, executable, cache):
        self.path = Path(path).absolute()
        value, text = read_document(self.path)
        self.disk = text
        self.document = model.Document(value)
        self.executable = executable
        self.runtime = Native(executable)
        self.cache = Path(cache)
        self.preview = None
        self.active_document = None
        self.maps = None
        self.runtime_revision = 0
        self.epoch = secrets.token_hex(8)

    def cursor(self):
        current = self.preview.get('runtime') if self.preview else None
        return {'epoch': self.epoch, **{k: current[k] if current else None for k in ('execution', 'revision', 'stop', 'package')}}

    def state(self):
        return {'cursor': self.cursor(), 'document': self.document.draft, 'revision': self.document.revision,
                'dirty': self.document.draft != self.document.saved,
                'undo': bool(self.document.undo), 'redo': bool(self.document.redo),
                'semantic': model.executable(self.document.draft),
                'review': model.diff(self.document.saved, self.document.draft),
                'preview': self.preview, 'active_semantic': model.executable(self.active_document) if self.active_document else None,
                'maps': self.maps, 'draft_source': model.lower(self.document.draft)[0],
                'operation': json.loads(cook.S1['MANIFEST'].read_text())['operation']}

    def debug_request(self, action, **extra):
        model.require(self.preview and self.preview.get('runtime'), 'cook and start a preview first')
        current = self.preview['runtime']
        request = {'version': 1, 'action': action, **{k: current[k] for k in ('session', 'execution', 'revision', 'stop')}, **extra}
        return {'action': 'debug', 'request': json.dumps(request, separators=(',', ':'))}

    def act(self, request):
        model.require(type(request) is dict and type(request.get('action')) is str, 'closed authoring action required')
        action = request['action']
        if action == 'state':
            model.closed(request, ('action',))
            if self.preview:
                self.preview = self.runtime.request({'action': 'inspect'})
            return self.state()
        # Every command, including debug controls, carries the document revision.
        model.integer(request.get('revision'), 1, 2147483647)
        model.require(request['revision'] == self.document.revision, 'stale document revision')
        play = ('cook', 'interact', 'breakpoint', 'continue', 'into', 'over', 'out', 'restart')
        if action in play:
            model.require(request.get('cursor') == self.cursor(), 'stale preview execution/stop')
        if action == 'command':
            model.closed(request, ('action', 'revision', 'command'))
            self.document.command(request['command'])
        elif action in ('undo', 'redo'):
            model.closed(request, ('action', 'revision'))
            self.document.history(action == 'redo')
        elif action == 'merge':
            model.closed(request, ('action', 'revision', 'remote'))
            merged = model.merge(self.document.saved, self.document.draft, request['remote'])
            disk_document, disk_text = read_document(self.path)
            changed = disk_text != self.disk
            model.require(not changed or model.digest(disk_document) == model.digest(request['remote']),
                          'disk changed; merge the current disk document')
            self.document.commit(merged, {'kind': 'merge'})
            if changed:
                self.disk = disk_text
                self.document.saved = copy.deepcopy(request['remote'])
        elif action == 'save':
            model.closed(request, ('action', 'revision'))
            # No browser-supplied path; detect external edits and reject symlinks.
            _, current = read_document(self.path)
            model.require(current == self.disk, 'document changed on disk; merge or reopen explicitly')
            text = model.canonical(self.document.draft)
            with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=self.path.parent, delete=False) as file:
                temporary = Path(file.name)
                try:
                    file.write(text)
                    file.flush()
                    os.fsync(file.fileno())
                    file.close()
                    _, current = read_document(self.path)
                    model.require(current == self.disk, 'document changed during save')
                    os.chmod(temporary, stat.S_IMODE(self.path.stat(follow_symlinks=False).st_mode))
                    temporary.replace(self.path)
                finally:
                    temporary.unlink(missing_ok=True)
            self.disk = text
            self.document.saved = copy.deepcopy(self.document.draft)
        elif action == 'reopen':
            model.closed(request, ('action', 'revision', 'discard'))
            model.require(request['discard'] is True, 'explicit draft discard required')
            value, text = read_document(self.path)
            replacement = model.Document(value)
            replacement.revision = self.document.revision + 1
            self.document = replacement
            self.disk = text
        elif action == 'cook':
            model.closed(request, ('action', 'revision', 'cursor'))
            if self.preview:
                self.preview = self.runtime.request({'action': 'inspect'})
                model.require(not self.preview['runtime']['partial'] and not self.preview['runtime']['faulted'],
                              'finish the paused tick or restart before replacement')
            changed = self.active_document is None or model.executable(self.document.draft) != model.executable(self.active_document)
            if changed:
                # Admission only after the owned strict paired compiler succeeds.
                artifact, maps = cook.compile_document(self.document.draft, self.runtime_revision + 1, self.cache)
                expected = self.preview['runtime']['package'] if self.preview else ''
                result = self.runtime.request({'action': 'load', 'artifact': artifact, 'expected': expected})
                model.require(result['ok'], result['error'])
                self.preview = result
                self.active_document = copy.deepcopy(self.document.draft)
                self.maps = maps
                self.runtime_revision += 1
        elif action == 'interact':
            model.closed(request, ('action', 'revision', 'cursor', 'instance', 'amount'))
            model.integer(request['instance'], 0, 1)
            model.integer(request['amount'], 1, 10)
            model.require(self.preview is not None, 'cook a preview first')
            result = self.runtime.request({'action': 'interact', 'instance': request['instance'], 'amount': request['amount']})
            self.preview = result
            model.require(result['ok'], result['error'])
        elif action == 'breakpoint':
            model.closed(request, ('action', 'revision', 'cursor', 'node', 'enabled'))
            model.require(type(request['enabled']) is bool and self.maps is not None, 'breakpoint value required')
            spans = [s for s in self.maps['spans'] if s['node'] == request['node']]
            model.require(spans, 'node has no active executable source span')
            line = spans[0]['line']
            # Both instance entrypoints share this authored graph/node identity.
            for asset in ('0000000000000100', '0000000000000101'):
                result = self.runtime.request(self.debug_request('breakpoint', asset=asset, line=line, enabled=request['enabled']))
                self.preview = result
                model.require(result['ok'], result['error'])
        elif action in ('continue', 'into', 'over', 'out'):
            model.closed(request, ('action', 'revision', 'cursor'))
            self.preview = self.runtime.request(self.debug_request(action))
            model.require(self.preview['ok'], self.preview['error'])
        elif action == 'restart':
            model.closed(request, ('action', 'revision', 'cursor'))
            self.runtime.close()
            self.runtime = Native(self.executable)
            self.preview = None
            self.active_document = None
            self.maps = None
            self.runtime_revision = 0
            self.epoch = secrets.token_hex(8)
        else:
            raise model.Invalid('unsupported authoring action')
        return self.state()

    def close(self):
        self.runtime.close()


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def send(self, status, data, mime='application/json'):
        self.send_response(status)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; frame-ancestors 'none'; object-src 'none'")
        self.end_headers()
        self.wfile.write(data)

    def origin(self):
        return 'http://' + self.server.address

    def do_GET(self):
        if self.headers.get('Host') != self.server.address:
            self.send(403, b'{}')
            return
        names = {'/': ('index.html', 'text/html; charset=utf-8'), '/app.js': ('app.js', 'text/javascript'), '/style.css': ('style.css', 'text/css')}
        if self.path not in names:
            self.send(404, b'{}')
            return
        name, mime = names[self.path]
        data = (WEB / name).read_bytes()
        if name == 'index.html': data = data.replace(b'__TOKEN__', self.server.token.encode())
        self.send(200, data, mime)

    def do_POST(self):
        if (self.path != '/action' or self.headers.get('Host') != self.server.address or
            self.headers.get('Origin') != self.origin() or
            self.headers.get('Authorization') != 'Bearer ' + self.server.token or
            self.headers.get('Content-Type') != 'application/json'):
            self.send(403, b'{}')
            return
        try:
            self.connection.settimeout(15)
            length = int(self.headers.get('Content-Length', '0'))
            model.require(0 < length <= model.MAX_BYTES, 'request capacity')
            request = json.loads(self.rfile.read(length), object_pairs_hook=model.unique,
                                 parse_constant=lambda _: (_ for _ in ()).throw(model.Invalid('nonfinite JSON')))
            result = self.server.owner.act(request)
            self.send(200, json.dumps({'ok': True, 'state': result}).encode())
        except (model.Invalid, ValueError, KeyError, OSError, RecursionError, subprocess.SubprocessError) as error:
            self.send(400, json.dumps({'ok': False, 'error': str(error)[:2048], 'state': self.server.owner.state()}).encode())


def serve(path, executable, cache, port=0):
    owner = Owner(path, executable, cache)
    server = http.server.HTTPServer(('127.0.0.1', port), Handler)
    server.address = '127.0.0.1:' + str(server.server_port)
    server.token = secrets.token_urlsafe(32)
    server.owner = owner
    print(json.dumps({'url': 'http://' + server.address}), flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        owner.close()
