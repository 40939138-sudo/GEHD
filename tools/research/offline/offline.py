"""Bound, read-only replay of saved observation receipts. No live game access.

Bindings identify evidence; they do not qualify it. No historical result file is
used as an oracle. All support is scoped to the recorded non-atomic interval.
"""
from pathlib import Path
import argparse
import collections
import hashlib
import importlib
import json
import os
import stat
import sys

SCHEMA = 'GEHD_OFFLINE_OBSERVATION_BINDING_V1'
RESULT_SCHEMA = 'GEHD_OFFLINE_OBSERVATION_RESULT_V1'
CODE_FILES = ('offline.py', 'geometry.py', 'projection.py')
MAX_FILE = 64 * 1024 * 1024
COMMON = ('resourceId', 'generation', 'contentRevision', 'contentIdentitySha256',
          'pixelShaderSha256')


class Invalid(ValueError):
    pass


class Insufficient(ValueError):
    pass


def require(value, reason):
    if not value:
        raise Invalid(reason)


def digest(raw):
    return hashlib.sha256(raw).hexdigest().upper()


def _unique(pairs):
    result = {}
    for key, value in pairs:
        # Historical observer emitters repeat some fields identically. Preserve
        # that narrow compatibility, but never silently choose a conflicting value.
        if key in result:
            require(key == 'pixelShaderSha256' and type(result[key]) is type(value) and result[key] == value,
                    'conflicting_duplicate_json_key:' + key)
        result[key] = value
    return result


def parse(raw):
    return json.loads(raw, object_pairs_hook=_unique,
                      parse_constant=lambda x: (_ for _ in ()).throw(Invalid('nonfinite_json')))


def absolute(value):
    p = Path(value)
    require(p.is_absolute(), 'absolute_path_required')
    require('..' not in p.parts, 'parent_path_not_allowed')
    # Reject reparse points on every existing ancestor, including junctions.
    for node in reversed((p, *p.parents)):
        try:
            s = node.lstat()
        except FileNotFoundError:
            continue
        require(not stat.S_ISLNK(s.st_mode) and
                not getattr(s, 'st_file_attributes', 0) & 0x400, 'reparse_path')
    return p.resolve()


def inside(p, root):
    return p == root or root in p.parents


def child(root, relative):
    require(isinstance(relative, str) and relative != '', 'relative_path_required')
    rel = Path(relative)
    require(not rel.is_absolute() and '..' not in rel.parts and ':' not in relative,
            'relative_path_escape')
    p = absolute(root / rel)
    require(inside(p, root) and p != root, 'path_escape')
    return p


def read_bytes(p):
    p = absolute(p)
    before = p.stat()
    require(stat.S_ISREG(before.st_mode) and before.st_size <= MAX_FILE, 'file_bounds')
    data = p.read_bytes()
    after = p.stat()
    require((before.st_size, before.st_mtime_ns, before.st_ino) ==
            (after.st_size, after.st_mtime_ns, after.st_ino) and len(data) == before.st_size,
            'input_changed_during_read')
    return data


def entry(p, root):
    require(inside(p, root), 'evidence_escape')
    raw = read_bytes(p)
    return {'path': p.relative_to(root).as_posix(), 'bytes': len(raw), 'sha256': digest(raw)}


def new_path(output, activity, code, protected=(), extra_areas=()):
    p = absolute(output)
    for area in (activity, code, *extra_areas):
        require(not inside(p, area) and not inside(area, p), 'output_input_overlap')
    require(p not in protected, 'output_is_input')
    require(not p.exists(), 'output_exists')
    require(p.parent.is_dir(), 'output_parent_missing')
    return p


def dump_new(p, value):
    with p.open('x', encoding='utf-8', newline='\n') as f:
        json.dump(value, f, ensure_ascii=False, indent=2, allow_nan=False)
        f.write('\n')


def log_rows(raw):
    rows = []
    for line in raw.splitlines():
        if line.strip():
            row = parse(line)
            require(isinstance(row, dict), 'jsonl_object_required')
            rows.append((line, row))
    require(rows, 'empty_log')
    return rows


def bind(activity, evidence, code, mode, scope, binary, output):
    activity, evidence, code = map(absolute, (activity, evidence, code))
    binary = absolute(binary)
    require(scope in ('saved-observation', 'synthetic-observation'), 'scope')
    require(mode in ('O', 'D'), 'mode')
    require(inside(activity, evidence) and inside(binary, evidence), 'evidence_root')
    require(binary.name == 'd3d9.dll', 'observer_binary_name')
    output = new_path(output, activity, code, extra_areas=(binary.parent,))
    pre = parse(read_bytes(child(activity, 'preflight.json')))
    require(pre.get('sourceEnabled') is (mode == 'D'), 'mode_preflight_mismatch')
    require(pre.get('observerSha256') == digest(read_bytes(binary)), 'observer_binary_identity')
    expected = pre.get('expectedReplacementContentIdentity' if mode == 'D'
                       else 'expectedOriginalContentIdentity')
    require(isinstance(expected, str) and len(expected) == 64, 'expected_content_identity')
    files = {}
    for name in ('preflight.json', 'install-plan.json', 'observer.jsonl'):
        files[name] = entry(child(activity, name), evidence)
    files['observer-binary'] = entry(binary, evidence)
    for name in ('scene-request.json', 'closeout.json', 'flush-result.json', 'removed.json',
                 'frida/result.json', 'frida/profile.json'):
        p = child(activity, name)
        if p.exists():
            files[name] = entry(p, evidence)
    for name in pre.get('tools', {}):
        require(Path(name).name == name, 'tool_relative_name')
        files['tool:' + name] = entry(child(activity, name), evidence)
    # Only image receipts in raw log authorize image reads. No EXE/game/package
    # path from preflight is opened, nor is an old result/triangle file imported.
    for _, row in log_rows(read_bytes(child(activity, 'observer.jsonl'))):
        receipts = []
        if row.get('event') == 'scene_capture_image':
            receipts.append(row)
        if row.get('event') == 'scene_capture_target_image':
            receipts.append(row.get('target', {}))
        if row.get('event') == 'scene_capture_draw_pair':
            receipts.extend(row.get('pair', {}).get(k, {}) for k in ('before', 'after'))
        for r in receipts:
            if r.get('fileSaved') and r.get('file'):
                f = child(activity, r['file'])
                require(f.suffix in ('.bin', '.bgra'), 'image_extension')
                files['image:' + r['file']] = entry(f, evidence)
    helpers = {n: entry(child(code, n), code) for n in CODE_FILES}
    binding = {'schema': SCHEMA, 'scope': scope, 'mode': mode,
               'activity': str(activity), 'evidenceRoot': str(evidence),
               'codeRoot': str(code), 'files': files, 'code': helpers,
               'expectedContentIdentity': expected,
               'purpose': 'READ_ONLY_SAVED_RECEIPTS_NO_LIVE_GAME',
               'limits': ['Binding is an identity inventory, not successful qualification.',
                          'No current installed game or runtime state is checked.']}
    # Detect mutations while inventory was being assembled.
    for value in files.values():
        raw = read_bytes(child(evidence, value['path']))
        require(len(raw) == value['bytes'] and digest(raw) == value['sha256'], 'bind_input_changed')
    dump_new(output, binding)
    return binding


class Reader:
    def __init__(self, binding):
        self.b = binding
        self.evidence = absolute(binding['evidenceRoot'])
        self.activity = absolute(binding['activity'])
        self.code = absolute(binding['codeRoot'])
        require(inside(self.activity, self.evidence), 'activity_escape')
        self.used = {}
        self.failures = {}
        require(set(binding['code']) == set(CODE_FILES), 'code_manifest')
        require(self.code == Path(__file__).resolve().parent, 'wrong_code_root')
        for name, expected in binding['code'].items():
            require(expected['path'] == name, 'code_path')
            self.verify(child(self.code, name), expected)
        # Validate all manifest locations before reading any evidence.
        fixed = {'preflight.json', 'install-plan.json', 'observer.jsonl', 'scene-request.json',
                 'closeout.json', 'flush-result.json', 'removed.json', 'frida/result.json',
                 'frida/profile.json'}
        for role, expected in binding['files'].items():
            p = child(self.evidence, expected['path'])
            if role == 'observer-binary':
                require(p.name == 'd3d9.dll', 'binary_role')
            elif role in fixed:
                require(p == child(self.activity, role), 'fixed_role_path')
            elif role.startswith('tool:'):
                name = role[5:]
                require(Path(name).name == name and p == child(self.activity, name), 'tool_role')
            elif role.startswith('image:'):
                require(p == child(self.activity, role[6:]) and p.suffix in ('.bin', '.bgra'), 'image_role')
            else:
                raise Invalid('unknown_file_role:' + role)

    @staticmethod
    def verify(p, expected):
        raw = read_bytes(p)
        require(len(raw) == expected['bytes'] and digest(raw) == expected['sha256'], 'file_identity:' + str(p))
        return raw

    def raw(self, role):
        if role not in self.b['files']:
            raise Insufficient('missing_bound_file:' + role)
        expected = self.b['files'][role]
        try:
            raw = self.verify(child(self.evidence, expected['path']), expected)
            self.used[role] = expected
            return raw
        except (ValueError, OSError) as e:
            self.failures[role] = str(e)
            raise Invalid(str(e)) from e

    def json(self, role):
        return parse(self.raw(role))

    def unchanged(self):
        for expected in self.used.values():
            self.verify(child(self.evidence, expected['path']), expected)


def layer(fn):
    try:
        return {'status': 'SUPPORTED', **fn()}
    except Insufficient as e:
        return {'status': 'INSUFFICIENT', 'reason': str(e)}
    except (Invalid, KeyError, TypeError, ValueError, IndexError, OSError) as e:
        return {'status': 'INVALID', 'reason': str(e)}


def one(items, name):
    if not items:
        raise Insufficient('missing:' + name)
    require(len(items) == 1, 'ambiguous:' + name)
    return items[0]


def analyze(binding_path, output):
    binding_path = absolute(binding_path)
    binding_raw = read_bytes(binding_path)
    b = parse(binding_raw)
    require(b['schema'] == SCHEMA and b['scope'] in ('saved-observation', 'synthetic-observation'), 'binding_schema')
    require(b['mode'] in ('O', 'D'), 'mode')
    reader = Reader(b)
    binary_parent = child(reader.evidence, b['files']['observer-binary']['path']).parent
    output = new_path(output, reader.activity, reader.code, (binding_path,), (binary_parent,))
    geometry = importlib.import_module('geometry')
    projection_module = importlib.import_module('projection')
    require(absolute(getattr(geometry, '__file__', '')) == child(reader.code, 'geometry.py') and
            absolute(getattr(projection_module, '__file__', '')) == child(reader.code, 'projection.py') and
            geometry.clip_points is projection_module.clip_points, 'loaded_helper_identity')
    results = {}
    state = {}

    def identity():
        pre = reader.json('preflight.json')
        plan = reader.json('install-plan.json')
        require(pre.get('sourceEnabled') is (b['mode'] == 'D') and
                plan.get('sourceEnabled') is (b['mode'] == 'D'), 'mode_binding_mismatch')
        expected = pre.get('expectedReplacementContentIdentity' if b['mode'] == 'D'
                           else 'expectedOriginalContentIdentity')
        require(expected == b['expectedContentIdentity'], 'expected_content_binding')
        require(digest(reader.raw('observer-binary')) == pre['observerSha256'] ==
                plan['files']['d3d9.dll']['sha256'], 'build_identity')
        for name, h in pre.get('tools', {}).items():
            require(digest(reader.raw('tool:' + name)) == h, 'tool_identity:' + name)
        state.update(pre=pre, plan=plan)
        return {'observerSha256': pre['observerSha256'], 'mode': b['mode']}

    results['identity'] = layer(identity)

    def events():
        rows = log_rows(reader.raw('observer.jsonl'))
        state['rows'] = rows
        return {'records': len(rows)}

    results['log'] = layer(events)

    def ev(name):
        if 'rows' not in state:
            raise Insufficient('invalid_or_missing_log')
        return [r for _, r in state['rows'] if r.get('event') == name]

    def selection():
        require(results['identity']['status'] == 'SUPPORTED', 'identity_not_supported')
        start = one(ev('observer_start'), 'observer_start')
        require(start['pointerBytes'] == 4 and type(start['pid']) is int, 'process_architecture')
        modules = one(ev('named_module_binding'), 'module_binding')
        require(any(m.get('role') == 'observer' and m.get('fileHashKnown') and
                    m.get('fileSha256') == state['pre']['observerSha256']
                    for m in modules['modules']), 'loaded_observer_identity')
        target = one(ev('scene_capture_draw_pair') + ev('scene_capture_target_image'), 'selected_target')
        selected = one([r for r in ev('scene_capture_draw') if
                        (r.get('frame'), r.get('drawOrdinal')) ==
                        (target.get('frame'), target.get('drawOrdinal'))], 'selected_draw')
        state.update(target=target, selected=selected, ctx=selected['context'])
        return {'frame': selected['frame'], 'drawOrdinal': selected['drawOrdinal']}

    results['selection'] = layer(selection)

    def content():
        if 'ctx' not in state:
            raise Insufficient('selection_unavailable')
        ctx = state['ctx']
        relation = one([r for r in ev('resource_draw_relation_checkpoint')
                        if r.get('indexedDrawOrdinal') == state['selected']['drawOrdinal']
                        and r.get('lastFrame') == state['selected']['frame']], 'draw_relation')
        require(all(relation[k] == ctx[k] for k in COMMON), 'content_draw_join')
        require(ctx['contentIdentitySha256'] == b['expectedContentIdentity'], 'content_identity')
        require(relation['currentContentWitness'] and relation['completeMipChain'] and
                relation['expectedLevelCount'] == relation['populatedLevelCount'] and
                type(relation['expectedLevelCount']) is int and relation['expectedLevelCount'] > 0,
                'incomplete_content')
        state['relation'] = relation
        return {'identity': ctx['contentIdentitySha256'], 'levels': relation['expectedLevelCount'],
                'scope': 'RECORDED_COMPLETE_CONTENT_WITNESS_NOT_PIXEL_PROOF'}

    results['content'] = layer(content)

    def source():
        if 'ctx' not in state:
            raise Insufficient('selection_unavailable')
        ctx, pre = state['ctx'], state['pre']
        configuration = one(ev('source_configuration'), 'source_configuration')
        substitutions = [r for r in ev('d3dx_source_replacement') if r.get('substituted')]
        if b['mode'] == 'O':
            require(configuration['status'] == 'DISABLED' and not substitutions and
                    ctx['sourceLoadOrdinal'] == 0 and not ctx['sourceObservationStable'], 'original_mode_conflict')
            if 'relation' in state:
                require(state['relation']['sourceLoadOrdinal'] == 0 and
                        not state['relation']['sourceWasSubstituted'], 'original_relation_conflict')
            return {'status': 'NOT_APPLICABLE', 'reason': 'DISABLED_ORIGINAL_CONTENT_HAS_NO_CALLER_RECEIPT'}
        require(configuration['status'] == 'ARMED' and
                configuration['replacementSha256'] == pre['replacementSha256'], 'source_configuration')
        origin = one([r for r in substitutions if r.get('ordinal') == ctx['sourceLoadOrdinal']], 'source')
        require(origin['hresult'] == 0 and origin['sourceLinkBound'] and origin['callerVerified'], 'source_success')
        require(origin['callerRva'] == pre['profilePreflight']['profile']['returnRva'] and
                origin['originalInputSha256'] == pre['profilePreflight']['profile']['sourceSha256'] == ctx['sourceOriginalInputSha256'] and
                origin['effectiveDdsSha256'] == pre['replacementSha256'] and
                origin['returnedContentIdentitySha256'] == ctx['sourceReturnedContentIdentity'] == b['expectedContentIdentity'] and
                origin['returnedResourceId'] == ctx['resourceId'] and ctx['sourceObservationStable'], 'source_join')
        return {'sourceLoadOrdinal': origin['ordinal'], 'sourceSha256': origin['originalInputSha256']}

    results['source'] = layer(source)

    def draw():
        require(results['content']['status'] == 'SUPPORTED', 'content_not_supported')
        require(results['source']['status'] in ('SUPPORTED', 'NOT_APPLICABLE'), 'source_mode_not_supported')
        selected, ctx, target = state['selected'], state['ctx'], state['target']
        require(state['relation']['drawHresult'] == 0, 'draw_failed')
        require(ctx['pixelShaderSha256'] == state['pre']['expectedShaderSha256'] and
                ctx['stage'] == 0 and ctx['samplingClass'] == 'SAMPLING_ELIGIBLE', 'selected_shader_sampler')
        image = one(ev('scene_capture_image'), 'present')
        arm = one(ev('scene_capture_armed'), 'scene_arm')
        require(reader.json('scene-request.json')['requestSignaled'] and
                image['frame'] == arm['afterFrame'] + 1 and not ev('scene_capture_stop'), 'capture_window')
        terminal = one(ev('generic_runtime_terminal_snapshot'), 'terminal')
        require(terminal['terminalReason'] == 'RUNNER_PRE_CLOSE_REQUEST' and
                terminal['outputOverflowBeforeTerminal'] == terminal['outputWriteFailuresBeforeTerminal'] == 0 and
                terminal['frame'] > image['frame'], 'lossy_or_incomplete_log')
        draws = ev('scene_capture_draw')
        require(image['drawOrdinals'] == [r['drawOrdinal'] for r in draws] and
                len(set(image['drawOrdinals'])) == len(draws) and
                all((r['frame'], r['captureId'], r['deviceId']) ==
                    (image['frame'], image['captureId'], image['deviceId']) for r in draws), 'frame_join')
        require(image['frame'] == target['frame'] == selected['frame'], 'selected_frame')
        context = ctx['renderTargetContext']
        slot = one([r for r in context['targets'] if r.get('slot') == 0], 'RT0_descriptor')
        require(context['scopeStillCurrent'] and slot['identityKnown'] and
                slot['getHresult'] == slot['identityHresult'] == slot['descHresult'] == 0,
                'retained_target_descriptor')
        if target['event'] == 'scene_capture_draw_pair':
            keys = (*COMMON, 'stage', 'sourceLoadOrdinal', 'sourceOriginalInputSha256',
                    'sourceReturnedContentIdentity')
            require(all(target['selection'][k] == ctx[k] for k in keys), 'pair_selection')
            require(type(target['selection']['writeStamp']) is int and target['selection']['writeStamp'] >= 0,
                    'pair_write_stamp_missing')
            if 'writeStamp' in ctx:
                require(target['selection']['writeStamp'] == ctx['writeStamp'], 'pair_write_stamp')
            require(target['scope'] == context['scope'], 'pair_target_scope')
            for phase in ('before', 'after'):
                receipt = target['pair'][phase]
                require(all(receipt[k] == slot[k] for k in ('width', 'height', 'format')) and
                        slot['multisampleType'] == 0, 'pair_descriptor_to_RT0')
        else:
            rt = target['target']
            require(target['windowStillCurrent'] and rt['scopeStillCurrent'] and
                    rt['scope'] == context['scope'] and rt['surfaceId'] == slot['surfaceId'] and
                    rt['slot'] == slot['slot'] == 0, 'single_target_join')
            require(all(rt[k] == slot[k] for k in ('width', 'height', 'format')) and
                    slot['multisampleType'] == 0, 'single_descriptor_to_RT0')
        state['present'] = image
        return {'frame': selected['frame'], 'drawOrdinal': selected['drawOrdinal'],
                'resourceId': ctx['resourceId'], 'pixelContributionProven': False}

    results['draw'] = layer(draw)

    def query():
        if 'target' not in state or 'occlusion' not in state['target']:
            raise Insufficient('no_query_receipt')
        require(results['draw']['status'] == 'SUPPORTED', 'draw_not_supported')
        require(state['target']['observedDrawInterleavingChecked'], 'query_interleaving_guard')
        q = state['target']['occlusion']
        if not q.get('complete'):
            require(q.get('samples') is None, 'incomplete_query_has_samples')
            raise Insufficient('query_not_complete:' + str(q.get('failure')))
        require(q['attempted'] and q['failure'] == 'NONE' and type(q['samples']) is int and q['samples'] >= 0,
                'query_receipt')
        require(all(q[k] == 0 for k in ('createHresult', 'beginHresult', 'drawHresult', 'endHresult', 'getDataHresult')) and
                q['getDataCalls'] == 1 and q['getDataFlags'] == 0 and
                q['timing'] == 'SINGLE_READ_AFTER_EXISTING_POST_RT0_READBACK', 'query_interval')
        return {'samples': q['samples'], 'allDrawsExcluded': False, 'pixelContributionProven': False}

    results['query'] = layer(query)

    def image_receipt(r):
        require(r['fileSaved'] and r['failure'] == 'NONE' and
                r.get('captureHresult', r.get('hresult')) == 0, 'image_capture')
        require(type(r['width']) is int and type(r['height']) is int and
                0 < r['width'] <= 16384 and 0 < r['height'] <= 16384 and r['format'] == 21,
                'image_descriptor')
        require(r['layout'] in ('BGRA8_UNORM', 'BGRA8_TOP_DOWN_PACKED') and
                r.get('rowBytes', r['width'] * 4) == r['width'] * 4, 'image_layout')
        raw = reader.raw('image:' + r['file'])
        require(len(raw) == r['bytes'] == r['width'] * r['height'] * 4 and digest(raw) == r['sha256'].upper(), 'image_identity')
        return raw

    def images():
        target = one(ev('scene_capture_draw_pair') + ev('scene_capture_target_image'), 'selected_target')
        image = one(ev('scene_capture_image'), 'present')
        present = image_receipt(image)
        require(image['complete'] and image['presentHresult'] == 0 and
                image['sourceRect'] is None and image['destRect'] is None and
                not image['overrideWindow'] and not image['dirtyRegion'], 'present_scope')
        value = {'presentSha256': digest(present), 'pixelContributionProven': False,
                 'sameDrawAssociationSupported': results['draw']['status'] == 'SUPPORTED'}
        if target['event'] == 'scene_capture_draw_pair':
            p = target['pair']
            require(target['observedDrawInterleavingChecked'], 'pair_interleaving_guard')
            require(p['attempted'] and p['complete'] and p['sameRetainedSurface'] and
                    p['currentBefore'] and p['currentAfter'] and p['failure'] == 'NONE' and
                    p['drawHresult'] == 0 and
                    p['timing'] == 'SYNCHRONOUS_READBACK_AROUND_ORIGINAL_DRAW_NONATOMIC', 'pair_scope')
            require(all(p[k] == 0 for k in ('getBeforeHresult', 'getAfterHresult',
                                          'identityBeforeHresult', 'identityAfterHresult')), 'pair_hresult')
            before, after = image_receipt(p['before']), image_receipt(p['after'])
            require((p['before']['width'], p['before']['height']) ==
                    (p['after']['width'], p['after']['height']), 'pair_dimensions')
            require(len(before) == len(after), 'pair_lengths')
            value.update(kind='RT0_PAIR', changedBytes=sum(a != c for a, c in zip(before, after)),
                         changedPixels=sum(before[i:i+4] != after[i:i+4] for i in range(0, len(before), 4)),
                         beforeSha256=digest(before), afterSha256=digest(after), allWritersExcluded=False)
        else:
            r = target['target']
            require(r['complete'] and r['scopeStillCurrent'] and target['windowStillCurrent'], 'single_target_scope')
            raw = image_receipt(r)
            value.update(kind='SINGLE_RT0', targetSha256=digest(raw), changedPixels=None,
                         reason='NO_BEFORE_AFTER_PAIR')
        return value

    results['images'] = layer(images)

    def geom():
        require(results['draw']['status'] == 'SUPPORTED', 'draw_not_supported')
        ctx = state['ctx']
        if not ctx.get('selectedCanonical'):
            raise Insufficient('canonical_not_recorded')
        record = ctx['selectedCanonical']
        data = geometry.decode(record)
        g = ctx['indexedObjectGeometry']
        observed = g.get('observedSourceGeometry', g)
        require(record['bytes'] == observed['canonicalBytes'] and
                record['sha256'] == observed['orderedAttributesSha256'] and
                data['indexCount'] == observed['primitiveCount'] * 3 and
                data['fieldMask'] == observed['fieldMask'], 'geometry_receipt_join')
        require(record['fromUploadObservation'] and not g['contentAdopted'] and
                not record['contentAdopted'], 'geometry_adoption_scope')
        state.update(canonical=record, geometry=data)
        return {'triangleCount': data['indexCount'] // 3, 'sha256': record['sha256'],
                'geometryAdopted': False}

    results['geometry'] = layer(geom)

    def projection():
        if results['geometry']['status'] != 'SUPPORTED':
            raise Insufficient('geometry_unavailable')
        fr, profile = reader.json('frida/result.json'), reader.json('frida/profile.json')
        states = [r for r in fr['payloads'] if r.get('kind') == 'selected_state_boundary']
        require(len(states) == 2 and [s['phase'] for s in states] == [1, 2], 'boundary_pair')
        before, after = states
        raw, target = one([(raw, r) for raw, r in state['rows'] if r.get('event') == 'scene_capture_draw_pair'], 'raw_pair')
        require(after['payload'] == target and after['rawPayloadSha256'] == digest(raw), 'raw_boundary_join')
        require(all(before['payload'][k] == target[k] for k in ('frame', 'drawOrdinal', 'scope', 'selection')) and
                before['payload']['windowValid'] and before['thread'] == after['thread'] and
                before['snapshot'] == after['snapshot'], 'state_boundary_join')
        require(all(s['pendingStateCalls'] == 0 for s in states), 'pending_state_calls')
        start = one(ev('observer_start'), 'observer_start')
        require(profile['pid'] == start['pid'] and fr['moduleSha256'] == fr['moduleAfterSha256'] ==
                state['pre']['profilePreflight']['profile']['executableSha256'], 'frida_process_identity')
        require(profile['moduleSha256'] == fr['moduleSha256'], 'frida_profile_module')
        require(fr['templateSha256'] == state['pre']['stateAdapterInputs']['observer.js'] and
                fr['runnerSha256'] == state['pre']['stateAdapterInputs']['run.mjs'] and
                profile['bridge']['sha256'] == state['pre']['observerSha256'], 'frida_adapter_identity')
        require(profile['selection']['contentSha256'] == b['expectedContentIdentity'] and
                profile['selection']['pixelShaderSha256'] == state['pre']['expectedShaderSha256'], 'frida_selection_identity')
        if b['mode'] == 'D':
            require(profile['selection']['sourceSha256'] == state['pre']['profilePreflight']['profile']['sourceSha256'], 'frida_source_identity')
        require(any(p.get('kind') == 'owner_verified' and p.get('sha256') == state['pre']['observerSha256']
                    for p in fr['payloads']), 'frida_observer_owner')
        require(fr['status'] == 'COMPLETE' and fr['captureValid'] and fr['detached'] and fr['clientClosed'] and
                fr['referenceCleanupConfirmed'] and not fr['terminal']['pending'] and
                fr['terminal']['draws'] == 1 and fr['terminal']['events'] <= profile['maxEvents'] and
                len(fr['terminal']['cleanup']) == 2 and all(v['released'] for v in fr['terminal']['cleanup']) and
                fr['observation']['normalized_result']['messages_truncated'] is False, 'frida_capture_incomplete')
        w = before['snapshot']['lastSuccessfulWrites']
        if not w.get('vertexShader') or w['vertexShader'].get('isNull'):
            raise Insufficient('vertex_shader_missing')
        require(w['vertexShader']['thread'] == before['thread'] and
                all(v['thread'] == before['thread'] for v in w['vertexConstants'].values()), 'setter_thread')
        projected = geometry.project(state['canonical'], w['vertexShader'], w['vertexConstants'])
        clips = geometry.clip_points(w['vertexShader'], w['vertexConstants'], state['geometry']['positions'])
        planes = ('left', 'right', 'bottom', 'top', 'near', 'far')
        groups, margins = collections.Counter(), []
        for i in range(0, len(clips), 3):
            points = clips[i:i+3]
            ds = [geometry.distances(v) for v in points]
            eps = max(1.0, max(abs(v) for p in points for v in p)) * 1e-6
            outside = {name: -max(d[n] for d in ds) for n, name in enumerate(planes)
                       if max(d[n] for d in ds) < -eps}
            groups[','.join(outside) or 'no_single_plane'] += 1
            margins.append({'ordinal': i // 3, 'outsidePlaneMargins': outside, 'tolerance': eps})
        return {'triangleCount': projected['triangleCount'], 'counts': projected['counts'],
                'triangles': projected['triangles'], 'planeGroups': dict(groups),
                'planeMargins': margins, 'geometryAdopted': False, 'currentStateProven': False,
                'pixelContributionProven': False, 'arithmetic': projected['arithmetic']}

    results['projection'] = layer(projection)
    reader.unchanged()
    require(read_bytes(binding_path) == binding_raw, 'binding_changed')
    result = {'schema': RESULT_SCHEMA, 'scope': b['scope'], 'mode': b['mode'],
              'status': 'REPLAY_COMPLETE' if all(v['status'] in ('SUPPORTED', 'NOT_APPLICABLE') for v in results.values()) else 'REPLAY_PARTIAL',
              'bindingSha256': digest(binding_raw), 'layers': results,
              'readInputs': reader.used, 'identityFailures': reader.failures,
              'geometryAdopted': False, 'currentStateProven': False, 'pixelContributionProven': False,
              'gameExecuted': False, 'surfaceAccepted': False,
              'limits': ['A bound saved receipt is not current GPU state or a game deployment qualification.',
                         'Query and RT0 differences describe a non-atomic recorded interval, not texture causality.',
                         'Other MRTs, all writers, complete state, visible recovery and HD quality are not verified.',
                         'Historical result/check files were not used as acceptance oracles.',
                         'Only identical duplicate pixelShaderSha256 keys are accepted for the historical emitter; all other duplicates/conflicting values are rejected.',
                         'If selected draw lacks writeStamp, only pair/boundary consistency is available, not an independent draw write-stamp witness.']}
    output.mkdir()
    dump_new(output / 'result.json', result)
    return result


def plan_activity(reference_binding, activity, mode, expected_content, output):
    """Prepare an explicit input/check contract; never inspect a live process."""
    reference_binding = absolute(reference_binding)
    raw = read_bytes(reference_binding)
    b = parse(raw)
    require(b['schema'] == SCHEMA and mode in ('O', 'D'), 'plan_binding')
    reader = Reader(b)
    activity = absolute(activity)
    require(inside(activity, reader.evidence) and not activity.exists(), 'fresh_activity_required')
    binary_parent = child(reader.evidence, b['files']['observer-binary']['path']).parent
    require(not any(inside(activity, area) or inside(area, activity)
                    for area in (reader.activity, reader.code, binary_parent)), 'activity_overlap')
    require(isinstance(expected_content, str) and len(expected_content) == 64 and
            all(c in '0123456789abcdefABCDEF' for c in expected_content), 'expected_content_sha256')
    output = new_path(output, reader.activity, reader.code, (reference_binding,), (binary_parent,))
    require(not inside(output, activity), 'plan_output_inside_future_activity')
    pre = reader.json('preflight.json')
    require(digest(reader.raw('observer-binary')) == pre['observerSha256'], 'plan_build_identity')
    spec = {'schema': 'GEHD_OFFLINE_ACTIVITY_SPEC_V1', 'status': 'PENDING_LIVE_ENVIRONMENT_CHECKS',
            'activity': str(activity), 'evidenceRoot': str(reader.evidence), 'codeRoot': str(reader.code),
            'referenceBindingSha256': digest(raw), 'mode': mode,
            'observerSha256': pre['observerSha256'], 'expectedContentIdentity': expected_content.upper(),
            'expectedShaderSha256': pre['expectedShaderSha256'], 'sourceEnabled': mode == 'D',
            'runtime': {'pid': None, 'frame': None, 'drawOrdinal': None, 'window': None},
            'requiredSavedInputs': ['preflight.json', 'install-plan.json', 'observer.jsonl', 'scene-request.json',
                                    'image receipts with complete raw files', 'bound observer and tools'],
            'conditionalInputs': {'projection': ['frida/result.json', 'frida/profile.json', 'selected canonical bytes'],
                                  'D': ['exact source profile, substitution and returned content receipts'],
                                  'O': ['replacement disabled, original full content identity; no D source receipt required']},
            'pendingChecks': ['Fresh game/module/owner identities and deployment ownership',
                              'Usable window and reproducible view; current resource/draw identities',
                              'Fresh output, save backup and environment preflight at activity time',
                              'REA client/runtime/helper binding and readiness before scene event if projection is requested',
                              'Explicit flush, normal exit, owned removal and declared original/save audit'],
            'gameExecuted': False, 'deploymentQualified': False,
            'limits': ['This specification does not deploy, connect REA, start a process or reuse historical PID/offsets.',
                       'Passing offline analysis is not acceptance of a visible surface, recovery or HD product.']}
    reader.unchanged()
    dump_new(output, spec)
    return spec


def main():
    p = argparse.ArgumentParser(description=__doc__)
    commands = p.add_subparsers(dest='command', required=True)
    b = commands.add_parser('bind')
    for name in ('activity', 'evidence', 'code', 'binary', 'output'):
        b.add_argument('--' + name, required=True)
    b.add_argument('--mode', required=True, choices=('O', 'D'))
    b.add_argument('--scope', required=True, choices=('saved-observation', 'synthetic-observation'))
    a = commands.add_parser('analyze')
    a.add_argument('--binding', required=True)
    a.add_argument('--output', required=True)
    n = commands.add_parser('plan')
    for name in ('reference-binding', 'activity', 'expected-content', 'output'):
        n.add_argument('--' + name, required=True)
    n.add_argument('--mode', required=True, choices=('O', 'D'))
    args = vars(p.parse_args())
    command = args.pop('command')
    try:
        if command == 'bind':
            result = bind(**args)
        elif command == 'plan':
            result = plan_activity(**args)
        else:
            result = analyze(args['binding'], args['output'])
        print(json.dumps({'status': result.get('status', 'BOUND'), 'schema': result['schema']}))
    except (ValueError, OSError, KeyError, TypeError) as e:
        print(json.dumps({'status': 'REJECTED', 'reason': str(e)}))
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main())
