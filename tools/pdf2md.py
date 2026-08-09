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
"""

import re
import sys

import pdfplumber


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

    tables = 0
    for number, page in enumerate(pdf.pages, 1):
        blocks = page_markdown(page)
        if not any(b.strip() for b in blocks):
            continue
        tables += len([t for t in page.find_tables() if columns(t) >= 2])
        # The page number is the only stable way back into the original, and a
        # note in these manuals is usually cited by page.
        print(f'<!-- page {number} -->')
        text = '\n'.join(blocks)
        print(re.sub(r'\n{3,}', '\n\n', text).strip())
        print()

    print(f'pdf2md: {len(pdf.pages)} pages, {tables} tables', file=sys.stderr)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
