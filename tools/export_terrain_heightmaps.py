"""Export winning FO4 LAND heights from an MO2 profile without modifying plugins.

Usage: python tools/export_terrain_heightmaps.py --mo2 D:/FO4 --data GAME/Data
       --output OUTPUT

CSAHHF01 files contain a 32-byte little-endian header (<8sIIiiII), followed
by width*height float32 world-Z samples, south to north, west to east.
Header fields: magic, width, height, west cell, south cell, samples/cell, zero.
Missing LAND vertices are -FLT_MAX. No fabricated zero-height terrain fills holes.
The companion JSON records readable input order and winning cell sources.
"""
from __future__ import annotations
import argparse
import json
import math
import mmap
from pathlib import Path
import re
import struct
import zlib
import numpy as np

MISSING = np.finfo(np.float32).min
HEADER = struct.Struct('<8sIIiiII')


def fields(data):
    offset = 0
    extended = None
    while offset < len(data):
        if offset + 6 > len(data):
            raise ValueError('truncated subrecord header')
        tag, size = struct.unpack_from('<4sH', data, offset)
        offset += 6
        if tag == b'XXXX':
            if size != 4 or offset + 4 > len(data) or extended is not None:
                raise ValueError('invalid extended subrecord')
            extended = struct.unpack_from('<I', data, offset)[0]
            offset += 4
            continue
        size = extended if extended is not None else size
        extended = None
        if offset + size > len(data):
            raise ValueError('truncated subrecord')
        yield tag, data[offset:offset + size]
        offset += size
    if extended is not None:
        raise ValueError('orphan extended subrecord')


def decode_height(data):
    # xEdit wbVertexHeightMap: float base, 33x33 signed deltas, 3 unused bytes.
    # FO4VR 1403875A9 reads 0x448 bytes; 140387600 onward accumulates signed
    # deltas, resetting each row to its previous row's FIRST vertex. The later
    # CVTTSS2SI/SHL 3 sequence truncates accumulated height before scaling.
    if len(data) != 1096:
        raise ValueError('LAND VHGT must contain 1096 bytes')
    base, = struct.unpack_from('<f', data)
    if not math.isfinite(base):
        raise ValueError('nonfinite LAND offset')
    delta = np.frombuffer(data, dtype=np.int8, count=1089, offset=4).reshape(33, 33)
    first = np.cumsum(delta[:, 0], dtype=np.float32) + base
    values = first[:, None] + np.cumsum(delta, axis=1, dtype=np.float32) - delta[:, :1]
    if not np.isfinite(values).all() or np.max(np.abs(values)) >= 268435456:
        raise ValueError('LAND height exceeds native integer scaling range')
    return np.trunc(values).astype(np.float32) * 8


def read_plugin(path, worlds, cells, lands):
    with path.open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
        masters = []

        def identity(raw):
            index = raw >> 24
            names = masters + [path.name.casefold()]
            if index >= len(names):
                raise ValueError(f'{path.name}: invalid master index {index}')
            return names[index], raw & 0xffffff

        def walk(start, end, world=None, cell=None, depth=0):
            if depth > 16:
                raise ValueError('excessive plugin group nesting')
            pos = start
            while pos < end:
                if pos + 24 > end:
                    raise ValueError('truncated record header')
                tag, size, flags, form = struct.unpack_from('<4sIII', data, pos)
                if tag == b'GRUP':
                    if size < 24 or pos + size > end:
                        raise ValueError('invalid group bounds')
                    label = data[pos + 8:pos + 12]
                    kind = struct.unpack_from('<i', data, pos + 12)[0]
                    if kind != 0 or label in (b'WRLD', b'CELL'):
                        walk(pos + 24, pos + size,
                             identity(flags) if kind == 1 else world,
                             identity(flags) if kind in (6, 8, 9, 10) else cell, depth + 1)
                    pos += size
                    continue
                finish = pos + 24 + size
                if finish > end:
                    raise ValueError('record exceeds containing group')
                if tag in (b'TES4', b'WRLD', b'CELL', b'LAND'):
                    body = data[pos + 24:finish]
                    if flags & 0x40000:
                        expected, = struct.unpack_from('<I', body)
                        decoder = zlib.decompressobj()
                        body = decoder.decompress(body[4:], expected + 1)
                        if len(body) != expected or not decoder.eof or decoder.unused_data:
                            raise ValueError('invalid compressed record')
                    parts = dict(fields(body))
                    if tag == b'TES4':
                        masters[:] = [v.rstrip(b'\0').decode('utf-8').casefold()
                                      for k, v in fields(body) if k == b'MAST']
                    else:
                        key = identity(form)
                        deleted = bool(flags & 0x20)
                        if tag == b'WRLD':
                            previous = worlds.get(key, {}) if flags & 0x4000 else {}
                            value = dict(previous)
                            if b'EDID' in parts:
                                value['name'] = parts[b'EDID'].rstrip(b'\0').decode('ascii')
                            value['deleted'] = deleted
                            worlds[key] = value
                        elif tag == b'CELL':
                            coordinate = parts.get(b'XCLC')
                            interior = bool(parts.get(b'DATA', b'\0')[0] & 1)
                            cells[key] = None if deleted or interior or coordinate is None else (
                                world, *struct.unpack_from('<ii', coordinate))
                        else:
                            lands[key] = (cell, None if deleted else parts.get(b'VHGT'), path.name)
                pos = finish
        walk(0, len(data))


def profile_inputs(mo2, game_data):
    ini = (mo2 / 'ModOrganizer.ini').read_text(encoding='utf-8-sig')
    match = re.search(r'^selected_profile=(?:@ByteArray\((.*?)\)|(.*))$', ini, re.M)
    if not match:
        raise ValueError('MO2 selected_profile is missing')
    profile = mo2 / 'profiles' / (match[1] or match[2]).strip()
    mod_lines = (profile / 'modlist.txt').read_text(encoding='utf-8-sig').splitlines()
    # MO2 writes highest priority first. Overwrite wins every loose-file conflict.
    roots = [mo2 / 'overwrite'] + [mo2 / 'mods' / line[1:] for line in mod_lines if line.startswith('+')] + [game_data]
    files = {}
    for root in roots:
        if root.is_dir():
            for path in root.iterdir():
                if path.is_file() and path.suffix.casefold() in ('.esm', '.esp', '.esl'):
                    files.setdefault(path.name.casefold(), path)
    enabled = {line[1:].casefold() for line in (profile / 'plugins.txt').read_text(encoding='utf-8-sig').splitlines() if line.startswith('*')}
    enabled.update(('fallout4.esm', 'fallout4_vr.esm'))
    order = [line.strip() for line in (profile / 'loadorder.txt').read_text(encoding='utf-8-sig').splitlines() if line.strip() and not line.startswith('#')]
    paths = []
    for name in order:
        if name.casefold() in enabled:
            if name.casefold() not in files:
                raise ValueError(f'Enabled plugin not found: {name}')
            paths.append(files[name.casefold()])
    absent = enabled - {p.name.casefold() for p in paths}
    if absent:
        raise ValueError(f'Enabled plugins missing from loadorder: {sorted(absent)}')
    return profile.name, paths


def export(paths, output, samples=8):
    if samples not in (8, 16, 32):
        raise ValueError("samples/cell must be 8, 16 or 32")
    previous_names = set()
    manifest = output / 'terrain-sources.json'
    if manifest.exists():
        previous_names = {world['name'] for world in json.loads(manifest.read_text(encoding='utf-8'))['worlds']}
    worlds, cells, lands = {}, {}, {}
    for path in paths:
        print(f'Reading {path.name}', flush=True)
        read_plugin(path, worlds, cells, lands)
    grouped = {}
    for key, (cell, heights, source) in lands.items():
        location = cells.get(cell)
        if heights is None or location is None or location[0] is None:
            continue
        world, x, y = location
        group = grouped.setdefault(world, {})
        if (x, y) in group:
            raise ValueError(f'Multiple winning LAND records for {world} cell {(x,y)}')
        group[x, y] = (heights, source)
    output.mkdir(parents=True, exist_ok=True)
    report = {'plugins': [str(p) for p in paths], 'worlds': []}
    names = set()
    for world, records in grouped.items():
        info = worlds.get(world, {})
        if info.get('deleted'):
            continue
        name = info.get('name', '')
        if not re.fullmatch(r'[A-Za-z0-9_-]+', name) or name.casefold() in names:
            raise ValueError(f'Invalid or duplicate worldspace editor ID: {name!r}')
        names.add(name.casefold())
        west, east = min(x for x,y in records), max(x for x,y in records)
        south, north = min(y for x,y in records), max(y for x,y in records)
        width, height = (east-west+1)*32+1, (north-south+1)*32+1
        if max(width, height) > 16384:
            raise ValueError(f'{name}: dimensions exceed D3D11 texture limit')
        pixels = np.full((height, width), MISSING, dtype='<f4')
        for (x,y), (raw, source) in sorted(records.items()):
            row, col = (y-south)*32, (x-west)*32
            decoded = decode_height(raw)
            # Shared boundaries can disagree in existing mods. Use the higher
            # surface conservatively; no interior samples are merged.
            pixels[row:row+33,col:col+33] = np.maximum(pixels[row:row+33,col:col+33], decoded)
        pixels = pixels[::32//samples, ::32//samples].copy()
        height, width = pixels.shape
        destination = output / f'{name}.cshf'
        with destination.open('wb') as stream:
            stream.write(HEADER.pack(b'CSAHHF01', width, height, west, south, samples, 0))
            stream.write(pixels.tobytes())
        valid = pixels[pixels != MISSING]
        report['worlds'].append({'name':name, 'size':[width,height], 'cells':len(records),
            'heightRange':[float(valid.min()),float(valid.max())],
            'sources':sorted({source for raw,source in records.values()})})
        print(f'{name}: {width}x{height}, {len(records)} LAND cells', flush=True)
    for old_name in previous_names - {w['name'] for w in report['worlds']}:
        if re.fullmatch(r'[A-Za-z0-9_-]+', old_name):
            (output / (old_name + '.cshf')).unlink(missing_ok=True)
    (output / 'terrain-sources.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mo2', type=Path, required=True)
    parser.add_argument('--data', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--samples-per-cell', type=int, choices=(8,16,32), default=8)
    args = parser.parse_args()
    profile, paths = profile_inputs(args.mo2, args.data)
    print(f'Profile: {profile}; {len(paths)} enabled plugins')
    export(paths, args.output, args.samples_per_cell)
