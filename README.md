# gxnet

A portable C++20 implementation of the **GxNet** telegram language used by Bizerba Gx-family devices (price-labelling lines, checkweighers, industrial scales).

The library is deliberately **transport-free**. It knows how to build, encode, parse and check telegrams; it never opens a socket, loads a DLL or touches the registry. Feed the strings it produces to whatever channel you already have – the BCS COM interface, `_connectService`, a file drop – and hand it back what comes in.

- C++20, no third-party dependencies
- Reads both the textual form and the binary form found in communication logs
- Builds as a static library; a C ABI header is provided for embedding
- 1922 subfunctions with symbolic names and introducing software versions
- 407 self-checks across three binaries, no external test framework
- Clean under AddressSanitizer, UndefinedBehaviorSanitizer and ThreadSanitizer

---

## Why version checking is the point

Every subfunction in the vendor reference carries the software release that introduced it. A device running an older release does not report an error for a command it has never heard of – it does nothing. That failure mode is expensive on a production line, because the sequence *looks* like it worked.

Query the device once at start-up with `A?ST8D` (`SRT_GX_VERSION`), cache the result, and every telegram built afterwards is checked against it for free:

```
error [version.too_new] SW9B: SRW_UNIQUE_PCK_DATA_READY requires
      software 16.40 but the device runs 14.00
```

The three lines of code that produce it are in [`docs/usage.md`](docs/usage.md), along with the rest of the API.

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
tools/           gen_registry.py, gen_docs.py, a read-only BCS introspection script
docs/            the notes and guides listed below
.github/         CI: Windows first, Linux for the sanitizers
dist/            packaged builds, gitignored
```

Each component has its own `include/` and `src/`. The library is the part with no dependencies; `link/` speaks to a device through the vendor's `_connect.BRAIN` server rather than the wire, and `app/` is a separate target that is off by default.

## Targets

| Target | What it is | Option |
|---|---|---|
| `gxnet` | the library: static, no dependencies | always built |
| `gxnet_link` | transport layer; the mock everywhere, BCS on Windows | `GXNET_BUILD_LINK`, on |
| `gxlint` | command-line annotator and validator for captures | `GXNET_BUILD_EXAMPLES`, on |
| `gxnet_tests` | the library's own checks | `GXNET_BUILD_TESTS`, on |
| `gxnet_link_tests` | transport, against the mock | `GXNET_BUILD_TESTS` |
| `gxdemo_tests` | gxdemo's model, no window server needed | `GXNET_BUILD_TESTS` |
| `gxdemo` | wxWidgets bench, fetches wx through CPM | `GXNET_BUILD_APP`, **off** |

`GXNET_SANITIZE` is passed straight to `-fsanitize`.

---

## Building

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
```

All three test binaries are run by `ctest`. `gxdemo_tests` links one source file out of `app/` and needs no window server, because `Session` deliberately includes no wxWidgets header.

That target ends with a soak: connect, work, disconnect, repeatedly. It asserts on the containers rather than on resident size, because an allocator claims arenas and keeps them, so RSS rises and then plateaus even when nothing leaks. What must stay flat is what the program owns – the log against its ceiling, the listeners against their subscriptions, the pending queue against zero. `GXNET_SOAK_CYCLES=5000` turns it into a real soak; the default keeps the whole suite under a second.

### Sanitizers

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DGXNET_SANITIZE="address;undefined"
cmake --build build-asan -j && ctest --test-dir build-asan
```

`thread` works too and has to be built on its own. Clang and GCC only.

**LeakSanitizer exists on Linux and nowhere else** – not on macOS, and not on Windows in any toolchain, MSVC and clang-cl included. So leak checking is a Linux job, which is what CI uses it for; on Windows, Dr. Memory or UMDH fill the gap, and a COM reference leak is invisible to all of them anyway because it is not a heap leak. AddressSanitizer itself does cross-compile with llvm-mingw and runs on Windows.

### Without CMake

```sh
g++ -std=c++20 -Icore/include -Icore/src core/src/*.cpp tests/test_gxnet.cpp -o gxnet_tests
```

### The application, and cross-compiling it

`-DGXNET_BUILD_APP=ON` fetches wxWidgets through CPM, which is why it is off by default: the library keeps building in seconds with no network. From a Mac for Windows:

```sh
cmake -S . -B build-win -DGXNET_BUILD_APP=ON -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=app/cmake/llvm-mingw-x86_64.toolchain.cmake
```

llvm-mingw links libc++ and compiler-rt statically, so the result is one self-contained executable.

---

## Documentation

**The vendor manuals are not here.** They are Bizerba's, and this repository is public. What stands in for them, and what documents this project itself:

| File | What it covers |
|---|---|
| [`docs/usage.md`](docs/usage.md) | the API in worked examples, and the C ABI |
| [`docs/telegram-format.md`](docs/telegram-format.md) | token layout, blocks, escaping, the binary form, acknowledgements |
| [`docs/gxlint.md`](docs/gxlint.md) | the command-line annotator and the sample capture |
| [`docs/registry.md`](docs/registry.md) | regenerating the subfunction table from the reference |
| [`docs/gxnet-notes.md`](docs/gxnet-notes.md) | the telegram language: codes, encodings, the rules stated once and far from where they are needed |
| [`docs/bcs-notes.md`](docs/bcs-notes.md) | `_connect.BRAIN`, the COM/DCOM server that carries the telegrams, and its automation interface |
| [`docs/machine-notes.md`](docs/machine-notes.md) | how a GLM installation is wired: channels, outgoing lines, the memory card, unique data |

Every claim in the notes cites the manual by symbolic name, subfunction code and, where a name is not enough, a chapter and a short German quote – so the originals stay searchable without being redistributed.

---

## What this library deliberately does not do

**Parameter semantics.** The registry carries names and versions only. For a number of subfunctions – including `GGW_UNIQUE_DATEN` (`GW7D`), `XCX_DELETE_UNIQUE_DATA` (`XX13`) and `SRW_UNIQUE_PCK_DATA_READY` (`SW9B`) – the vendor reference lists the name and nothing else: no value range, no description. Inventing plausible ranges would be worse than leaving the gap visible. What the manual's own prose adds is an [optional local table](docs/registry.md), generated and not shipped.

**A wire transport.** The library opens no sockets and loads no DLLs, and `link/` goes through the vendor server rather than speaking to the device directly. The connection layer below the telegrams is undocumented and non-trivial: a post-connect info message, back-synchronisation after sending, bus addressing and a licence check, none of which are specified. The binary codec here is for reading logs, not for speaking the protocol.

**Guess at a shape it cannot check.** Block arity, the `SendOne` separator, the FTP parameter directions: where the answer is not established, the code says so at the declaration and the reasoning is in the notes, rather than a plausible invention that fails on a line.

---

## Licence

MIT – see [LICENSE](LICENSE). Every source file carries an `SPDX-License-Identifier: MIT` line, so the identifier travels with the file when one is lifted out on its own.

What the licence does **not** cover, because none of it is here: the Bizerba manuals this was written against, their sample code, and any data belonging to a particular installation. The notes in `docs/` cite the manuals; they do not reproduce them.
