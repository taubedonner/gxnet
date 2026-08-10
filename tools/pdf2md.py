#!/usr/bin/env python3
"""Convert a PDF reference manual to markdown, keeping its tables as tables.

Usage:
    python3 pdf2md.py GxNet_de_de.pdf > GxNet-de.md

Needs `pdfplumber` (pip install pdfplumber).

The point is the tables. A vendor reference puts the value range in one column
and its meaning in the next, and a converter that extracts running text merges
the two into a single line with no separator: the range then cannot be told
from the description by anything but guesswork. Extracting the table structure
first, and the text only from what is left over, keeps that boundary.

Line breaks inside a cell become `<br>`, matching what a markdown table can
hold on one row.

Cross references are kept as links. A manual refers to itself constantly, and
in the PDF those references are clickable while the printed number beside them
belongs to a different numbering than the file's own pages. Extracting the link
annotations resolves that: every page gets an anchor, and a reference becomes
`[GGW_LAND](#page-113)`.
"""

import re
import sys

import pdfplumber
from pdfminer.pdftypes import resolve1


def cell(value):
    text = re.sub(r'[ \t]+', ' ', (value or '')).strip()
    text = re.sub(r'\s*\n\s*', '<br>', text)
    return text.replace('|', '\\|')


def columns(found):
    rows = found.extract()
    return max((len(r) for r in rows), default=0)


def render_table(table):
    rows = [[cell(c) for c in row] for row in table]
    rows = [r for r in rows if any(r)]
    if not rows:
        return []

    width = max(len(r) for r in rows)
    rows = [r + [''] * (width - len(r)) for r in rows]

    out = ['| ' + ' | '.join(rows[0]) + ' |', '|' + '---|' * width]
    for row in rows[1:]:
        out.append('| ' + ' | '.join(row) + ' |')
    return out


def destination(annotation, page_numbers, document):
    """The page a link annotation leads to, or None if it leads elsewhere.

    A destination is normally an array whose first element references the
    target page object; it may also be a name to be looked up in the document,
    and it may sit behind a GoTo action rather than in the annotation itself.
    Anything else -- a URL, another file -- is not a page of this document.
    """
    data = annotation.get('data') or {}
    dest = data.get('Dest')
    if dest is None:
        action = resolve1(data.get('A')) or {}
        dest = action.get('D') if isinstance(action, dict) else None
    dest = resolve1(dest)

    if isinstance(dest, (bytes, str)):
        try:
            dest = resolve1(document.get_dest(dest))
        except Exception:
            return None
        if isinstance(dest, dict):
            dest = resolve1(dest.get('D'))

    if not isinstance(dest, list) or not dest:
        return None
    return page_numbers.get(getattr(dest[0], 'objid', None))


def page_links(page, page_numbers):
    """Every cross reference on the page, as (text, target page number).

    The text is read back out of the rectangle the annotation covers, because
    that is the only thing the extracted markdown and the annotation have in
    common: by then the words have been through table extraction and carry no
    coordinates any more.
    """
    found = []
    for annotation in page.annots or []:
        target = destination(annotation, page_numbers, page.pdf.doc)
        if target is None:
            continue
        box = (annotation['x0'], annotation['top'], annotation['x1'], annotation['bottom'])
        text = page.crop(box, strict=False).extract_text() or ''
        text = re.sub(r'\s+', ' ', text).strip(' ,.;:')
        if text:
            found.append((text, target))
    return found


def with_links(text, links):
    """Rewrites cross-referenced words as markdown links.

    Only where the match is certain: the words must occur in the page exactly
    as often as there are annotations carrying them, and every one of those
    must lead to the same page. A page number is a couple of digits and turns
    up in ordinary content as well, so a rule less strict than this one would
    quietly link the wrong number.

    All of it happens in a single pass, so nothing this produces can be matched
    again by a shorter reference.
    """
    targets = {}
    for word, target in links:
        targets.setdefault(word, []).append(target)

    linked = {}
    for word, pages in targets.items():
        if len(set(pages)) != 1:
            continue
        pattern = r'(?<![\w#-])' + re.escape(word) + r'(?![\w-])'
        if len(re.findall(pattern, text)) == len(pages):
            linked[word] = pages[0]

    if not linked:
        return text, targets

    # Longest first: PSV_PCK must not consume the start of PSV_PCK_LONG.
    order = sorted(linked, key=len, reverse=True)
    pattern = r'(?<![\w#-])(' + '|'.join(re.escape(w) for w in order) + r')(?![\w-])'
    text = re.sub(pattern, lambda m: '[%s](#page-%d)' % (m.group(1), linked[m.group(1)]), text)

    return text, {w: t for w, t in targets.items() if w not in linked}


def page_markdown(page):
    """Tables first, then the text that is not inside one.

    `extract_text` on a page with tables returns the cells too, in reading
    order and without their boundaries, so the same content would appear twice
    and the second copy would be the unusable one. Blanking the table areas
    before taking the text is what keeps them apart.
    """
    out = []

    # A single-column "table" is a text frame, not a table. These manuals are
    # laid out in boxes, so accepting them turns every ordinary paragraph into
    # a one-cell row and makes the result harder to read than the PDF.
    tables = [t for t in page.find_tables() if columns(t) >= 2]

    for found in tables:
        out.extend([''] + render_table(found.extract()) + [''])

    remainder = page
    for found in tables:
        try:
            remainder = remainder.outside_bbox(found.bbox)
        except ValueError:
            # A table whose bbox covers the page leaves nothing outside it.
            return out

    text = remainder.extract_text() or ''
    paragraph = []
    for line in text.split('\n'):
        line = line.strip()
        if line:
            paragraph.append(line)
        elif paragraph:
            out.extend(['', ' '.join(paragraph), ''])
            paragraph = []
    if paragraph:
        out.extend(['', ' '.join(paragraph), ''])
    return out


def main(path, title=None):
    pdf = pdfplumber.open(path)
    print('# ' + (title or path.rsplit('/', 1)[-1].rsplit('.', 1)[0]))
    print()

    page_numbers = {page.page_obj.pageid: number for number, page in enumerate(pdf.pages, 1)}

    tables = 0
    found = inlined = 0
    for number, page in enumerate(pdf.pages, 1):
        blocks = page_markdown(page)
        if not any(b.strip() for b in blocks):
            continue
        tables += len([t for t in page.find_tables() if columns(t) >= 2])

        text = re.sub(r'\n{3,}', '\n\n', '\n'.join(blocks)).strip()
        links = page_links(page, page_numbers)
        text, left = with_links(text, links)
        found += len(links)
        inlined += len(links) - sum(len(t) for t in left.values())

        # The page number is the only stable way back into the original, and a
        # note in these manuals is usually cited by page. The anchor is what
        # the cross references above point at.
        print(f'<!-- page {number} -->')
        print(f'<a id="page-{number}"></a>')
        print()
        print(text)
        if left:
            # Ambiguous in the text, so still worth recording: the reference is
            # in the original whether or not it can be placed here.
            pairs = '; '.join('%s -> %s' % (word, ', '.join(str(t) for t in sorted(set(pages))))
                              for word, pages in sorted(left.items()))
            print(f'<!-- cross references not placed: {pairs} -->')
        print()

    print(f'pdf2md: {len(pdf.pages)} pages, {tables} tables, {inlined} of {found} cross references linked',
          file=sys.stderr)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
