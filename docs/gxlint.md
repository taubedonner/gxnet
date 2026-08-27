# `gxlint`

A filter for working through a captured communication log. It expands tokens to their symbolic names, decodes payload values and reports anything the target device would not accept.

```sh
gxlint --device 16.40 < capture.commlog
```

```
A!PV04|PW02|GW09|GL19|GL1A|GL16|PD00|GL2B|GL2C
  PV04  PSV_DATA  [since 6.21]
    PW02  PSW_PCKHDL  [since 6.21]
    GW09  GGW_AUSZEICH_ART  [since 6.21]
    GL19  GGL_PLUNR  [since 6.21]
    ...
  data: 1|2|4711|0|1|KG;-3;100|20997|1545
    GL19 = 4711
    PD00 = KG;-3;100  (= 0.100000 KG)
```

Exit status is non-zero when any error was found, so it drops straight into a build or a smoke test. `--quiet` suppresses the annotation and keeps only the findings, and `--help` prints the input shapes it accepts.

## Binary frames

Log lines carrying the compact binary form are recognised automatically, so a capture can be piped in as-is:

```
  frame D0710201
  MV08  MDV_SEQUENZ_END  [since 6.21]
    PV04  PSV_DATA  [since 6.21]
      GL19  GGL_PLUNR  [since 6.21] = 1000
      GT52  GGT_CODE2  [since 6.21] = 0100000000000001211SAMPL...
      PD00  PSD_GEW_NETTO_EINZEL  [since 6.21] = KG;-3;722  (= 0.722000 KG)
```

Round-trips are byte-exact: a captured frame re-encodes to itself. This is what makes the server's own logs readable – see [`bcs-notes.md`](bcs-notes.md), *Reading the server's own logs*.

Server notes (the `--` lines) and comments (`;` or `#`) are printed and not parsed, so an annotated capture stays readable and a real log does not report one spurious error per housekeeping line.

## The sample capture

`examples/sample.commlog` is a worked example, and it is generated rather than captured: a real communication log carries device licences, addresses and live marking codes. Every frame in it was produced by this project's own binary encoder from a telegram a device really was sent or really answered, so the bytes are the genuine encoding of an invented conversation – a version query, a standard dialog acknowledged, an outgoing-line list, a package record with the "no unique data" flag set, and the same dialog refused for carrying `WW62`.

```sh
gxlint --device 16.40 < examples/sample.commlog
```

## What the optional semantics table adds

With `registry_docs.hpp` built locally, `gxlint` names the value beside the number instead of leaving it bare, and `--meaning` adds what each subfunction is for. Without it everything still works and those two simply report nothing. See [`registry.md`](registry.md).
