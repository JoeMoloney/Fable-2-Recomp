"""Bounded read-only ShaderBankFile v3 analysis; no SDK/converter execution."""
import argparse
import hashlib
import json
import pathlib
import struct
import zlib

MAX_FILE = 16 * 1024 * 1024
MAX_GROUP = 1024 * 1024


class Invalid(ValueError):
    pass


class Reader:
    def __init__(self, data, offset=0):
        self.data, self.pos = data, offset

    def take(self, size):
        if size < 0 or self.pos < 0 or size > len(self.data) - self.pos:
            raise Invalid(f'out of bounds at {self.pos:#x}, size {size}')
        result = self.data[self.pos:self.pos + size]
        self.pos += size
        return result

    def u32(self):
        return struct.unpack('>I', self.take(4))[0]

    def count(self, maximum=65536):
        value = self.u32()
        if value > maximum:
            raise Invalid(f'count {value} exceeds {maximum}')
        return value

    def string(self):
        end = self.data.find(b'\0', self.pos, min(len(self.data), self.pos + 4096))
        if end < 0:
            raise Invalid('unterminated string')
        try:
            return self.take(end - self.pos + 1)[:-1].decode('ascii')
        except UnicodeDecodeError as exc:
            raise Invalid('non-ASCII string') from exc


def word(data, offset):
    return Reader(data, offset).u32()


def container(data):
    r = Reader(data)
    flags, virtual, physical = r.u32(), r.u32(), r.u32()
    if flags & 0xffffff00 != 0x102a1100 or virtual + physical != len(data):
        raise Invalid('container flags/size')
    r.take(4)
    ctab, definitions, shader = r.u32(), r.u32(), r.u32()
    if not ctab or ctab >= virtual or shader < 36 or shader + 24 > virtual:
        raise Invalid('container metadata offsets')
    if definitions and definitions >= virtual:
        raise Invalid('definition offset')
    s = Reader(data, shader)
    physical_offset, size = s.u32(), s.u32()
    if size % 12 or physical_offset + size > physical:
        raise Invalid('shader physical range')
    code = Reader(data, virtual + physical_offset).take(size)
    table_length = word(data, ctab)
    if table_length < 28 or ctab + 4 + table_length > virtual:
        raise Invalid('constant table range')
    table = Reader(data, ctab + 4).take(table_length)
    if word(table, 0) != 28:
        raise Invalid('constant table header')
    count, info = word(table, 12), word(table, 16)
    if count > 4096:
        raise Invalid('constant count')
    constants = []
    ci = Reader(table, info)
    for _ in range(count):
        name_offset = ci.u32()
        register_set, index, register_count, reserved = struct.unpack('>4H', ci.take(8))
        type_offset, default_offset = ci.u32(), ci.u32()
        if register_set > 3 or reserved:
            raise Invalid('constant register metadata')
        Reader(table, type_offset).take(16)
        if default_offset and default_offset >= len(table):
            raise Invalid('constant default offset')
        constants.append({'name': Reader(table, name_offset).string(),
                          'register_set': register_set, 'index': index, 'count': register_count})
    vertex_elements = []
    if flags & 1:
        if shader + 36 > virtual:
            raise Invalid('vertex reflection header range')
        skip, count = word(data, shader + 24), word(data, shader + 28)
        if count > 256:
            raise Invalid('vertex element count')
        elements = Reader(data[:virtual], shader + 36 + skip * 4)
        for _ in range(count):
            value = elements.u32()
            vertex_elements.append({'instruction': value & 4095,
                                    'usage': (value >> 12) & 15,
                                    'usage_index': (value >> 16) & 15})
    return {'sha256': hashlib.sha256(data).hexdigest(), 'bytes': len(data),
            'stage': 2 if flags & 1 else 1,
            'virtual_size': virtual, 'physical_size': physical,
            'ctab_offset': ctab, 'shader_offset': shader,
            'microcode_sha256': hashlib.sha256(code).hexdigest(),
            'microcode_hex': code.hex(), 'constants': constants,
            'vertex_elements': vertex_elements}


def parse(data):
    if len(data) > MAX_FILE:
        raise Invalid('bank too large')
    r = Reader(data)
    if r.take(14) != b'ShaderBankFile' or r.u32() != 3:
        raise Invalid('bank header/version')
    debug = r.take(1)[0]
    if debug not in (0, 1):
        raise Invalid('debug flag')
    for _ in range(r.count(1024)):
        r.take(9 * 4)
        for _ in range(r.count(4096)):
            r.string()
            r.u32()
    for _ in range(r.count()):
        r.string()
    count, auxiliary = r.count(), r.u32()
    entries = []
    for i in range(count):
        name = r.string() if debug else r.u32()
        stage, slot = r.take(1)[0], r.u32()
        if stage not in (1, 2):
            raise Invalid('stage')
        entries.append({'index': i, 'name_or_id': name, 'stage': stage, 'slot': slot})
    groups = r.count(16384)
    sizes = [r.count(MAX_GROUP) for _ in range(groups)]
    if sum(sizes) > 64 * 1024 * 1024:
        raise Invalid('total decompression budget')
    offsets = [r.u32() for _ in range(groups)]
    blob_size = r.count(MAX_FILE)
    blob_offset = r.pos
    blob = r.take(blob_size)
    if r.pos != len(data):
        raise Invalid('trailing bank data')
    slots = {}
    spans = []
    for group in range(groups):
        g = Reader(blob, offsets[group])
        length = g.count(MAX_FILE)
        compressed = g.take(length)
        spans.append((offsets[group], g.pos))
        d = zlib.decompressobj()
        try:
            raw = d.decompress(compressed, sizes[group] + 1)
        except zlib.error as exc:
            raise Invalid('zlib stream') from exc
        if len(raw) != sizes[group] or d.unconsumed_tail or d.unused_data:
            raise Invalid('compressed size/tail mismatch')
        # The real banks deliberately use non-EOF Z_SYNC_FLUSH streams.
        if not d.eof and not compressed.endswith(b'\0\0\xff\xff'):
            raise Invalid('non-EOF stream without flush terminator')
        h = Reader(raw)
        base = h.u32()
        relative = [h.u32() for _ in range(4)]
        record_spans = []
        for index, offset in enumerate(relative):
            if base + offset < 20:
                raise Invalid('record overlaps group header')
            rec = Reader(raw, base + offset)
            for _ in range(2):
                rec.take(rec.count(65536) * 16)
            rec.take(32)
            length = rec.count(MAX_GROUP)
            payload = rec.take(length)
            record_spans.append((base + offset, rec.pos))
            slots[group * 4 + index] = container(payload)
        record_spans.sort()
        if any(a[1] > b[0] for a, b in zip(record_spans, record_spans[1:])):
            raise Invalid('overlapping records')
    spans.sort()
    if spans and (spans[0][0] != 0 or spans[-1][1] != len(blob)):
        raise Invalid('blob coverage')
    if any(a[1] != b[0] for a, b in zip(spans, spans[1:])):
        raise Invalid('compressed spans overlap/gap')
    if any(e['slot'] not in slots for e in entries):
        raise Invalid('entry slot')
    if any(e['stage'] != slots[e['slot']]['stage'] for e in entries):
        raise Invalid('entry/container stage mismatch')
    return {'sha256': hashlib.sha256(data).hexdigest(), 'debug': bool(debug),
            'auxiliary': auxiliary, 'blob_offset': blob_offset,
            'groups': groups, 'entries': entries, 'slots': slots}


def selected_declaration(code):
    """The verified pair ONLY; not a general fetch patcher."""
    if len(code) != 96 or hashlib.sha256(code).hexdigest() != 'b88a0cadefb415d93a12db89b588f4e60981972193c654c9bc523c02eb2fc4bf':
        raise Invalid('selected VS identity/length')
    result = bytearray(code)
    struct.pack_into('>III', result, 3 * 12, 0x25f81000, 0x00393a88, 0x00000005)
    struct.pack_into('>III', result, 4 * 12, 0x05f80000, 0x40253fc8, 0x00000305)
    return bytes(result)


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('bank', type=pathlib.Path)
    ap.add_argument('output', type=pathlib.Path)
    args = ap.parse_args()
    if args.bank.stat().st_size > MAX_FILE:
        raise Invalid('bank too large')
    parsed = parse(args.bank.read_bytes())
    with args.output.open('x') as f:
        json.dump(parsed, f, indent=2)
        f.write('\n')
