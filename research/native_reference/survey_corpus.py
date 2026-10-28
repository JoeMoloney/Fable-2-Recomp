"""Run the copied converter over pinned Fable containers; compilation is NOT parity."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import zlib

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'native_offline'))
import bank


def sha(data):
    return hashlib.sha256(data).hexdigest()


def containers(data, parsed):
    """Revisit only records already bounds/overlap/decompression-checked by bank.parse."""
    r = bank.Reader(data, parsed['blob_offset'])
    blob = data[r.pos:]
    # Recover the compressed span table from the checked bank header via a
    # separate bounded walk, rather than modifying the previous tested parser.
    r = bank.Reader(data, 19)
    for _ in range(r.count(1024)):
        r.take(36)
        for _ in range(r.count(4096)):
            r.string(); r.u32()
    for _ in range(r.count()):
        r.string()
    count = r.count(); r.u32()
    for _ in range(count):
        if parsed['debug']:
            r.string()
        else:
            r.u32()
        r.take(1); r.u32()
    groups = r.count(16384)
    sizes = [r.count(bank.MAX_GROUP) for _ in range(groups)]
    offsets = [r.u32() for _ in range(groups)]
    blob = r.take(r.count(bank.MAX_FILE))
    for group, offset in enumerate(offsets):
        comp = bank.Reader(blob, offset)
        d = zlib.decompressobj()
        raw = d.decompress(comp.take(comp.count(bank.MAX_FILE)), sizes[group] + 1)
        if len(raw) != sizes[group]:
            raise ValueError('decompression changed')
        rec = bank.Reader(raw)
        base = rec.u32()
        relative = [rec.u32() for _ in range(4)]
        for index, relative_offset in enumerate(relative):
            rec = bank.Reader(raw, base + relative_offset)
            for _ in range(2):
                rec.take(rec.count() * 16)
            rec.take(32)
            payload = rec.take(rec.count(bank.MAX_GROUP))
            slot = group * 4 + index
            if sha(payload) != parsed['slots'][slot]['sha256']:
                raise ValueError('container identity changed')
            yield slot, payload


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('output', type=Path)
    args = ap.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    for name in ('containers', 'hlsl', 'spirv', 'logs'):
        (out / name).mkdir()
    game = Path('/mnt/HDD/git_stuff/my_git/Fable-2-Recomp/data/Shaders')
    data = (game / 'ShadersRelease.sbk').read_bytes()
    debug_data = (game / 'Shaders.sbk').read_bytes()
    if sha(data) != 'c2732102a0aedb8c7a950e5781fa15092a3c12c2721380944c4e717dade5e736':
        raise ValueError('release bank identity')
    if sha(debug_data) != '60d1e468c909c1403758f54ee1df50de500c28604ade01acf174a79106621fa7':
        raise ValueError('debug bank identity')
    parsed = bank.parse(data)
    debug = bank.parse(debug_data)
    names = {}
    for entry in debug['entries']:
        slot = entry['slot']
        if debug['slots'][slot]['microcode_sha256'] == parsed['slots'][slot]['microcode_sha256']:
            names.setdefault(slot, []).append(entry['name_or_id'])
    compiler = HERE / 'tools/dxc-1.9.2609/root'
    tool_manifest = json.loads((compiler.parent / 'manifest.json').read_text())
    for path in ('bin/dxc', 'lib/libdxcompiler.so'):
        if sha((compiler / path).read_bytes()) != tool_manifest['files']['root/' + path]:
            raise ValueError('compiler identity changed')
    env = os.environ.copy()
    env['LD_LIBRARY_PATH'] = str(compiler / 'lib')
    report = {'scope': 'emission/compilation survey, NO execution or shader semantic parity',
              'release_bank_sha256': sha(data), 'debug_bank_sha256': sha(debug_data),
              'converter_revision': '990d03b28a27b50277ee5d8d942e1c5f873869d1',
              'assertions_enabled': True, 'sonic_specializations_enabled': False,
              'dxc_archive_sha256': tool_manifest['archive_sha256'], 'shaders': []}

    def run(command, log):
        try:
            result = subprocess.run(['prlimit', '--core=0', '--cpu=10', '--as=2147483648', '--',
                                     *map(str, command)], capture_output=True, env=env, timeout=20)
            log.write_bytes(result.stdout + result.stderr)
            return result.returncode
        except subprocess.TimeoutExpired as exc:
            log.write_bytes((exc.stdout or b'') + (exc.stderr or b'') + b'\nTIMEOUT\n')
            return 124

    common = HERE / 'upstream/xenosrecomp/XenosRecomp/shader_common.h'
    for slot, payload in containers(data, parsed):
        meta = parsed['slots'][slot]
        stage = 'vs' if meta['stage'] == 2 else 'ps'
        stem = f'{slot:03d}-{stage}'
        source = out / 'containers' / (stem + '.bin')
        hlsl = out / 'hlsl' / (stem + '.hlsl')
        spirv = out / 'spirv' / (stem + '.spv')
        source.write_bytes(payload)
        row = {'slot': slot, 'stage': stage, 'names': names.get(slot, []),
               'container_sha256': sha(payload), 'microcode_sha256': meta['microcode_sha256'],
               'emit_exit': run([HERE / 'build-clang-release/emit_hlsl', source, common, hlsl],
                                out / 'logs' / (stem + '-emit.log'))}
        if row['emit_exit'] == 0 and hlsl.is_file():
            row['hlsl_sha256'] = sha(hlsl.read_bytes())
            command = [compiler / 'bin/dxc', hlsl, '-E', 'main', '-T', stage + '_6_0',
                       '-HV', '2021', '-all-resources-bound', '-spirv', '-fvk-use-dx-layout',
                       '-fspv-target-env=vulkan1.2', '-Qstrip_debug', '-Fo', spirv]
            if stage == 'vs':
                command.append('-fvk-invert-y')
            row['compile_exit'] = run(command, out / 'logs' / (stem + '-dxc.log'))
            if row['compile_exit'] == 0:
                row['spirv_sha256'] = sha(spirv.read_bytes())
                row['validate_exit'] = run(['spirv-val', '--target-env', 'vulkan1.2',
                                            '--scalar-block-layout', spirv],
                                           out / 'logs' / (stem + '-validate.log'))
        report['shaders'].append(row)
        if len(report['shaders']) % 50 == 0:
            print('Surveyed', len(report['shaders']), 'containers', flush=True)
    report['summary'] = {}
    for stage in ('vs', 'ps', 'all'):
        rows = [r for r in report['shaders'] if stage == 'all' or r['stage'] == stage]
        report['summary'][stage] = {'total': len(rows), 'emitted': sum(r['emit_exit'] == 0 for r in rows),
                                    'compiled': sum(r.get('compile_exit') == 0 for r in rows),
                                    'spirv_valid': sum(r.get('validate_exit') == 0 for r in rows)}
    failures = Counter()
    for row in report['shaders']:
        stem = f"{row['slot']:03d}-{row['stage']}"
        for phase, key in (('emit', 'emit_exit'), ('dxc', 'compile_exit'), ('validate', 'validate_exit')):
            if key in row and row[key] != 0:
                text = (out / 'logs' / (stem + '-' + phase + '.log')).read_text(errors='replace')
                lines = [line for line in text.splitlines() if 'error:' in line or 'Assertion' in line or 'TIMEOUT' in line]
                row['failure'] = lines[0] if lines else text[:500]
                failures[phase] += 1
                break
    report['failure_counts'] = dict(failures)
    report['survey_complete'] = len(report['shaders']) == len(parsed['slots']) == 588
    (out / 'survey.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['summary'], indent=2))
    # Unsupported shaders are measured results, not a failed harness execution.
    return 0 if report['survey_complete'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
