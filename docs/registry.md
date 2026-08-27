# Regenerating the registry

The subfunction table in `core/include/gxnet/detail/registry_table.hpp` is generated from the vendor reference, which is **not in this repository**. Keep a local copy; the export and the intermediate markdown are gitignored.

```sh
python3 tools/pdf2md.py GxNet_de_de.pdf > docs/markdown/GxNet-de.md
python3 tools/gen_registry.py docs/markdown/GxNet-de.md docs/markdown/GxNet.md \
    > core/include/gxnet/detail/registry_table.hpp
```

The generator reads the markdown export of the vendor subfunction reference and emits the lookup table. Re-run it whenever you receive a newer revision; nothing else needs to change.

Names and versions are identifiers, and identifiers are tracked in git – that table is committed.

## The optional semantics table

What a subfunction is *for* and what its values *mean* is the manual's own prose, so that table is built locally and stays out of the repository:

```sh
python3 tools/gen_docs.py core/include/gxnet/detail/registry_table.hpp \
    docs/markdown/GxNet.md docs/markdown/GxNet-de.md \
    > core/include/gxnet/detail/registry_docs.hpp
```

`registry.hpp` picks it up with `__has_include`. Without it everything still compiles and `tokenMeaning`, `tokenValues` and `tokenValueName` simply report nothing – that is the state of a fresh checkout, and the test suite covers both.

With the table, `gxlint` names the value beside the number instead of leaving it bare, and `--meaning` adds what each subfunction is for.

The English edition comes first and the German fills its gaps: the two carry the same date and the English one is a partial translation, so about one description in twenty stays German. See [`gxnet-notes.md`](gxnet-notes.md), *How to read these notes*.
