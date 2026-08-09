#!/usr/bin/env python3
"""Convert a compiled HTML help file (.chm) to a single markdown document.

Usage:
    python3 chm2md.py ixnet.chm > IxNet.md
    python3 chm2md.py extracted-dir/ > IxNet.md

Extraction needs `7z` on PATH; an already extracted directory is accepted too.

Why not a generic html-to-text pass: the documents this exists for are
reference manuals, and everything worth having in them is in tables. A
converter that flattens a table into prose loses the column boundaries, and
with them the difference between a value range and a description. Tables are
therefore emitted as markdown tables, with line breaks inside a cell kept as
`<br>` so that a later pass can still tell rows apart.

Page order and heading depth come from the help file's own table of contents
(`.hhc`), so the result reads in the order the book was written in rather than
in whatever order the archive happens to store its pages.
"""

import html
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.parse

from bs4 import BeautifulSoup, NavigableString, Tag

BLOCK = {'p', 'div', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'tr', 'li', 'br', 'table', 'ul', 'ol', 'pre'}


def read_html(path):
    """Decodes a page using the charset it declares, falling back to cp1252."""
    raw = open(path, 'rb').read()
    match = re.search(rb'charset=["\']?([\w-]+)', raw[:4096], re.I)
    encoding = match.group(1).decode('ascii', 'replace') if match else 'cp1252'
    try:
        return raw.decode(encoding, errors='replace')
    except LookupError:
        return raw.decode('cp1252', errors='replace')


def inline(node):
    """Renders inline content, keeping emphasis and dropping internal links."""
    if isinstance(node, NavigableString):
        return re.sub(r'\s+', ' ', html.unescape(str(node)))
    if not isinstance(node, Tag):
        return ''

    if node.name in ('script', 'style'):
        return ''
    if node.name == 'br':
        return '\n'

    text = ''.join(inline(child) for child in node.children)

    if node.name in ('b', 'strong'):
        return f'**{text.strip()}**' if text.strip() else ''
    if node.name in ('i', 'em'):
        return f'*{text.strip()}*' if text.strip() else ''
    if node.name in ('code', 'tt', 'samp', 'kbd'):
        return f'`{text.strip()}`' if text.strip() else ''
    if node.name == 'a':
        href = node.get('href', '')
        # External links keep their target; internal ones would point at pages
        # that do not exist once everything is one file.
        if href.startswith(('http://', 'https://', 'mailto:')):
            return f'[{text.strip()}]({href})'
        return text
    if node.name == 'img':
        alt = (node.get('alt') or '').strip()
        return f'![{alt}]()' if alt else ''
    return text


def cell_text(td):
    text = ''.join(inline(child) for child in td.children)
    text = re.sub(r'[ \t]+', ' ', text).strip()
    # A cell may hold several lines; markdown tables are one line per row.
    text = re.sub(r'\s*\n\s*', '<br>', text)
    return text.replace('|', '\\|')


def render_table(table):
    rows = []
    for tr in table.find_all('tr'):
        cells = [cell_text(td) for td in tr.find_all(['td', 'th'])]
        if any(c for c in cells):
            rows.append(cells)
    if not rows:
        return []

    width = max(len(r) for r in rows)
    rows = [r + [''] * (width - len(r)) for r in rows]

    # Markdown has no table without a header row, and these tables rarely mark
    # one up. The first row is used as the header, which is what it is in every
    # reference table seen here.
    out = ['| ' + ' | '.join(rows[0]) + ' |', '|' + '---|' * width]
    for row in rows[1:]:
        out.append('| ' + ' | '.join(row) + ' |')
    return out


def render(node, depth, out):
    """Walks the document, appending markdown blocks to `out`."""
    if isinstance(node, NavigableString):
        text = re.sub(r'\s+', ' ', html.unescape(str(node))).strip()
        if text:
            out.append(text)
        return
    if not isinstance(node, Tag) or node.name in ('script', 'style', 'head'):
        return

    name = node.name

    if name == 'table':
        out.extend([''] + render_table(node) + [''])
        return

    if re.fullmatch(r'h[1-6]', name or ''):
        level = min(int(name[1]) + depth, 6)
        text = ''.join(inline(c) for c in node.children).strip()
        if text:
            out.extend(['', '#' * level + ' ' + text, ''])
        return

    if name == 'pre':
        out.extend(['', '```', node.get_text().rstrip(), '```', ''])
        return

    if name in ('ul', 'ol'):
        ordered = name == 'ol'
        out.append('')
        for i, li in enumerate(node.find_all('li', recursive=False), 1):
            text = ''.join(inline(c) for c in li.children).strip()
            text = re.sub(r'\s*\n\s*', ' ', text)
            if text:
                out.append(f'{i}. {text}' if ordered else f'- {text}')
        out.append('')
        return

    if name == 'p':
        text = ''.join(inline(c) for c in node.children)
        text = re.sub(r'[ \t]+', ' ', text).strip()
        if text:
            out.extend(['', text, ''])
        return

    for child in node.children:
        render(child, depth, out)


def page_markdown(path, depth):
    soup = BeautifulSoup(read_html(path), 'lxml')
    body = soup.body or soup
    out = []
    render(body, depth, out)

    lines = []
    for block in out:
        lines.extend(block.split('\n'))
    text = '\n'.join(lines)
    text = re.sub(r'\n{3,}', '\n\n', text)
    return text.strip()


def all_pages(root):
    return sorted(
        os.path.join(dirpath, f)
        for dirpath, _, files in os.walk(root)
        for f in files
        if f.lower().endswith(('.htm', '.html'))
    )


def contents(root):
    """Ordered (title, path, depth) from the help file's own table of contents.

    The contents file decides order and nesting, but it does not decide what
    gets converted: a .chm can carry pages it never lists, and one here lists
    only three of its eleven. Anything unlisted is therefore appended rather
    than dropped, which is also what happens when there is no .hhc at all.
    """
    hhc = next((os.path.join(root, f) for f in os.listdir(root) if f.lower().endswith('.hhc')), None)
    items = []
    taken = set()

    if hhc:
        soup = BeautifulSoup(read_html(hhc), 'lxml')
        for obj in soup.find_all('object'):
            name = local = None
            for param in obj.find_all('param'):
                if param.get('name', '').lower() == 'name':
                    name = param.get('value')
                elif param.get('name', '').lower() == 'local':
                    local = param.get('value')
            if not (name and local):
                continue
            # Nesting depth is how many <ul> the entry sits inside.
            depth = len(obj.find_parents('ul')) - 1
            # The paths are URL-encoded, and a page whose name contains a space
            # is otherwise looked for under its escaped spelling and not found.
            path = os.path.join(root, urllib.parse.unquote(local).replace('\\', '/'))
            if os.path.exists(path):
                items.append((name, path, max(depth, 0)))
                taken.add(os.path.normcase(os.path.abspath(path)))

    for path in all_pages(root):
        if os.path.normcase(os.path.abspath(path)) in taken:
            continue
        items.append((os.path.splitext(os.path.basename(path))[0], path, 0))

    return items


def main(source, title=None):
    temp = None
    if os.path.isdir(source):
        root = source
    else:
        if not shutil.which('7z'):
            sys.exit('chm2md: 7z is needed to extract a .chm')
        temp = tempfile.mkdtemp(prefix='chm2md-')
        subprocess.run(['7z', 'x', '-o' + temp, source], check=True, stdout=subprocess.DEVNULL)
        root = temp

    try:
        pages = contents(root)
        if not pages:
            sys.exit('chm2md: no pages found')

        print('# ' + (title or os.path.splitext(os.path.basename(source))[0]))
        print()
        seen = set()
        for name, path, depth in pages:
            if path in seen:
                continue
            seen.add(path)
            print('#' * min(depth + 2, 6) + ' ' + name)
            print()
            body = page_markdown(path, depth + 2)
            if body:
                print(body)
                print()
        print(f'chm2md: {len(seen)} pages', file=sys.stderr)
    finally:
        if temp:
            shutil.rmtree(temp, ignore_errors=True)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
