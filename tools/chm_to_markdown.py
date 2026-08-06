"""Turns an extracted CHM into one markdown file.

    7z x -o<dir> docs/Help/English/ixnet.chm
    python3 tools/chm_to_markdown.py <dir> IxNet docs/markdown/IxNet.md


The CHM is a RoboHelp export: a handful of HTML pages whose real content is
tables. textutil flattens a table to one cell per line, which loses the column
structure, so the tables are read here instead and re-emitted as markdown.
"""
import html, pathlib, re, sys, urllib.parse
from html.parser import HTMLParser


class Page(HTMLParser):
    """Collects a page as a sequence of paragraphs and tables."""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.blocks = []          # ("p", text) | ("table", [[cell, ...], ...])
        self.table = None
        self.row = None
        self.cell = None
        self.text = []
        self.skip = 0

    def handle_starttag(self, tag, attrs):
        # RoboHelp inlines a page of Word CSS and a script block in every file.
        if tag in ("style", "script"):
            self.skip += 1
            return
        if self.skip:
            return
        if tag == "table":
            self.flush_text()
            self.table = []
        elif tag == "tr" and self.table is not None:
            self.row = []
        elif tag in ("td", "th") and self.row is not None:
            self.cell = []
        elif tag == "br":
            (self.cell if self.cell is not None else self.text).append(" ")

    def handle_endtag(self, tag):
        if tag in ("style", "script"):
            self.skip = max(0, self.skip - 1)
            return
        if self.skip:
            return
        if tag == "table" and self.table is not None:
            if self.table:
                self.blocks.append(("table", self.table))
            self.table = None
        elif tag == "tr" and self.row is not None:
            if any(c.strip() for c in self.row):
                self.table.append(self.row)
            self.row = None
        elif tag in ("td", "th") and self.cell is not None:
            self.row.append(clean("".join(self.cell)))
            self.cell = None
        elif tag in ("p", "div", "h1", "h2", "h3", "h4", "li"):
            self.flush_text()

    def handle_data(self, data):
        if self.skip:
            return
        (self.cell if self.cell is not None else self.text).append(data)

    def flush_text(self):
        if self.cell is not None or self.table is not None:
            return
        line = clean("".join(self.text))
        self.text = []
        if line:
            self.blocks.append(("p", line))


def clean(value):
    return re.sub(r"\s+", " ", html.unescape(value)).strip()


def render(blocks):
    out = []
    for kind, payload in blocks:
        if kind == "p":
            out.append(payload)
            continue
        width = max(len(row) for row in payload)
        rows = [row + [""] * (width - len(row)) for row in payload]
        # A one-column table is a layout device, not data.
        if width == 1:
            out.extend(row[0] for row in rows if row[0])
            continue
        out.append("| " + " | ".join(c.replace("|", "\\|") for c in rows[0]) + " |")
        out.append("|" + "---|" * width)
        for row in rows[1:]:
            out.append("| " + " | ".join(c.replace("|", "\\|") for c in row) + " |")
    return out


def read(path):
    raw = path.read_bytes()
    for encoding in ("utf-8", "cp1252", "latin-1"):
        try:
            return raw.decode(encoding)
        except UnicodeDecodeError:
            continue
    return raw.decode("latin-1", "replace")


def main(root, title, out_path):
    root = pathlib.Path(root)
    toc = read(root / "ixnet.hhc")
    entries = re.findall(
        r'<param name="Name" value="([^"]*)"[^>]*>\s*<param name="Local" value="([^"]*)"', toc, re.I)

    lines = [f"# {title}", ""]
    seen = set()
    for name, local in entries:
        if local.startswith("http"):
            continue
        target = root / urllib.parse.unquote(local)
        if not target.exists() or target in seen:
            continue
        seen.add(target)

        page = Page()
        page.feed(read(target))
        page.flush_text()
        body = render(page.blocks)
        # The first line of every page repeats its own title.
        if body and clean(body[0]).lower() == clean(html.unescape(name)).lower():
            body = body[1:]

        lines.append(f"## {html.unescape(name)}")
        lines.append("")
        lines.append(f"<!-- {local} -->")
        lines.append("")
        lines.extend(body)
        lines.append("")

    pathlib.Path(out_path).write_text("\n".join(lines) + "\n")
    print(f"{out_path}: {len(seen)} pages, {len(lines)} lines")


if __name__ == "__main__":
    main(*sys.argv[1:])
