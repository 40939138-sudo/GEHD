"""Narrow qualifications: synthetic receipts/geometry plus saved identity-bound VS.

Game data is read from the explicitly supplied evidence root, never embedded here.
Historical acceptance values are regression assertions only, not analyzer logic.
"""
from pathlib import Path
import argparse
import copy
import json
import shutil
import struct
import subprocess
import os
import sys
import traceback
import types
import offline


def write(p, value):
    p.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(value, bytes):
        with p.open('xb') as f:
            f.write(value)
    else:
        offline.dump_new(p, value)


def load(p):
    return offline.parse(p.read_bytes())


def canonical(points):
    raw = b'GEHD_INDEXED_ATTRIBUTES_V2\0' + struct.pack('<II', 3, len(points))
    raw += bytes((0, 0, 2, 12, 5, 0, 1, 8))
    for point in points:
        raw += struct.pack('<5f', *point, 0.0, 0.0)
    return {'attempted': True, 'complete': True, 'reason': 'NONE',
            'schema': 'GEHD_SELECTED_CANONICAL_V1', 'bytes': len(raw), 'hex': raw.hex(),
            'sha256': offline.digest(raw), 'fromUploadObservation': True, 'contentAdopted': False}


class Qualification:
    def __init__(self, evidence, output):
        self.evidence = offline.absolute(evidence)
        self.output = offline.absolute(output)
        self.code = Path(__file__).resolve().parent
        self.output.mkdir()
        (self.output / 'results').mkdir()
        (self.output / 'fixtures').mkdir()
        self.r36 = self.evidence / 'toolchain/game-canonical-elevator-r36-r1'
        self.r31 = self.evidence / 'toolchain/game-target-elevator-r31-original-r1'
        self.pre = load(self.r36 / 'preflight.json')
        self.plan = load(self.r36 / 'install-plan.json')
        self.rows = offline.log_rows((self.r36 / 'observer.jsonl').read_bytes())
        self.fr = load(self.r36 / 'frida/result.json')
        self.profile = load(self.r36 / 'frida/profile.json')
        self.checks = []
        self.commands = []
        self.inputs_before = {}
        for activity in (self.r36, self.r31):
            for p in activity.rglob('*'):
                if p.is_file():
                    self.inputs_before[str(p)] = offline.digest(p.read_bytes())

    def check(self, name, fn):
        try:
            detail = fn()
            self.checks.append({'check': name, 'passed': True, 'detail': detail})
        except Exception as e:
            self.checks.append({'check': name, 'passed': False, 'error': str(e),
                                'trace': traceback.format_exc()})

    def execute(self, activity, evidence, binary, name, mode='D'):
        binding = self.output / (name + '.binding.json')
        result = self.output / 'results' / name
        self.commands.extend([
            [sys.executable, '-B', str(self.code / 'offline.py'), 'bind', '--activity', str(activity),
             '--evidence', str(evidence), '--code', str(self.code), '--binary', str(binary),
             '--mode', mode, '--scope', 'saved-observation' if evidence == self.evidence else 'synthetic-observation',
             '--output', str(binding)],
            [sys.executable, '-B', str(self.code / 'offline.py'), 'analyze', '--binding', str(binding), '--output', str(result)]])
        offline.bind(str(activity), str(evidence), str(self.code), mode,
                     'saved-observation' if evidence == self.evidence else 'synthetic-observation', str(binary), str(binding))
        return offline.analyze(str(binding), str(result)), binding

    def fixture(self, name, mutate=None, points=None):
        p = self.output / 'fixtures' / name
        p.mkdir()
        pre, plan, fr, profile = map(copy.deepcopy, (self.pre, self.plan, self.fr, self.profile))
        required = {'observer_start', 'named_module_binding', 'source_configuration', 'd3dx_source_replacement',
                    'scene_capture_draw', 'scene_capture_draw_pair', 'scene_capture_armed', 'scene_capture_image',
                    'resource_draw_relation_checkpoint', 'generic_runtime_terminal_snapshot'}
        rows = [copy.deepcopy(row) for _, row in self.rows if row.get('event') in required]
        binary = b'GEHD_SYNTHETIC_OBSERVER_NOT_EXECUTABLE\n'
        bh = offline.digest(binary)
        pre['observerSha256'] = bh
        pre['tools'] = {}
        plan['files']['d3d9.dll']['sha256'] = bh
        profile['bridge']['sha256'] = bh
        for row in rows:
            if row['event'] == 'named_module_binding':
                for m in row['modules']:
                    if m['role'] == 'observer':
                        m['fileSha256'] = bh
        target = next(r for r in rows if r['event'] == 'scene_capture_draw_pair')
        selected = next(r for r in rows if r['event'] == 'scene_capture_draw' and r['drawOrdinal'] == target['drawOrdinal'])
        points = points or [(-.3, -.3, .5), (.3, -.3, .5), (0, .3, .5),
                            (-3, -.3, .5), (-2, -.3, .5), (-2.5, .3, .5)]
        record = canonical(points)
        selected['context']['selectedCanonical'] = record
        observed = selected['context']['indexedObjectGeometry']['observedSourceGeometry']
        observed.update(canonicalBytes=record['bytes'], orderedAttributesSha256=record['sha256'],
                        fieldMask=3, primitiveCount=len(points) // 3)
        image = next(r for r in rows if r['event'] == 'scene_capture_image')
        raw_image = b'\x20\x30\x40\xff'
        for r in (image, target['pair']['before'], target['pair']['after']):
            r.update(width=1, height=1, bytes=4, sha256=offline.digest(raw_image))
            if 'rowBytes' in r:
                r['rowBytes'] = 4
        for row in rows:
            if row['event'] == 'scene_capture_draw' and row['drawOrdinal'] == target['drawOrdinal']:
                for slot in row['context']['renderTargetContext']['targets']:
                    if slot['slot'] == 0:
                        slot.update(width=1, height=1)
        for s in fr['payloads']:
            if s.get('kind') == 'owner_verified' and s.get('sha256') == self.pre['observerSha256']:
                s['sha256'] = bh
        boundaries = [s for s in fr['payloads'] if s.get('kind') == 'selected_state_boundary']
        for s in boundaries:
            constants = s['snapshot']['lastSuccessfulWrites']['vertexConstants']
            for k in (0, 1, 2, 3, 8, 9, 10):
                values = [0., 0., 0., 0.]
                values[k if k < 4 else k - 8] = 1.
                constants[str(k)]['values'] = values
                constants[str(k)]['rawBits'] = struct.pack('<4f', *values).hex()
        context = {'pre': pre, 'plan': plan, 'rows': rows, 'fr': fr, 'profile': profile,
                   'target': target, 'selected': selected, 'boundaries': boundaries,
                   'images': {r['file']: raw_image for r in (image, target['pair']['before'], target['pair']['after'])}}
        if mutate:
            mutate(context)
        # The post boundary embeds the exact emitted JSON object and line hash.
        line = json.dumps(target, separators=(',', ':'), allow_nan=False).encode()
        boundaries[1]['payload'] = copy.deepcopy(target)
        boundaries[1]['rawPayloadSha256'] = offline.digest(line)
        boundaries[0]['payload'].update({k: copy.deepcopy(target[k]) for k in ('frame', 'drawOrdinal', 'scope', 'selection')})
        rawlog = b'\n'.join(json.dumps(r, separators=(',', ':'), allow_nan=False).encode() for r in rows) + b'\n'
        write(p / 'preflight.json', pre)
        write(p / 'install-plan.json', plan)
        write(p / 'observer.jsonl', rawlog)
        write(p / 'scene-request.json', {'requestSignaled': True})
        write(p / 'frida/result.json', fr)
        write(p / 'frida/profile.json', profile)
        write(p / 'staged-files/d3d9.dll', binary)
        for name, raw in context['images'].items():
            write(p / name, raw)
        return p

    def synthetic(self, name, mutate=None, points=None):
        p = self.fixture(name, mutate, points)
        r, b = self.execute(p, self.output, p / 'staged-files/d3d9.dll', name)
        return r['layers'], b

    @staticmethod
    def expect(value, reason):
        if not value:
            raise AssertionError(reason)

    def run(self):
        def duplicate_keys():
            self.expect(offline.parse(b'{"pixelShaderSha256":"a","pixelShaderSha256":"a"}') ==
                        {'pixelShaderSha256': 'a'}, 'same-value compatibility')
            for raw in (b'{"pixelShaderSha256":"a","pixelShaderSha256":"b"}',
                        b'{"field":4,"field":4}', b'{"pixelShaderSha256":true,"pixelShaderSha256":1}'):
                try:
                    offline.parse(raw)
                except offline.Invalid:
                    continue
                raise AssertionError('conflicting duplicate accepted')
        self.check('identical duplicate fields only; conflicts rejected', duplicate_keys)
        def saved_r36():
            r, _ = self.execute(self.r36, self.evidence, self.r36 / 'staged-files/d3d9.dll', 'saved-r36')
            l = r['layers']
            self.expect(l['projection']['status'] == 'SUPPORTED' and l['projection']['triangleCount'] == 56 and
                        l['projection']['counts']['outside'] == 56 and
                        all('left' in x['outsidePlaneMargins'] for x in l['projection']['planeMargins']), 'saved r36 direction')
            self.expect(l['query']['samples'] == 0 and l['images']['changedPixels'] == 0 and not r['pixelContributionProven'], 'saved r36 intervals')
            return 'raw log/Frida recomputed; 56 outside and left, zero query/RT0'
        self.check('saved r36 raw replay', saved_r36)

        def saved_o():
            r, _ = self.execute(self.r31, self.evidence, self.r31 / 'staged-files/d3d9.dll', 'saved-r31-original', 'O')
            l = r['layers']
            self.expect(l['source']['status'] == 'NOT_APPLICABLE' and l['content']['status'] == l['draw']['status'] == l['images']['status'] == 'SUPPORTED', 'original branches')
            self.expect(l['query']['status'] == l['geometry']['status'] == l['projection']['status'] == 'INSUFFICIENT', 'original missing fields are scoped')
            return 'original content/draw/single RT retained without D source/geometry/query'
        self.check('saved r31 original branch', saved_o)

        def two_triangles():
            l, _ = self.synthetic('two-triangles')
            self.expect(l['projection']['status'] == 'SUPPORTED' and l['projection']['triangleCount'] == 2 and
                        l['projection']['counts']['outside'] == l['projection']['counts']['intersects'] == 1, 'dynamic triangle classification')
            return 'synthetic geometry and constants + identity-bound saved VS'
        self.check('different triangle count and mixed clip classification', two_triangles)

        def missing_vs():
            def mutate(c):
                for s in c['boundaries']:
                    s['snapshot']['lastSuccessfulWrites']['vertexShader'] = None
            l, _ = self.synthetic('missing-vs', mutate)
            self.expect(l['projection']['status'] == 'INSUFFICIENT' and l['draw']['status'] == l['images']['status'] == l['geometry']['status'] == 'SUPPORTED', 'missing VS scope')
        self.check('missing VS preserves other layers', missing_vs)

        def wrong_vs():
            def mutate(c):
                for s in c['boundaries']:
                    s['snapshot']['lastSuccessfulWrites']['vertexShader'].update(bytesHex='00', length=1, sha256=offline.digest(b'\0'))
            l, _ = self.synthetic('wrong-vs', mutate)
            self.expect(l['projection']['status'] == 'INVALID' and l['draw']['status'] == 'SUPPORTED', 'unknown shader isolated')
        self.check('wrong shader does not project', wrong_vs)

        def wrong_bits():
            def mutate(c):
                for s in c['boundaries']:
                    s['snapshot']['lastSuccessfulWrites']['vertexConstants']['0']['rawBits'] = '00' * 16
            l, _ = self.synthetic('wrong-bits', mutate)
            self.expect(l['projection']['status'] == 'INVALID' and l['images']['status'] == 'SUPPORTED', 'constant raw bits checked')
        self.check('constant values/raw bits disagreement', wrong_bits)

        def short_canonical():
            def mutate(c):
                c['selected']['context']['selectedCanonical']['hex'] = '00'
            l, _ = self.synthetic('short-canonical', mutate)
            self.expect(l['geometry']['status'] == 'INVALID' and l['draw']['status'] == 'SUPPORTED', 'truncated canonical')
        self.check('truncated canonical isolated', short_canonical)

        def missing_field():
            def mutate(c):
                del c['target']['selection']['resourceId']
            l, _ = self.synthetic('missing-field', mutate)
            self.expect(l['draw']['status'] == 'INVALID' and l['content']['status'] == 'SUPPORTED', 'missing join field')
        self.check('missing draw field retains content', missing_field)

        def pair_selection():
            def mutate(c):
                c['target']['selection']['stage'] = 1
                c['target']['selection']['sourceLoadOrdinal'] += 1
            l, _ = self.synthetic('wrong-pair-selection', mutate)
            self.expect(l['draw']['status'] == 'INVALID' and l['content']['status'] == 'SUPPORTED' and
                        not l['images'].get('sameDrawAssociationSupported'), 'pair selection not attributed')
        self.check('pair stage/source mismatch is not the selected draw', pair_selection)

        def wrong_surface_descriptor():
            def mutate(c):
                c['selected']['context']['renderTargetContext']['targets'][0]['width'] = 2
            l, _ = self.synthetic('wrong-target-descriptor', mutate)
            self.expect(l['draw']['status'] == 'INVALID' and not l['images'].get('sameDrawAssociationSupported'), 'descriptor mismatch')
        self.check('pair descriptor must match retained RT0', wrong_surface_descriptor)

        def no_interleaving_guard():
            def mutate(c):
                c['target']['observedDrawInterleavingChecked'] = False
            l, _ = self.synthetic('no-interval-guard', mutate)
            self.expect(l['draw']['status'] == 'SUPPORTED' and l['content']['status'] == 'SUPPORTED' and
                        l['query']['status'] == l['images']['status'] == 'INVALID', 'invalid interval retains draw')
        self.check('missing interval guard isolates query/images', no_interleaving_guard)

        def pair_hresult():
            def mutate(c):
                c['target']['pair']['identityAfterHresult'] = -1
            l, _ = self.synthetic('pair-hresult', mutate)
            self.expect(l['images']['status'] == 'INVALID' and l['draw']['status'] == 'SUPPORTED', 'complete does not hide HRESULT')
        self.check('pair complete with failed identity HRESULT rejected', pair_hresult)

        def snapshot_mismatch():
            def mutate(c):
                c['boundaries'][1]['snapshot']['lastSuccessfulWrites']['vertexConstants']['0']['thread'] += 1
            l, _ = self.synthetic('snapshot-mismatch', mutate)
            self.expect(l['projection']['status'] == 'INVALID' and l['draw']['status'] == 'SUPPORTED', 'different snapshots cannot project')
        self.check('before/after state mismatch isolates projection', snapshot_mismatch)

        def pending_state():
            def mutate(c):
                c['boundaries'][0]['pendingStateCalls'] = 1
            l, _ = self.synthetic('pending-state', mutate)
            self.expect(l['projection']['status'] == 'INVALID' and l['images']['status'] == 'SUPPORTED', 'in-flight state call')
        self.check('pending setter call isolates projection', pending_state)

        def frida_profile_mismatch():
            def mutate(c):
                c['profile']['selection']['contentSha256'] = '0' * 64
            l, _ = self.synthetic('frida-selection-mismatch', mutate)
            self.expect(l['projection']['status'] == 'INVALID' and l['content']['status'] == 'SUPPORTED', 'Frida selection join')
        self.check('Frida content profile mismatch rejected', frida_profile_mismatch)

        def no_owner():
            def mutate(c):
                c['fr']['payloads'] = [p for p in c['fr']['payloads'] if p.get('kind') != 'owner_verified']
            l, _ = self.synthetic('no-observer-owner', mutate)
            self.expect(l['projection']['status'] == 'INVALID' and l['draw']['status'] == 'SUPPORTED', 'no observer owner witness')
        self.check('missing observer owner receipt isolates projection', no_owner)

        def original_surface():
            # Build a synthetic O branch with a single captured RT0. This is not
            # a saved r31 image mutation or a claim of original source receipts.
            original = load(self.r31 / 'preflight.json')['expectedOriginalContentIdentity']
            def mutate(c):
                c['pre'].update(sourceEnabled=False, expectedOriginalContentIdentity=original)
                c['plan']['sourceEnabled'] = False
                for row in c['rows']:
                    if row['event'] == 'source_configuration':
                        row['status'] = 'DISABLED'
                    if row['event'] == 'd3dx_source_replacement':
                        row['substituted'] = False
                    if row['event'] == 'resource_draw_relation_checkpoint':
                        row.update(contentIdentitySha256=original, sourceLoadOrdinal=0, sourceWasSubstituted=False)
                ctx = c['selected']['context']
                ctx.update(contentIdentitySha256=original, sourceLoadOrdinal=0, sourceObservationStable=False)
                slot = ctx['renderTargetContext']['targets'][0]
                rt = copy.deepcopy(c['target']['pair']['before'])
                rt.update(complete=True, scopeStillCurrent=True, scope=ctx['renderTargetContext']['scope'],
                          surfaceId=slot['surfaceId'] + 1, slot=0, captureHresult=0,
                          timing='POST_DRAW_READBACK_NONATOMIC')
                c['target'].update(event='scene_capture_target_image', target=rt, windowStillCurrent=True)
            p = self.fixture('original-wrong-surface', mutate)
            r, _ = self.execute(p, self.output, p / 'staged-files/d3d9.dll', 'original-wrong-surface', 'O')
            l = r['layers']
            self.expect(l['draw']['status'] == 'INVALID' and l['content']['status'] == 'SUPPORTED' and
                        l['source']['status'] == 'NOT_APPLICABLE' and not l['images'].get('sameDrawAssociationSupported'), 'O target must be same surface')
        self.check('original target wrong surface rejected without D requirements', original_surface)

        def positive_interval():
            def mutate(c):
                c['target']['occlusion']['samples'] = 7
                r = c['target']['pair']['after']
                raw = b'\0\0\0\xff'
                c['images'][r['file']] = raw
                r['sha256'] = offline.digest(raw)
            l, _ = self.synthetic('positive-interval', mutate)
            self.expect(l['query']['samples'] == 7 and l['images']['changedPixels'] == 1 and
                        not l['query']['pixelContributionProven'] and not l['images']['pixelContributionProven'], 'positive not causal')
        self.check('query and image positive do not establish surface', positive_interval)

        def pending_query():
            def mutate(c):
                c['target']['occlusion'].update(complete=False, samples=None, failure='PENDING')
            l, _ = self.synthetic('pending-query', mutate)
            self.expect(l['query']['status'] == 'INSUFFICIENT' and 'samples' not in l['query'] and l['images']['status'] == 'SUPPORTED', 'no inferred zero')
        self.check('pending query is insufficient not zero', pending_query)

        def stale_raw():
            p = self.fixture('wrong-file-sha')
            b = self.output / 'wrong-file-sha.binding.json'
            offline.bind(str(p), str(self.output), str(self.code), 'D', 'synthetic-observation', str(p / 'staged-files/d3d9.dll'), str(b))
            with (p / 'selected-target-after.bin').open('ab') as f:
                f.write(b'x')
            r = offline.analyze(str(b), str(self.output / 'results/wrong-file-sha'))
            self.expect(r['layers']['images']['status'] == 'INVALID' and r['layers']['source']['status'] == r['layers']['draw']['status'] == 'SUPPORTED', 'bound image failure isolated')
        self.check('bound file hash/length mismatch isolated', stale_raw)

        def truncated_log():
            p = self.fixture('truncated-log')
            with (p / 'observer.jsonl').open('ab') as f:
                f.write(b'{')
            b = self.output / 'truncated-log.binding.json'
            try:
                offline.bind(str(p), str(self.output), str(self.code), 'D', 'synthetic-observation', str(p / 'staged-files/d3d9.dll'), str(b))
            except ValueError:
                self.expect(not b.exists(), 'no accepted truncated binding')
                return
            raise AssertionError('accepted truncated JSONL')
        self.check('truncated JSONL rejected at binding', truncated_log)

        def mode_mismatch():
            for mode, activity in (('O', self.r36), ('D', self.r31)):
                try:
                    offline.bind(str(activity), str(self.evidence), str(self.code), mode, 'saved-observation',
                                 str(activity / 'staged-files/d3d9.dll'), str(self.output / ('wrong-mode-' + mode + '.json')))
                except offline.Invalid:
                    continue
                raise AssertionError('mode mismatch accepted')
        self.check('O and D wrong mode rejected', mode_mismatch)

        def outputs():
            b = self.output / 'saved-r36.binding.json'
            for target in (self.r36, self.r36 / 'new-output', self.code / 'new-output',
                           self.output / 'results/saved-r36', self.r36 / '..' / 'escaped-output'):
                try:
                    offline.analyze(str(b), str(target))
                except offline.Invalid:
                    continue
                raise AssertionError('unsafe output accepted:' + str(target))
        self.check('input/code/collision/escape output rejection', outputs)

        def junction_rejected():
            if os.name != 'nt':
                raise AssertionError('Windows junction qualification required')
            junction = self.output / 'fixtures/history-junction'
            target = self.output / 'fixtures/two-triangles'
            run = subprocess.run(['cmd.exe', '/d', '/c', 'mklink', '/J', str(junction), str(target)],
                                 capture_output=True, text=True, creationflags=0x08000000)
            self.expect(run.returncode == 0, 'junction creation: ' + run.stdout + run.stderr)
            try:
                try:
                    offline.absolute(junction / 'preflight.json')
                except offline.Invalid as e:
                    self.expect('reparse' in str(e), 'junction reason')
                    return
                raise AssertionError('junction accepted')
            finally:
                # Removes this newly created directory link, never its target.
                os.rmdir(junction)
        self.check('actual Windows junction path rejected', junction_rejected)

        def code_identity():
            b = load(self.output / 'saved-r36.binding.json')
            b['code']['geometry.py']['sha256'] = '0' * 64
            p = self.output / 'wrong-helper.binding.json'
            write(p, b)
            try:
                offline.analyze(str(p), str(self.output / 'results/wrong-helper'))
            except offline.Invalid:
                return
            raise AssertionError('wrong helper identity accepted')
        self.check('helper identity mismatch rejected', code_identity)

        def preloaded_helper():
            old = sys.modules.get('geometry')
            fake = types.ModuleType('geometry')
            fake.__file__ = str(self.output / 'foreign_geometry.py')
            sys.modules['geometry'] = fake
            try:
                try:
                    offline.analyze(str(self.output / 'saved-r36.binding.json'),
                                    str(self.output / 'results/preloaded-helper'))
                except offline.Invalid as e:
                    self.expect('loaded_helper_identity' in str(e), 'loaded helper rejection')
                    return
                raise AssertionError('foreign preloaded helper accepted')
            finally:
                if old is None:
                    del sys.modules['geometry']
                else:
                    sys.modules['geometry'] = old
        self.check('preloaded helper from another location rejected', preloaded_helper)

        def fresh_plan():
            b = self.output / 'saved-r36.binding.json'
            spec = offline.plan_activity(str(b), str(self.evidence / 'toolchain/planned-offline-next-activity'), 'D',
                                         self.pre['expectedReplacementContentIdentity'], str(self.output / 'next-activity-spec.json'))
            self.expect(all(v is None for v in spec['runtime'].values()) and not spec['gameExecuted'] and
                        spec['pendingChecks'] and not Path(spec['activity']).exists(), 'fresh plan only')
        self.check('new activity spec has explicit pending live checks', fresh_plan)

        def plan_history_output():
            saved_build = self.evidence / '草稿區/有界取證/elevator-observer-build-r36'
            bound = load(self.output / 'saved-r36.binding.json')
            bound['files']['observer-binary'] = offline.entry(saved_build / 'd3d9.dll', self.evidence)
            b = self.output / 'external-build.binding.json'
            write(b, bound)
            try:
                offline.plan_activity(str(b),
                                      str(self.evidence / 'toolchain/planned-offline-next-activity'), 'D',
                                      self.pre['expectedReplacementContentIdentity'],
                                      str(saved_build / 'new-plan.json'))
            except offline.Invalid:
                return
            raise AssertionError('plan wrote historical binary directory')
        self.check('activity spec rejects historical binary output', plan_history_output)

        def plan_history_activity():
            try:
                offline.plan_activity(str(self.output / 'external-build.binding.json'),
                                      str(self.evidence / '草稿區/有界取證/elevator-observer-build-r36/future-activity'), 'D',
                                      self.pre['expectedReplacementContentIdentity'],
                                      str(self.output / 'bad-future-activity.json'))
            except offline.Invalid:
                return
            raise AssertionError('future activity inside history accepted')
        self.check('activity spec rejects future activity inside saved binary area', plan_history_activity)

        def immutable():
            for p, h in self.inputs_before.items():
                self.expect(offline.digest(Path(p).read_bytes()) == h, 'historical input changed:' + p)
            return len(self.inputs_before)
        self.check('all files in both historical activities unchanged', immutable)
        result = {'schema': 'GEHD_OFFLINE_ENTRY_QUALIFICATION_V1',
                  'status': 'PASS' if all(c['passed'] for c in self.checks) else 'FAIL',
                  'checks': self.checks, 'commandsEquivalentToDirectCalls': self.commands,
                  'actualInvocation': [sys.executable, '-B', *sys.argv],
                  'code': {n: offline.digest((self.code / n).read_bytes()) for n in (*offline.CODE_FILES, 'test_offline.py')},
                  'gameExecuted': False,
                  'limits': ['Synthetic geometry/constants use an identity-bound saved VS; not a synthetic shader or GPU test.',
                             'Commands list equivalent CLI calls; this driver called the same Python functions directly.',
                             'No current installed game, REA service, replay GPU, visible surface or recovery qualification.']}
        write(self.output / 'verification.json', result)
        return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    result = Qualification(args.evidence, args.output).run()
    print(json.dumps({'status': result['status'], 'checks': len(result['checks']),
                      'failed': [c['check'] for c in result['checks'] if not c['passed']]}))
    sys.exit(0 if result['status'] == 'PASS' else 1)
