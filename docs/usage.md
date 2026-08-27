# Using the library

The API in worked examples: building a telegram, reading a value back, parsing what the device sent, looking a subfunction up, decoding a captured log, and the C boundary.

For what the strings mean once they are built, see [`telegram-format.md`](telegram-format.md); for getting them to a device, [`bcs-notes.md`](bcs-notes.md).

---

## Building a telegram

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

The builder is type-checked: `word("GL19", 5)` fails at build time because `GL19` is a Long, not a Word.

## Reading a value back

```cpp
Telegram probe = Builder(Family::Automatic, Access::Read)
                     .query("SW9B")     // no value: the device supplies it
                     .build();
*encodeOneLine(probe);                  // "A?SW9B"
```

## Parsing what the device sent

```cpp
auto header = parseHeader("A!PV04|PW02|GW09|GL19|GL1A|GL16|PD00|GL2B|GL2C");
auto record = parseRecord(*header, "1|2|4711|0|1|KG;-3;100|20997|1545");

const Dimension& weight = std::get<Dimension>((*record)[5]);
weight.mantissa;   // 100
weight.exponent;   // -3      -> 0.100 KG
```

Both transmission forms are supported: separate header and data lines (the two-argument `Send`), and the interleaved single line (`SendOne`), via `parseLines` and `parseOneLine`. Encoding is byte-exact, so a captured telegram re-encodes to itself.

## Looking a subfunction up

```cpp
tokenName(*Token::parse("GW7D"));   // "GGW_UNIQUE_DATEN"
tokenByName("GGL_PLUNR");           // GL19

auto info = Registry::builtin().find(*Token::parse("SW9B"));
info->since;      // 16.40
info->reserved;   // true when the reference lists no description
```

## Checking against a device's software release

Every subfunction in the vendor reference carries the software release that introduced it, and a device running an older release does not report an error for a command it has never heard of – it does nothing.

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

Query the device once at start-up with `A?ST8D` (`SRT_GX_VERSION`, format `MM.mm.bbbb`), cache the result, and every telegram built afterwards is checked against it for free.

## Reading a communication log

Device logs record the compact binary form, not the textual one. `binary::parse` decodes it into the same node tree, so validation and re-encoding work unchanged.

```cpp
gxnet::binary::Options opts;
opts.units = gxnet::binary::inferredUnitTable();

auto frame = gxnet::binary::parseHex("90770102D1060BB8", opts);
frame->nodes[0].token.str();   // "MW06"  (MDW_GET_BUFF)
```

Round-trips are byte-exact: a captured frame re-encodes to itself. The byte layout is described in [`telegram-format.md`](telegram-format.md), *Binary form*; [`gxlint.md`](gxlint.md) is the command-line way to the same decoding.

## The C ABI

`core/include/gxnet/gxnet_c.h` exposes lookup, validation and form conversion across a plain C boundary – for loading as a DLL from a service written in another language, or from an ERP runtime.

```c
gxnet_token_info_t info;
gxnet_lookup_token("GW7D", &info);       /* info.name, info.since, info.arity */

char diags[4096];
int errors = gxnet_validate("A!GW7D", "0", "16.40", diags, sizeof diags);
```
