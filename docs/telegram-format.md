# The telegram format

What a GxNet telegram is made of, textual form and binary form. Details that cost time to establish, recorded so the next reader does not have to rediscover them.

Companion files: [`gxnet-notes.md`](gxnet-notes.md) for the language itself – codes, value encodings and the rules the reference states once, far from where they are needed – and [`usage.md`](usage.md) for the API that builds and parses these strings.

---

## Token layout

Four characters: group letter, type letter, two hexadecimal index digits. The command class code is `(group << 4) | type` – `GW7D` is `0x01`, `XX13` is `0xA0`, `PV04` is `0x36`.

| Type | Letter | Payload fields |
|---|---|---|
| Command | `X` | 0 |
| Word | `W` | 1, 16-bit signed |
| Long | `L` | 1, 32-bit signed |
| Dimension | `D` | 1, `unit;exponent;mantissa` |
| Block | `V` | 0, may be closed by `LX02` |
| Text | `T` | 1, escaped |

## Blocks need not be closed

The reference shows both forms: `PV04|...|LX02` and `PV04|...` running to the end of the header. Which form was used is kept in `Node::explicit_close` so round-trips stay exact.

## Group letters are open-ended

The bundled table covers the Gx family. The Ix family uses further letters – `RX01` and `RX04` appear in registration telegrams – so the parser accepts any upper case group letter and the validator reports unknown ones as a *warning*, never an error. Data type letters are checked strictly, because payload arity depends on them.

## Block arity is assumed, not proven

The annotated example in the manual states that block commands take no field in the data line, and every worked example is consistent with that – except one, where the record carries one field more than the header declares. That is either a typo in the source or a nuance not captured here.

Because of it, a field-count mismatch is reported as a diagnostic rather than silently absorbed: a shifted record is precisely how the wrong data reaches a label.

## Read requests carry no data line

`A?GW7D` is the whole telegram. A payload token with no value is legal for `Access::Read` and an error for `Access::Write`.

The server between the program and the device disagrees, and wants a placeholder field per numeric payload token; that is a BCS rule rather than a GxNet one, and it is recorded in [`bcs-notes.md`](bcs-notes.md).

## Escaping

Control characters, `@` and `|` become `@` plus two upper case hex digits: `@0A`, `@40`, `@7C`. `EscapeOptions` can additionally escape `;` and bytes above `0x7F`.

## Legacy format

The bare `!` / `?` prefix (`Family::Legacy`) switches the dimension sub-separator from `;` to `|`.

## Binary form

`[frame header: 4][class][index][payload]...`, the class code the same as in the textual form. Not vendor-documented; reverse engineered from captures and confirmed by byte-exact round-trips.

- Block length fields count everything after the length, **including** the closing `LX02`.
- Text payloads are padded to an even byte count and **the pad is not counted** in the length. Missing this desynchronises a parser several fields later, which is exactly how the first implementation failed.
- The dimension unit field packs a unit code in the upper ten bits and a six-bit two's-complement exponent in the lower six. `0x00FD` is unit 3, exponent -3. The unit codes themselves are inferred, not documented; unmapped ones render as `#N` and round-trip.
- In the frame header, the first byte is the direction: **`0x90` is `?`, `0xD0` is `!`**. Every reply begins `0xD0`, and correctly so – a reply is a write telegram, the device writing the value back. Bytes 3 and 4 are the bus addresses, source then destination. The second byte varies with the payload type of the leading token and is not fully explained.

This form travels between the server and the device, where a client never sees it. `gxnet::binary` exists to read logs of it – not to speak it.

## Acknowledgements

A write is answered by `LGW_QUIT_OK` carrying the class code of the command that succeeded, or by an `LGV_QUIT` block carrying `LGW_RETURN` (the reason), `LGW_UFKENN` (the class code that failed) and `LGW_DEBUG` (an internal error number).

`LGW_RETURN` 4 is "third-party command", which is what a device answers for a subfunction its software release does not have. `LGW_DEBUG` is decodable by a published rule even where the appendix skips the code – see [`gxnet-notes.md`](gxnet-notes.md), *Internal error codes are built from a published rule*.

Whether an acknowledgement reaches the client at all is a property of the transport, not of the language: `_connect.BRAIN` consumes the positive one. See [`bcs-notes.md`](bcs-notes.md).

## Encoding

Everything is byte-oriented. `std::string` payloads are treated as opaque bytes, so UTF-8 passes through untouched; the default escape set leaves bytes above `0x7F` alone. Convert to and from wide strings only at the host boundary, never inside.
