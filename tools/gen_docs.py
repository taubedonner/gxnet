#!/usr/bin/env python3
"""Extract subfunction semantics from the vendor reference (markdown export).

Usage:
    python3 gen_docs.py GxNet-de.md registry_table.hpp > ../core/include/gxnet/detail/registry_docs.hpp

Produces the optional companion to registry_table.hpp: descriptions, value
ranges and named values.  The result is deliberately **not** tracked by git --
it is a derivative of Bizerba's manual, and the manual is not in this
repository either.  Everything still builds without it; see registry.hpp.

Why the German edition: it is a later revision than the English one, and its
tables survived the PDF conversion as real markdown tables with the five
columns intact.  The English export flattened them into prose with no
separators, so the value range and the description cannot be told apart.

Two row shapes appear, and both are handled.  Sketched with invented content,
since the real thing is what this tool is meant to keep out of the repository:

    |**AA00**<br>AAA_NAME|6.21|0 = first<br>1 = second|0 / 1|what it is for|
    |**AA01**|6.21|0 = first|0 / 1|what it is for|
    |AAA_OTHER||1 = second|||

A subfunction may occupy several rows, one per release that changed it; the
rows are merged, earliest wins for a given value.
"""

import re
import sys

TOKEN = re.compile(r'^[\dX#%!?*]?([A-Z]{2}[0-9A-F]{2})$')
FRAGMENT = re.compile(r'^[#%!?*]?([A-Z][A-Z0-9_]*)$')
VERSION = re.compile(r'^(\d{1,2})\.(\d{2})\b')
# "0 = text", and the form that repeats the value unsigned in brackets.
ENUM = re.compile(r'^(-?\d+)\s*(?:\([^)]*\))?\s*=\s*(.+)$')
REGISTRY = re.compile(r"Token\{'(\w)', '(\w)', 0x([0-9A-F]{2})\}")


def cells(line):
    """Splits one table row, keeping empty trailing cells.

    Not str.strip('|'): that eats every trailing pipe at once, so a row ending
    in empty cells loses columns and stops looking like a table row at all.
    """
    line = line.rstrip()
    if not (line.startswith('|') and line.endswith('|')):
        return None
    return [c.strip() for c in line[1:-1].split('|')]


def columns(row):
    """Locates the version cell and reads coding, range and description after it.

    The table has a fixed column order but not a fixed column count: the
    conversion leaves empty separator cells in some rows and not in others, so
    positions are taken relative to the version rather than from the left edge.
    """
    for i, cell in enumerate(row[1:], 1):
        if VERSION.match(cell):
            after = row[i + 1:] + ['', '', '']
            return after[0], after[1], after[2]
    return '', '', ''


def extract(path):
    entries = {}
    current = None
    with open(path, encoding='utf-8') as fh:
        for line in fh:
            row = cells(line)
            if row is None or len(row) < 3:
                current = None
                continue

            parts = row[0].split('<br>')
            match = TOKEN.match(parts[0])
            if match and any(FRAGMENT.match(p) for p in parts[1:2]):
                current = match.group(1)
            elif current and row[0] == '':
                # A row that continues the entry above: the reference splits a
                # long list of values across rows, and dropping them loses the
                # tail of every enumeration that did not fit on one line.
                pass
            else:
                current = None
                continue

            entry = entries.setdefault(current, {'desc': '', 'range': '', 'vals': {}})
            coding, value_range, description = columns(row)
            if not any((coding, value_range, description)):
                # No version cell in a continuation row; the columns still line
                # up with the header, so read them from the right instead.
                padded = row[1:] + ['', '', '']
                coding, value_range, description = padded[1], padded[2], padded[3]

            for part in coding.split('<br>'):
                m = ENUM.match(part.strip())
                if m:
                    entry['vals'].setdefault(int(m.group(1)), m.group(2).strip())
            if value_range and not entry['range']:
                entry['range'] = value_range.replace('<br>', ' ').strip()
            text = description.replace('<br>', ' ').strip()
            if text and text not in entry['desc']:
                entry['desc'] = (entry['desc'] + ' ' + text).strip()
    return entries


def known_tokens(path):
    with open(path, encoding='utf-8') as fh:
        return {g + t + i for g, t, i in REGISTRY.findall(fh.read())}


def quote(text):
    """A C++ string literal in pure ASCII.

    The manual is full of umlauts and the generated header has to compile
    wherever the library does, including toolchains that read source in a
    local code page. Every non-ASCII byte therefore becomes a hex escape, and
    because a hex escape is greedy the string is broken whenever a literal hex
    digit follows one.
    """
    out = ['"']
    escaped = False
    for byte in text.encode('utf-8'):
        char = chr(byte)
        if byte < 0x20 or byte >= 0x7F:
            out.append('\\x%02X' % byte)
            escaped = True
            continue
        if escaped and char in '0123456789abcdefABCDEF':
            out.append('" "')
        escaped = False
        if char in '"\\':
            out.append('\\')
        out.append(char)
    out.append('"')
    return ''.join(out)


def emit(entries, tokens):
    rows = sorted((t for t in tokens if t in entries), key=lambda t: t)
    rows = [t for t in rows if entries[t]['desc'] or entries[t]['range'] or entries[t]['vals']]

    flat = []
    placed = {}
    for token in rows:
        vals = sorted(entries[token]['vals'].items())
        placed[token] = (len(flat), len(vals))
        flat.extend(vals)

    print('// SPDX-License-Identifier: MIT')
    print('// Generated by tools/gen_docs.py -- do not edit by hand.')
    print('// Source: Bizerba GxNet subfunction reference, German edition.')
    print('//')
    print('// Not tracked by git: this is derived from a vendor manual that is not in')
    print('// the repository either. Regenerate it with tools/gen_docs.py.')
    print('#ifndef GXNET_DETAIL_REGISTRY_DOCS_HPP')
    print('#define GXNET_DETAIL_REGISTRY_DOCS_HPP')
    print('')
    print('#include <array>')
    print('')
    print('#include "gxnet/detail/token_doc.hpp"')
    print('')
    print('namespace gxnet {')
    print('namespace detail {')
    print('')
    print('// clang-format off')
    print('inline constexpr std::array<TokenValue, %d> kDocValues{{' % len(flat))
    for value, text in flat:
        print('    {%d, %s},' % (value, quote(text)))
    print('}};')
    print('')
    print('inline constexpr std::array<TokenDoc, %d> kDocTable{{' % len(rows))
    for token in rows:
        entry = entries[token]
        begin, count = placed[token]
        print("    {Token{'%s', '%s', 0x%s}, %s, %s, %d, %d}," % (
            token[0], token[1], token[2:4],
            quote(entry['desc']), quote(entry['range']), begin, count))
    print('}};')
    print('// clang-format on')
    print('')
    print('}  // namespace detail')
    print('}  // namespace gxnet')
    print('')
    print('#endif  // GXNET_DETAIL_REGISTRY_DOCS_HPP')

    described = sum(1 for t in rows if entries[t]['desc'])
    valued = sum(1 for t in rows if entries[t]['vals'])
    print('// entries: %d of %d registry tokens; %d described, %d with named values, %d values'
          % (len(rows), len(tokens), described, valued, len(flat)), file=sys.stderr)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    emit(extract(sys.argv[1]), known_tokens(sys.argv[2]))
