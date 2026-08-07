# gxnet

A portable C++20 implementation of the **GxNet** telegram language used by
Bizerba Gx-family devices (price-labelling lines, checkweighers, industrial
scales).

The library is deliberately **transport-free**. It knows how to build, encode,
parse and check telegrams; it never opens a socket, loads a DLL or touches the
registry. Feed the strings it produces to whatever channel you already have —
the BCS COM interface, `_connectService`, a file drop — and hand it back what
comes in.

- C++20, no third-party dependencies
- Reads both the textual form and the binary form found in communication logs
- Builds as a static library; a C ABI header is provided for embedding
- 1917 subfunctions with symbolic names and introducing software versions
- 407 self-checks across three binaries, no external test framework
- Clean under AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer

---

## Why version checking is the point

Every subfunction in the vendor reference carries the software release that
introduced it. A device running an older release does not report an error for a
command it has never heard of — it does nothing. That failure mode is expensive
on a production line, because the sequence *looks* like it worked.

```cpp
gxnet::ValidateOptions opts;
opts.device_version = *gxnet::Version::parse("16.40");   // from A?ST8D

auto diags = gxnet::validate(telegram, opts);
if (!gxnet::hasNoErrors(diags)) {
    for (const auto& d : diags) std::cerr << d.str() << "\n";
    return;   // do not send
}
```

```
error [version.too_new] SW9B: SRW_UNIQUE_PCK_DATA_READY requires
      software 16.40 but the device runs 14.00
```

Query the device once at start-up with `A?ST8D` (`SRT_GX_VERSION`, format
`MM.mm.bbbb`), cache the result, and every telegram you build afterwards is
checked against it for free.

---

## What is in the repository

```
core/            the library: representation, encoding, parsing, validation
link/            transport: Transport, MockTransport, BcsTransport, Worker,
                 and the composite telegrams built on them
app/             gxdemo, a wxWidgets bench over the transport
examples/        gxlint, which annotates and validates captures, and a
                 sample.commlog to point it at
tests/           plain asserts, no framework: core, transport, and gxdemo's
                 own model
tools/           gen_registry.py, and a read-only BCS introspection script
docs/            three notes files, described below
.github/         CI: Windows first, Linux for the sanitizers
dist/            packaged builds, gitignored

Each component has its own `include/` and `src/`.
```

The library is the part with no dependencies. `link/` speaks to a device
through the vendor's `_connect.BRAIN` server rather than the wire, and `app/` is
a separate target that is off by default.

**The vendor manuals are not here.** They are Bizerba's, and this repository is
public. What stands in for them:

| File | What it covers |
|---|---|
| [`docs/gxnet-notes.md`](docs/gxnet-notes.md) | the telegram language: codes, encodings, the rules stated once and far from where they are needed |
| [`docs/bcs-notes.md`](docs/bcs-notes.md) | `_connect.BRAIN`, the COM/DCOM server that carries the telegrams |
| [`docs/machine-notes.md`](docs/machine-notes.md) | how a GLM installation is wired: channels, outgoing lines, the memory card, unique data |

Every claim in them cites the manual by symbolic name, subfunction code and,
where a name is not enough, a chapter and a short German quote — so the
originals stay searchable without being redistributed. `gen_registry.py` needs a
local copy of `docs/markdown/GxNet.md`, which is gitignored.

---

## Building

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
```

Three test binaries, all run by `ctest`: `gxnet_tests` for the library,
`gxnet_link_tests` for the transport, and `gxdemo_tests` for the application's
model — the connection, the log, the spontaneous listeners, the firmware gate.
That last one links one source file out of `app/` and needs no window server,
because `Session` deliberately includes no wxWidgets header.

`gxdemo_tests` ends with a soak: connect, work, disconnect, repeatedly. It
asserts on the containers rather than on resident size, because an allocator
claims arenas and keeps them, so RSS rises and then plateaus even when nothing
leaks. What must stay flat is what the program owns — the log against its
ceiling, the listeners against their subscriptions, the pending queue against
zero. `GXNET_SOAK_CYCLES=5000` turns it into a real soak; the default keeps the
whole suite under a second.

Sanitizers:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DGXNET_SANITIZE="address;undefined"
cmake --build build-asan -j && ctest --test-dir build-asan
```

`GXNET_SANITIZE` is passed straight to `-fsanitize`, so `thread` works too and
has to be built on its own. Clang and GCC only.

**LeakSanitizer exists on Linux and nowhere else** — not on macOS, and not on
Windows in any toolchain, MSVC and clang-cl included. So leak checking is a
Linux job, which is what CI uses it for; on Windows, Dr. Memory or UMDH fill the
gap, and a COM reference leak is invisible to all of them anyway because it is
not a heap leak. AddressSanitizer itself does cross-compile with llvm-mingw and
runs on Windows.

Or without CMake:

```sh
g++ -std=c++20 -Icore/include -Icore/src core/src/*.cpp tests/test_gxnet.cpp -o gxnet_tests
```

The application needs `-DGXNET_BUILD_APP=ON` and fetches wxWidgets through CPM,
which is why it is off by default: the library keeps building in seconds with no
network. Cross-compiling it for Windows from a Mac:

```sh
cmake -S . -B build-win -DGXNET_BUILD_APP=ON -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=app/cmake/llvm-mingw-x86_64.toolchain.cmake
```

llvm-mingw links libc++ and compiler-rt statically, so the result is one
self-contained executable.

---

## Usage

### Building a telegram

```cpp
#include "gxnet/gxnet.hpp"
using namespace gxnet;

Telegram t = Builder(Family::Automatic, Access::Write)
                 .block("PV04")
                     .word("PW02", 7)
                     .long_("GL19", 1)
                     .dimension("PD00", Dimension("KG", -3, 1064))
                 .end()
                 .build();

encodeHeader(t.header);                     // "A!PV04|PW02|GL19|PD00|LX02"
*encodeRecord(t.header, t.records.front()); // "7|1|KG;-3;1064"
*encodeOneLine(t);                          // "A!PV04|PW02|7|GL19|1|PD00|KG;-3;1064|LX02"
```

The builder is type-checked: `word("GL19", 5)` fails at build time because
`GL19` is a Long, not a Word.

### Reading a value back

```cpp
Telegram probe = Builder(Family::Automatic, Access::Read)
                     .query("SW9B")     // no value: the device supplies it
                     .build();
*encodeOneLine(probe);                  // "A?SW9B"
```

### Parsing what the device sent

```cpp
auto header = parseHeader("A!PV04|PW02|GW09|GL19|GL1A|GL16|PD00|GL2B|GL2C");
auto record = parseRecord(*header, "1|2|4711|0|1|KG;-3;100|20997|1545");

const Dimension& weight = std::get<Dimension>((*record)[5]);
weight.mantissa;   // 100
weight.exponent;   // -3      -> 0.100 KG
```

Both transmission forms are supported: separate header and data lines (the
two-argument `Send`), and the interleaved single line (`SendOne`), via
`parseLines` and `parseOneLine`. Encoding is byte-exact, so a captured telegram
re-encodes to itself.

### Looking a subfunction up

```cpp
tokenName(*Token::parse("GW7D"));   // "GGW_UNIQUE_DATEN"
tokenByName("GGL_PLUNR");           // GL19

auto info = Registry::builtin().find(*Token::parse("SW9B"));
info->since;      // 16.40
info->reserved;   // true when the reference lists no description
```

---

## Reading a communication log

Device logs record the compact binary form, not the textual one. `binary::parse`
decodes it into the same node tree, so validation and re-encoding work unchanged.

```cpp
gxnet::binary::Options opts;
opts.units = gxnet::binary::inferredUnitTable();

auto frame = gxnet::binary::parseHex("90770102D1060BB8", opts);
frame->nodes[0].token.str();   // "MW06"  (MDW_GET_BUFF)
```

`gxlint` recognises log lines automatically, so a capture can be piped in as-is:

```
  frame D0710201
  MV08  MDV_SEQUENZ_END  [since 6.21]
    PV04  PSV_DATA  [since 6.21]
      GL19  GGL_PLUNR  [since 6.21] = 1000
      GT52  GGT_CODE2  [since 6.21] = 0100000000000001211SAMPL...
      PD00  PSD_GEW_NETTO_EINZEL  [since 6.21] = KG;-3;722  (= 0.722000 KG)
```

Round-trips are byte-exact: a captured frame re-encodes to itself.

## `gxlint`

A filter for working through a captured communication log. It expands tokens to
their symbolic names, decodes payload values and reports anything the target
device would not accept.

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

Exit status is non-zero when any error was found, so it drops straight into a
build or a smoke test. `--quiet` suppresses the annotation and keeps only the
findings, and `--help` prints the input shapes it accepts.

`examples/sample.commlog` is a worked example, and it is generated rather than
captured: a real communication log carries device licences, addresses and live
marking codes. Every frame in it was produced by this project's own binary
encoder from a telegram a device really was sent or really answered, so the
bytes are the genuine encoding of an invented conversation — a version query, a
standard dialog acknowledged, an outgoing-line list, a package record with the
"no unique data" flag set, and the same dialog refused for carrying `WW62`.

```sh
gxlint --device 16.40 < examples/sample.commlog
```

Server notes (the `--` lines) and comments (`;` or `#`) are printed and not
parsed, so an annotated capture stays readable and a real log does not report
one spurious error per housekeeping line.

---

## The C ABI

`core/include/gxnet/gxnet_c.h` exposes lookup, validation and form conversion across
a plain C boundary — for loading as a DLL from a service written in another
language, or from an ERP runtime.

```c
gxnet_token_info_t info;
gxnet_lookup_token("GW7D", &info);       /* info.name, info.since, info.arity */

char diags[4096];
int errors = gxnet_validate("A!GW7D", "0", "16.40", diags, sizeof diags);
```

---

## Protocol notes

Details that cost time to establish, recorded so the next reader does not have
to rediscover them.

**Token layout.** Four characters: group letter, type letter, two hexadecimal
index digits. The command class code is `(group << 4) | type` — `GW7D` is
`0x01`, `XX13` is `0xA0`, `PV04` is `0x36`.

| Type | Letter | Payload fields |
|---|---|---|
| Command | `X` | 0 |
| Word | `W` | 1, 16-bit signed |
| Long | `L` | 1, 32-bit signed |
| Dimension | `D` | 1, `unit;exponent;mantissa` |
| Block | `V` | 0, may be closed by `LX02` |
| Text | `T` | 1, escaped |

**Blocks need not be closed.** The reference shows both forms: `PV04|…|LX02`
and `PV04|…` running to the end of the header. Which form was used is kept in
`Node::explicit_close` so round-trips stay exact.

**Group letters are open-ended.** The bundled table covers the Gx family. The Ix
family uses further letters — `RX01` and `RX04` appear in registration
telegrams — so the parser accepts any upper case group letter and the validator
reports unknown ones as a *warning*, never an error. Data type letters are
checked strictly, because payload arity depends on them.

**Read requests carry no data line.** `A?GW7D` is the whole telegram. A payload
token with no value is legal for `Access::Read` and an error for
`Access::Write`.

**Escaping.** Control characters, `@` and `|` become `@` plus two upper case hex
digits: `@0A`, `@40`, `@7C`. `EscapeOptions` can additionally escape `;` and
bytes above `0x7F`.

**Legacy format.** The bare `!` / `?` prefix (`Family::Legacy`) switches the
dimension sub-separator from `;` to `|`.

**Binary form.** `[frame header: 4][class][index][payload]…`, the class code the
same as in the textual form. Not vendor-documented; reverse engineered from
captures and confirmed by byte-exact round-trips.

- Block length fields count everything after the length, **including** the
  closing `LX02`.
- Text payloads are padded to an even byte count and **the pad is not counted**
  in the length. Missing this desynchronises a parser several fields later,
  which is exactly how the first implementation failed.
- The dimension unit field packs a unit code in the upper ten bits and a
  six-bit two's-complement exponent in the lower six. `0x00FD` is unit 3,
  exponent −3. The unit codes themselves are inferred, not documented;
  unmapped ones render as `#N` and round-trip.
- In the frame header, the first byte is the direction: **`0x90` is `?`, `0xD0`
  is `!`**. Every reply begins `0xD0`, and correctly so — a reply is a write
  telegram, the device writing the value back. Bytes 3 and 4 are the bus
  addresses, source then destination. The second byte varies with the payload
  type of the leading token and is not fully explained.

**Acknowledgements.** A write is answered by `LGW_QUIT_OK` carrying the class
code of the command that succeeded, or by an `LGV_QUIT` block carrying
`LGW_RETURN` (the reason), `LGW_UFKENN` (the class code that failed) and
`LGW_DEBUG` (an internal error number). `LGW_RETURN` 4 is "third-party command",
which is what a device answers for a subfunction its software release does not
have.

**Encoding.** Everything is byte-oriented. `std::string` payloads are treated as
opaque bytes, so UTF-8 passes through untouched; the default escape set leaves
bytes above `0x7F` alone. Convert to and from wide strings only at the host
boundary, never inside.

---

## Talking to a device

`link/` carries telegrams to a device through the vendor's `_connect.BRAIN`
server rather than over the wire. What follows was established against a running
installation and is not in the manual.

**The entry point is misspelled in the manual.** The COM object registers as
`BCS.BCSComunnication.1` — one `m`, two `n`. The documented
`BCS.BCSCommunication.1` does not exist. Of the three interface versions only
`.1` is complete: `.2` and `.3` are progressively stripped and lack `SendOne`,
`ReceiveOne` and the receive-queue methods.

### The automation interface

`Get-Member` on the object, which is the only complete listing there is — the
manual documents a subset and omits the FTP family entirely.

```
TypeName: System.__ComObject#{47f810e1-2902-11d4-a81e-444553540000}
```

| Method | Signature |
|---|---|
| `Open` | `int Open (string, string, short, short, short)` |
| `Close` | `int Close ()` |
| `Reset` | `int Reset (string)` |
| `Send` | `int Send (string, string, string, int, int)` |
| `SendOne` | `int SendOne (string, string, int, int)` |
| `SendCheck` | `int SendCheck (string, int, int)` |
| `Receive` | `int Receive (string, string, string, int, int)` |
| `ReceiveOne` | `int ReceiveOne (string, string, int, int)` |
| `ReceiveWithoutAck` | `int ReceiveWithoutAck (string, string, string, int, int)` |
| `ReceiveOneWithoutAck` | `int ReceiveOneWithoutAck (string, string, int, int)` |
| `SendAcknowledge` | `int SendAcknowledge (string)` |
| `SendAcknowledgeNeg` | `int SendAcknowledgeNeg (string, int, string)` |
| `CreateReceiveQueue` | `int CreateReceiveQueue (string)` |
| `DeleteReceiveQueue` | `int DeleteReceiveQueue (string)` |
| `SetReceiveQueueFilter` | `int SetReceiveQueueFilter (string, string)` |
| `ReceiveAuthorizationRequest` | `int ReceiveAuthorizationRequest (string)` |
| `SendAuthorizationResponse` | `int SendAuthorizationResponse (string)` |
| `Error` | `void Error (int, int, string)` |
| `ErrorHeaderData` | `void ErrorHeaderData (int, int, string, string, string)` |
| `DeviceTest` | `int DeviceTest (string, string, string)` |
| `GetCategory` | `int GetCategory (short)` |
| `IsUnicodeDevice` | `int IsUnicodeDevice (short)` |
| `SetParam` | `void SetParam (int, string)` |
| `UploadFileFTP` | `int UploadFileFTP (string, string)` |
| `DownloadFileFTP` | `int DownloadFileFTP (string, string)` |
| `ListFilesOnServerFTP` | `int ListFilesOnServerFTP (string, string)` |
| `DeleteFileFTP` | `int DeleteFileFTP (string)` |
| `UploadFileSFTP` | `int UploadFileSFTP (string, string, string, string)` |
| `DownloadFileSFTP` | `int DownloadFileSFTP (string, string, string, string)` |
| `ListFilesOnServerSFTP` | `int ListFilesOnServerSFTP (string, string, string, string)` |
| `DeleteFileSFTP` | `int DeleteFileSFTP (string, string, string)` |

**The listing does not say which parameters are out.** `Get-Member` prints a
positional type list and nothing else, so the direction of every parameter has
to be established by use. What is settled:

```
Open(user, device, nTelegramType, nAccess, bLightLicenceEnable)
Send(szHeader, szData, out szHandle, nTimeout, out lStatus)
SendOne(szHeaderData, out szHandle, nTimeout, out lStatus)
ReceiveOne(out szHeaderData, szHandle, nTimeout, out lStatus)
```

The return value is the call's own success; `lStatus` is the transfer state
(0 ok, 1 timeout, 2 more data waiting). `IsUnicodeDevice(short)` is
undocumented in both directions — the `short` may be in or out, and this code
passes it by reference.

`ReceiveOne`'s `szHandle` has a second meaning the method's own description does
not give it. The manual defines it as the handle `Send` returned; the entry for
`ReceiveOneWithoutAck`, which takes the same parameter list, calls it "the handle
for spontaneous data (DUSTBIN or a queue you created)". Both readings are
correct, and it is the second one that makes unsolicited records reachable at
all: a record the device sent on its own belongs to no send, so it is filed
under a receive queue — `DUSTBIN` when the client made none — and receiving
against that name is how you collect it.

`link::Transport::receiveSpontaneous(queue, timeout)` does exactly that, with
`ReceiveOne` rather than `ReceiveOneWithoutAck` because the former acknowledges
by itself, and the manual is explicit that an unacknowledged record leaves the
device waiting.

The FTP family is undocumented in both directions, and one of its methods has
been measured rather than guessed. `ListFilesOnServerFTP` called as
`(out string, path)` returns **0 for every path**, including `""` and `"/"`,
leaves the out slot empty and hands the second argument back unchanged. A method
that succeeds on an empty path is not reporting on the path, so its return value
cannot distinguish one directory from another — and neither argument is being
written to, which puts the listing somewhere the caller is not looking. The
reading that fits: `DownloadFileFTP(szLocalFilepath, szServerFilepath)` names
the **local** side first, so by analogy the first parameter here is a local file
to write the listing into, an in parameter rather than an out one. Untested at
the time of writing; the panel's preset now sends that shape.

Every method returns `int` except `Error`, `ErrorHeaderData` and `SetParam`.
The FTP and SFTP families, `DeviceTest`, `GetCategory`, `SetParam` and
`IsUnicodeDevice` appear nowhere in the manual.

**Use `Send`, not `SendOne`.** `Send(header, data)` takes the two parts
separately. `SendOne` takes one string and splits it itself, and what it expects
between the parts is undocumented; an interleaved telegram gives it nothing to
split on and it fails with *Telegrammaufbau ist fehlerhaft* from
`CConvDataToBxNetBase::SeperteHeaderData`.

**The exchange is asymmetric.** Requests go out as two parts and answers come
back from `ReceiveOne` interleaved on one line:

```
->  A!GT63    ->  1111        header, then data
<-  A!GT63|1111               interleaved, one line
```

Read the answer as a header-plus-data pair and the parser takes the value for a
token and rejects the whole reply.

**Never wait for a reply to a write.** The server consumes the acknowledgement:
a positive one makes `Send` return zero and leaves the receive queue empty, so
waiting buys the full timeout and then reports a timeout over an operation that
worked. A negative one is not lost — it comes back as a failed `Send` carrying
the device's own words. Confirm a write by reading the value back.

**Fill the data field of a read.** The reference is right that a read is a
header alone, but the BCS parser rejects an empty field for a `D`, `L` or `W`
command (*Die Zeichenfolgen '| |' bzw. '||' sind nicht erlaubt*) and warns that
the compatibility mode letting it through is going away. `readPlaceholderData()`
supplies a `0` per numeric payload token.

**Direction can be the whole bug.** A command carrying a parameter looks like a
write and may still be a read: `MDW_GET_BUFF` takes a timeout in milliseconds
and is polled with `A?MW06|3000`. Sent as a write, the device answers
`LGW_RETURN` 2154.

**An unreachable device does not look like a timeout.** A send to a device that
is switched off fails with *Senden der Anfrage der Unicodeeinstellung des
Gerätes fehlgeschlagen* — asking the device for its Unicode setting is simply
the first thing the server does, so it is the first thing that fails when
nobody answers. `Open` succeeds against an offline device: it only registers the
client with the server.

**Read the server's own log.** It carries an error number, source file and line
for every failure, where the client sees only the last message. The numbers in a
`(GX)` error are the device's `LGW_RETURN`, so the same table decodes both.

---

## What this library deliberately does not do

**Parameter semantics.** The registry carries names and versions only. For a
number of subfunctions — including `GGW_UNIQUE_DATEN` (`GW7D`),
`XCX_DELETE_UNIQUE_DATA` (`XX13`) and `SRW_UNIQUE_PCK_DATA_READY` (`SW9B`) — the
vendor reference lists the name and nothing else: no value range, no
description. Inventing plausible ranges would be worse than leaving the gap
visible.

**A wire transport.** The library opens no sockets and loads no DLLs, and
`link/` goes through the vendor server rather than speaking to the device
directly. The connection layer below the telegrams is undocumented and
non-trivial: the error catalogue references a post-connect info message,
back-synchronisation after sending, bus addressing and a licence check, none of
which are specified. The binary codec here is for reading logs, not for
speaking the protocol.

**Block arity is assumed, not proven.** The annotated example in the manual
states that block commands take no field in the data line, and every worked
example is consistent with that — except one, where the record carries one field
more than the header declares. That is either a typo in the source or a nuance
not captured here. Because of it, a field-count mismatch is reported as a
diagnostic rather than silently absorbed: a shifted record is precisely how the
wrong data reaches a label.

---

## Regenerating the registry

```sh
python3 tools/gen_registry.py docs/markdown/GxNet.md > core/include/gxnet/detail/registry_table.hpp
```

The generator reads the markdown export of the vendor subfunction reference and
emits the lookup table. That export is gitignored, so keep a local copy; re-run
the generator whenever you receive a newer revision, and nothing else needs to
change.

---

## Licence

MIT — see [LICENSE](LICENSE). Every source file carries an
`SPDX-License-Identifier: MIT` line, so the identifier travels with the file
when one is lifted out on its own.

What the licence does **not** cover, because none of it is here: the Bizerba
manuals this was written against, their sample code, and any data belonging to a
particular installation. The notes in `docs/` cite the manuals; they do not
reproduce them.
