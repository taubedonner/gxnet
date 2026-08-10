#!/usr/bin/env python3
"""Extract subfunction semantics from the vendor reference (markdown export).

Usage:
    python3 gen_docs.py registry_table.hpp GxNet.md GxNet-de.md > ../core/include/gxnet/detail/registry_docs.hpp

The .hpp argument is the registry; every .md is a source, in the order its
wording is preferred. Sources are merged field by field, so an edition that
describes a subfunction without naming its values still contributes the
description.

Produces the optional companion to registry_table.hpp: descriptions, value
ranges and named values.  The result is deliberately **not** tracked by git --
it is a derivative of Bizerba's manual, and the manual is not in this
repository either.  Everything still builds without it; see registry.hpp.

English first, German behind it: the two editions carry the same date and the
English one is a partial translation, so roughly one description in twenty
still comes out German because the English edition says nothing there.

A row looks like this, sketched with invented content since the real thing is
what this tool exists to keep out of the repository:

    | AA00<br>AAA_NAME |  | 6.21 | 0 = first<br>1 = second | 0 / 1 | what it is for |

A subfunction may occupy several rows, one per release that changed it, and a
row with an empty first cell continues the one above; the rows are merged, and
the earliest wins for a given value.
"""

import re
import sys

TOKEN = re.compile(r'^[\dX#%!?*]?([A-Z]{2}[0-9A-F]{2})$')
FRAGMENT = re.compile(r'^[#%!?*]?([A-Z][A-Z0-9_]*)$')
VERSION = re.compile(r'^(\d{1,2})\.(\d{2})\b')
# "0 = text", and the form that repeats the value unsigned in brackets.
ENUM = re.compile(r'^(-?\d+)\s*(?:\([^)]*\))?\s*=\s*(.+)$')
REGISTRY = re.compile(r"Token\{'(\w)', '(\w)', 0x([0-9A-F]{2})\}, \"([A-Z0-9_]+)\"")
# A cell that defers to another subfunction instead of repeating its coding,
# and one that defers to the row above. Both are frequent enough that ignoring
# them leaves several hundred entries saying nothing at all.
SEE = re.compile(r'^(?:siehe|see)\s+([A-Z][A-Z0-9_]{2,})$', re.IGNORECASE)
DITTO = re.compile(r'^(?:dto\.?|ditto|s\.o\.)$', re.IGNORECASE)
LINK = re.compile(r'\[([^\]]*)\]\(#page-\d+\)')
# The version the reference prints beside a value, pulled into the text by the
# conversion because the two share a cell.
TRAILING_VERSION = re.compile(r'\s*,\s*\d{1,2}\.\d{2}\s*$')
# First cell of the header a coding table repeats when it continues on the next
# page: "Kommando" in German, "Name" in English.
HEADERS = {'Kommando', 'Name'}
# The three columns worth reading, as the header spells them in either edition.
COLUMNS = (('Kodierung', 'Coding'), ('Wertebereich', 'Value range'), ('Bedeutung', 'Meaning'))


def cells(line):
    """Splits one table row, keeping empty trailing cells.

    Not str.strip('|'): that eats every trailing pipe at once, so a row ending
    in empty cells loses columns and stops looking like a table row at all.
    """
    line = LINK.sub(r'\1', line.rstrip())
    if not (line.startswith('|') and line.endswith('|')):
        return None
    return [c.strip() for c in line[1:-1].split('|')]


def layout(row):
    """Column indices of coding, range and meaning, read off a header row.

    A row that continues an entry from the previous page has no version cell to
    measure from, and it is often short as well, because the conversion drops
    the empty cells at its end. Counting from the left against the header the
    table repeats is the only thing that stays right for both.
    """
    found = []
    for names in COLUMNS:
        found.append(next((i for i, c in enumerate(row) if c.startswith(names)), None))
    return tuple(found) if all(i is not None for i in found) else None


def clean(text):
    """Normalises a value's text and drops the release glued onto its end."""
    return TRAILING_VERSION.sub('', re.sub(r'\s+', ' ', text).strip())


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
    last_range = ''
    pages = 0
    columns_at = None
    with open(path, encoding='utf-8') as fh:
        for line in fh:
            row = cells(line)
            if row is None or len(row) < 3:
                # A value list that outgrows a page continues in a fresh table
                # on the next one, under a repeated header. Everything between
                # the two is layout, so the entry has to survive it -- but only
                # as far as the next page, or a stray empty first cell much
                # later in the document would attach itself to it.
                if line.startswith('<!-- page'):
                    pages += 1
                    if pages > 1:
                        current = None
                continue

            parts = row[0].split('<br>')
            match = TOKEN.match(parts[0])
            if match and any(FRAGMENT.match(p) for p in parts[1:2]):
                current = match.group(1)
                pages = 0
            elif current and row[0] == '':
                # A row that continues the entry above: the reference splits a
                # long list of values across rows, and dropping them loses the
                # tail of every enumeration that did not fit on one line.
                pages = 0
            elif set(''.join(row)) <= set('-'):
                continue
            elif row[0] in HEADERS:
                columns_at = layout(row) or columns_at
                continue
            else:
                current = None
                continue

            entry = entries.setdefault(current, {'desc': '', 'range': '', 'vals': {}, 'see': ''})
            coding, value_range, description = columns(row)
            if not any((coding, value_range, description)) and columns_at:
                padded = row + [''] * len(row)
                coding, value_range, description = (padded[i] for i in columns_at)

            # A value whose text does not fit the column width is broken across
            # lines, and the tail carries no number of its own. Read on its own
            # it looks like noise; dropped, it takes the half of the sentence
            # that says what the code means -- "19 = operating access refused,
            # because" is worse than no entry at all.
            value = None
            for part in coding.split('<br>'):
                part = part.strip()
                m = ENUM.match(part)
                if m:
                    value = int(m.group(1))
                    entry['vals'].setdefault(value, clean(m.group(2)))
                    continue
                reference = SEE.match(part)
                if reference and not entry['see']:
                    entry['see'] = reference.group(1)
                    value = None
                elif value is not None and part:
                    entry['vals'][value] = clean(entry['vals'][value] + ' ' + part)

            value_range = value_range.replace('<br>', ' ').strip()
            if DITTO.match(value_range):
                # "as above", and above is the previous row unless this entry
                # defers to another subfunction, which is resolved later.
                value_range = '' if entry['see'] else last_range
            elif value_range:
                last_range = value_range
            if value_range and not entry['range']:
                entry['range'] = value_range
            text = description.replace('<br>', ' ').strip()
            if text and text not in entry['desc']:
                entry['desc'] = (entry['desc'] + ' ' + text).strip()
    return entries


def known_tokens(path):
    """Token codes in the registry, and the code each symbolic name stands for."""
    with open(path, encoding='utf-8') as fh:
        found = REGISTRY.findall(fh.read())
    return {g + t + i for g, t, i, _ in found}, {name: g + t + i for g, t, i, name in found}


def resolve(entries, by_name):
    """Follows the "see <other subfunction>" cells.

    The reference codes a value set once and points the rest at it; several
    hundred subfunctions carry nothing but such a pointer, so an entry that
    stops here says nothing where the document says plenty. References chain,
    hence the repeats, and a cycle simply stops making progress.
    """
    for _ in range(3):
        for entry in entries.values():
            other = entries.get(by_name.get(entry['see'], ''))
            if other is None or other is entry:
                continue
            for value, text in other['vals'].items():
                entry['vals'].setdefault(value, text)
            if not entry['range']:
                entry['range'] = other['range']


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
    print('// Source: Bizerba GxNet subfunction reference.')
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


def merge(into, extra):
    """Fills gaps in `into` from `extra`, field by field rather than entry by
    entry: an edition may describe a subfunction without naming its values, or
    the other way round, and taking whole entries would discard the half that
    the first edition happens to be missing."""
    for token, other in extra.items():
        entry = into.setdefault(token, {'desc': '', 'range': '', 'vals': {}, 'see': ''})
        if not entry['desc']:
            entry['desc'] = other['desc']
        if not entry['range']:
            entry['range'] = other['range']
        if not entry['see']:
            entry['see'] = other['see']
        for value, text in other['vals'].items():
            entry['vals'].setdefault(value, text)


if __name__ == '__main__':
    # The registry is the .hpp; everything else is a source, in the order its
    # wording is preferred.
    registry = [a for a in sys.argv[1:] if a.endswith('.hpp')]
    sources = [a for a in sys.argv[1:] if a.endswith('.md')]
    if len(registry) != 1 or not sources:
        print(__doc__, file=sys.stderr)
        sys.exit(2)

    entries = extract(sources[0])
    for source in sources[1:]:
        merge(entries, extract(source))

    tokens, by_name = known_tokens(registry[0])
    resolve(entries, by_name)
    emit(entries, tokens)
