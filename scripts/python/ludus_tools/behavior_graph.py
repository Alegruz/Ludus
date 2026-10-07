"""Shared bounded structured sequence document; no general graph VM or text roundtrip.

Stable identities, explicit child order and layout isolation follow Ludus's
scripting architecture, Visual authoring section. The lowering is original;
thanks to Roblox Corporation for the paired strict Luau compiler contract.
"""
import copy
import hashlib
import json
import re
import secrets

MAX_BYTES = 131072
KINDS = ('Sequence', 'Increment', 'If', 'Repeat', 'DoorOpen', 'Return')
BRANCHES = ('Sequence', 'If', 'Repeat')
FIELDS = {'Sequence': set(), 'Increment': {'amount'}, 'If': {'threshold'},
          'Repeat': {'count'}, 'DoorOpen': {'operation'}, 'Return': set()}


class Invalid(ValueError):
    pass


def require(ok, message):
    if not ok:
        raise Invalid(message)


def closed(value, keys):
    require(type(value) is dict and set(value) == set(keys), 'unknown or missing fields')


def integer(value, low, high):
    require(type(value) is int and low <= value <= high, 'integer out of range')


def identity(value):
    require(type(value) is str and re.fullmatch('[0-9a-f]{16}', value) and int(value, 16), 'invalid stable identity')


def unique(pairs):
    value = {}
    for key, item in pairs:
        require(key not in value, 'duplicate JSON field')
        value[key] = item
    return value


def parse(text):
    require(type(text) is str and len(text.encode()) <= MAX_BYTES, 'document capacity')
    def bad_constant(value):
        raise Invalid('nonfinite JSON value: ' + value)
    value = json.loads(text, object_pairs_hook=unique, parse_constant=bad_constant)
    validate(value)
    return value


def canonical(value):
    validate(value)
    result = copy.deepcopy(value)
    result['nodes'].sort(key=lambda n: n['id'])
    result['variables'].sort(key=lambda n: n['id'])
    result['layout']['positions'].sort(key=lambda n: n['node'])
    return json.dumps(result, sort_keys=True, separators=(',', ':'), ensure_ascii=True) + '\n'


def semantic(value):
    result = json.loads(canonical(value))
    del result['layout']
    return result


def digest(value):
    return hashlib.sha256(canonical(value).encode()).hexdigest()


def executable(value):
    return hashlib.sha256(json.dumps(semantic(value), sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def validate(value):
    closed(value, ('version', 'graph', 'root', 'variables', 'nodes', 'layout'))
    integer(value['version'], 1, 1)
    identities = set()
    def claim(item):
        identity(item)
        require(item not in identities, 'duplicate identity')
        identities.add(item)
    claim(value['graph'])
    require(type(value['variables']) is list and len(value['variables']) == 2, 'declared state required')
    fields = set()
    for item in value['variables']:
        closed(item, ('id', 'field', 'type'))
        claim(item['id'])
        require((item['field'], item['type']) in ((1, 'uint32'), (2, 'boolean')) and type(item['field']) is int,
                'state must match the S1 manifest')
        require(item['field'] not in fields, 'duplicate state field')
        fields.add(item['field'])
    require(type(value['nodes']) is list and 1 <= len(value['nodes']) <= 64, 'node capacity')
    nodes = {}
    for node in value['nodes']:
        require(type(node) is dict and type(node.get('kind')) is str and node['kind'] in KINDS, 'unsupported node')
        kind = node['kind']
        closed(node, {'id', 'kind', 'version', 'ports', 'children'} | FIELDS[kind])
        claim(node['id'])
        integer(node['version'], 1, 1)
        closed(node['ports'], ('in', 'out'))
        claim(node['ports']['in'])
        claim(node['ports']['out'])
        require(type(node['children']) is list and len(node['children']) <= 16, 'child capacity')
        require(kind in BRANCHES or not node['children'], 'leaf has children')
        for child in node['children']:
            identity(child)
        if kind == 'Increment':
            require(node['amount'] == 'event' or type(node['amount']) is int, 'typed amount required')
            if node['amount'] != 'event': integer(node['amount'], 1, 10)
        elif kind == 'If': integer(node['threshold'], 1, 10)
        elif kind == 'Repeat': integer(node['count'], 1, 4)
        elif kind == 'DoorOpen': integer(node['operation'], 200, 200)
        nodes[node['id']] = node
    identity(value['root'])
    require(value['root'] in nodes and nodes[value['root']]['kind'] == 'Sequence', 'root must be a Sequence')
    seen = set()
    def visit(key, depth):
        require(key in nodes, 'missing child')
        require(key not in seen, 'cycle or multiply owned node')
        require(depth <= 8, 'block depth capacity')
        seen.add(key)
        node = nodes[key]
        calls = int(node['kind'] == 'DoorOpen')
        for child in node['children']:
            calls += visit(child, depth + 1)
        if node['kind'] == 'Repeat': calls *= node['count']
        require(calls <= 2, 'native command budget exceeds two per invocation')
        return calls
    visit(value['root'], 0)
    require(seen == set(nodes), 'unreachable node')
    closed(value['layout'], ('positions', 'zoom', 'comments'))
    require(type(value['layout']['positions']) is list and len(value['layout']['positions']) <= 64, 'layout capacity')
    positioned = set()
    for position in value['layout']['positions']:
        closed(position, ('node', 'x', 'y'))
        identity(position['node'])
        require(position['node'] in nodes and position['node'] not in positioned, 'invalid layout identity')
        positioned.add(position['node'])
        integer(position['x'], -4096, 4096)
        integer(position['y'], -4096, 4096)
    integer(value['layout']['zoom'], 25, 200)
    require(type(value['layout']['comments']) is str and len(value['layout']['comments'].encode()) <= 2048, 'comment capacity')
    require(len(json.dumps(value).encode()) <= MAX_BYTES, 'document capacity')
    return nodes


def fresh(used):
    while True:
        result = secrets.token_hex(8)
        if int(result, 16) and result not in used:
            used.add(result)
            return result


def apply(value, command):
    """Typed, validated document commands; rejection preserves the caller's draft."""
    nodes = validate(value)
    require(type(command) is dict and type(command.get('kind')) is str, 'typed command required')
    result = copy.deepcopy(value)
    records = {n['id']: n for n in result['nodes']}
    action = command['kind']
    for key in ('node', 'parent'):
        if key in command: identity(command[key])
    if action == 'literal':
        closed(command, ('kind', 'node', 'value'))
        require(command['node'] in records, 'missing edited node')
        node = records[command['node']]
        key = {'Increment': 'amount', 'If': 'threshold', 'Repeat': 'count'}.get(node['kind'])
        require(key is not None, 'node has no editable literal')
        node[key] = command['value']
    elif action == 'move':
        closed(command, ('kind', 'node', 'x', 'y'))
        require(command['node'] in records, 'missing moved node')
        positions = result['layout']['positions']
        positions[:] = [p for p in positions if p['node'] != command['node']]
        positions.append({'node': command['node'], 'x': command['x'], 'y': command['y']})
    elif action == 'order':
        closed(command, ('kind', 'node', 'children'))
        require(command['node'] in records, 'missing parent')
        old = records[command['node']]['children']
        require(type(command['children']) is list, 'ordered children required')
        for child in command['children']: identity(child)
        require(sorted(command['children']) == sorted(old), 'order must preserve children')
        records[command['node']]['children'] = copy.deepcopy(command['children'])
    elif action in ('add', 'paste'):
        closed(command, ('kind', 'parent', 'node' if action == 'paste' else 'type'))
        require(command['parent'] in records and records[command['parent']]['kind'] in BRANCHES, 'parent requires a structured block')
        used = {value['graph']} | {v['id'] for v in value['variables']}
        for node in records.values(): used.update((node['id'], *node['ports'].values()))
        if action == 'add':
            require(type(command['type']) is str and command['type'] in KINDS, 'unsupported node')
            node = {'id': fresh(used), 'kind': command['type'], 'version': 1,
                    'ports': {'in': fresh(used), 'out': fresh(used)}, 'children': []}
            defaults = {'Increment': {'amount': 'event'}, 'If': {'threshold': 2}, 'Repeat': {'count': 2},
                        'DoorOpen': {'operation': 200}}
            node.update(defaults.get(command['type'], {}))
            result['nodes'].append(node)
            key = node['id']
        else:
            require(command['node'] in nodes, 'missing copied subtree')
            def clone(key):
                node = copy.deepcopy(nodes[key])
                node['id'] = fresh(used)
                node['ports'] = {'in': fresh(used), 'out': fresh(used)}
                node['children'] = [clone(child) for child in node['children']]
                result['nodes'].append(node)
                return node['id']
            key = clone(command['node'])
        records[command['parent']]['children'].append(key)
    elif action == 'remove':
        closed(command, ('kind', 'node'))
        require(command['node'] in nodes and command['node'] != value['root'], 'cannot remove root/missing node')
        removed = set()
        def gather(key):
            removed.add(key)
            for child in nodes[key]['children']: gather(child)
        gather(command['node'])
        result['nodes'] = [n for n in result['nodes'] if n['id'] not in removed]
        for node in result['nodes']:
            node['children'] = [c for c in node['children'] if c not in removed]
        result['layout']['positions'] = [p for p in result['layout']['positions'] if p['node'] not in removed]
    else:
        raise Invalid('unsupported document command')
    validate(result)
    return json.loads(canonical(result))


def diff(before, after):
    before, after = (json.loads(canonical(value)) for value in (before, after))
    changes = []
    if before['graph'] != after['graph'] or before['root'] != after['root'] or before['variables'] != after['variables']:
        changes.append({'id': after['graph'], 'field': 'identity/state', 'before': before['root'], 'after': after['root']})
    a, b = ({n['id']: n for n in value['nodes']} for value in (before, after))
    for key in sorted(set(a) | set(b)):
        if key not in a or key not in b:
            changes.append({'id': key, 'field': 'node', 'before': a.get(key), 'after': b.get(key)})
        else:
            for field in sorted(set(a[key]) | set(b[key])):
                if a[key].get(field) != b[key].get(field):
                    changes.append({'id': key, 'field': 'execution order' if field == 'children' else field,
                                    'before': a[key].get(field), 'after': b[key].get(field)})
    return {'semantic': changes, 'layout': before['layout'] != after['layout']}


def merge(base, local, remote):
    """Three-way field merge by stable identity; order/deletion conflicts are explicit."""
    base, local, remote = (json.loads(canonical(value)) for value in (base, local, remote))
    conflicts = []
    absent = object()
    def choose(a, b, c, path):
        if b == c: return absent if b is absent else copy.deepcopy(b)
        if b == a: return absent if c is absent else copy.deepcopy(c)
        if c == a: return absent if b is absent else copy.deepcopy(b)
        if all(type(v) is dict for v in (a, b, c)):
            result = {}
            for key in sorted(set(a) | set(b) | set(c)):
                selected = choose(a.get(key, absent), b.get(key, absent), c.get(key, absent), path + '/' + key)
                if selected is not absent: result[key] = selected
            return result
        conflicts.append(path)
        return absent
    def keyed(values, key, path):
        maps = [{n[key]: n for n in items} for items in values]
        output = []
        for item in sorted(set(maps[0]) | set(maps[1]) | set(maps[2])):
            selected = choose(*(m.get(item, absent) for m in maps), path + '/' + item)
            if selected is not absent: output.append(selected)
        return output
    result = choose(*({k: v for k, v in value.items() if k not in ('nodes', 'layout')} for value in (base, local, remote)), 'document')
    nodes = keyed([v['nodes'] for v in (base, local, remote)], 'id', 'nodes')
    positions = keyed([v['layout']['positions'] for v in (base, local, remote)], 'node', 'layout/positions')
    layout = choose(*({k: v for k, v in value['layout'].items() if k != 'positions'} for value in (base, local, remote)), 'layout')
    require(not conflicts, 'merge conflict: ' + ', '.join(conflicts))
    result['nodes'] = nodes
    layout['positions'] = positions
    result['layout'] = layout
    validate(result)
    return json.loads(canonical(result))


def lower(value, event_type="Interact"):
    require(event_type in ("Interact", "Event"), "unsupported event type")
    """Generate strict Luau and exact graph/node/port spans, in authored child order."""
    nodes = validate(value)
    lines = ['--!strict', 'return function(config: Config, state: State, event: '+event_type+', api: Api)']
    maps = []
    def emit(node, text, depth=0, port='in'):
        lines.append('    ' * (depth + 1) + text)
        maps.append({'line': len(lines), 'graph': value['graph'], 'node': node['id'], 'port': node['ports'][port]})
    def block(key, depth):
        node = nodes[key]
        kind = node['kind']
        if kind == 'Increment':
            amount = 'event.Amount' if node['amount'] == 'event' else str(node['amount'])
            emit(node, 'state.Interactions += ' + amount, depth)
        elif kind in ('If', 'Repeat'):
            text = ('if state.Interactions >= ' + str(node['threshold']) + ' and not state.OpenRequested then') if kind == 'If' else ('for _ = 1, ' + str(node['count']) + ' do')
            emit(node, text, depth)
            for child in node['children']: block(child, depth + 1)
            emit(node, 'end', depth, 'out')
        elif kind == 'Sequence':
            for child in node['children']: block(child, depth)
        elif kind == 'DoorOpen':
            suffix = node['id']
            emit(node, 'local result_' + suffix + ' = api.RequestDoorOpen(event.Target, event.Amount)', depth)
            emit(node, 'if result_' + suffix + '.Status == "Accepted" then', depth)
            emit(node, 'state.OpenRequested = true', depth + 1)
            emit(node, 'end', depth, 'out')
        elif kind == 'Return':
            # do/end makes a structured early return legal before later siblings.
            emit(node, 'do return end', depth)
    block(value['root'], 0)
    lines.append('end')
    return '\n'.join(lines) + '\n', maps


class Document:
    """One draft owner. Only accepted typed commands enter bounded undo history."""
    def __init__(self, value):
        self.draft = json.loads(canonical(value))
        self.saved = copy.deepcopy(self.draft)
        self.revision = 1
        self.undo = []
        self.redo = []

    def commit(self, value, command):
        validate(value)
        if value == self.draft: return
        self.undo.append((copy.deepcopy(self.draft), copy.deepcopy(value), copy.deepcopy(command)))
        self.undo = self.undo[-64:]
        self.redo.clear()
        self.draft = value
        self.revision += 1

    def command(self, value):
        self.commit(apply(self.draft, value), value)

    def history(self, redo=False):
        source, target = (self.redo, self.undo) if redo else (self.undo, self.redo)
        require(source, 'no history')
        record = source.pop()
        target.append(record)
        self.draft = copy.deepcopy(record[1 if redo else 0])
        self.revision += 1
