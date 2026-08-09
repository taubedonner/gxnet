# The machine: how a GLM installation is actually wired

Not the command set – the configuration around it. Channels and outgoing lines, what reaches the ERP and by which path, the memory card, unique data, and the per-package flags that say what went wrong.

Nothing installation-specific lives here: no addresses, no licences, no captured codes. Those belong in the private notes, not in a public repository.

**Originals these notes were taken from:** `GxNet-de.md` for the subfunctions, `BRAIN2.md` and the GLM product manual for the machine side.

## How to read these notes

The vendor manuals are **not in this repository** – they are Bizerba's and stay out of it. Every claim here therefore carries something to search the originals with, and that is the point of the file rather than a decoration:

- the **symbolic name** as the manual spells it (`GGW_SENDKANAL_A_ENABLE`), which is unique and greppable;
- the **subfunction code** (`GWBF`) and the release it appeared in;
- where the passage is not findable by name, a **chapter or page** and a short **German quote**, because the tables repeat their headings and a page number alone drifts between editions.

**Use the German edition.** `GxNet-de.md` is a later revision than `GxNet.md`: the English `LGW_RETURN` table stops at 2658 and has no codes 14–24, and the `GGT_SIMPLE_TXT` limits are a version behind. Confirmed by converting the English PDF independently, so it is an edition difference and not a conversion loss.

Where a claim rests on a deduction rather than a reading, the deduction is shown. Where it was measured on a device, it says so.

Companion files:

- [`gxnet-notes.md`](gxnet-notes.md) – the telegram language itself.
- [`bcs-notes.md`](bcs-notes.md) – `_connect.BRAIN`, the COM/DCOM server that carries the telegrams.
- [`machine-notes.md`](machine-notes.md) – how a GLM-family installation is wired: channels, outgoing lines, the memory card, unique data.

---

## 1. Where the device may send, and what it sends there

**GWBF** `GGW_SENDKANAL_A_ENABLE`, and then **GWC0**–**GWC3** for channels B–E; **GW74**/**GW75** (from 13.40) for J and K. The wording is "Bereitschaft des GX über Kanal *x* initiativ zu senden" – readiness of the device to send on its own initiative over that channel – with `0` spelled out as "packungssynchrone Auszeichnungsdaten werden **nicht** versendet".

This is a per-channel gate on everything unsolicited. If it is `0` for the channel a program is connected on, no listener on the client side can help, and nothing about the failure says so: the send is acknowledged, the dialog appears, and the answer is never sent.

Read it before writing a receiver. But read *whose* channel it is first, because that is where this went wrong here.

#### A channel belongs to a device, not to your connection

Measured on one device: the switch reads **1 for every channel it exists on** – A, B, C, D, E – and no dialog answer arrives. So enabling is not the constraint, and neither is the absence of a channel. What the switch does say is *whose* channel each one is. On a multi-labeller line one channel is typically the system bus to a slave labeller, enabled because the GX has to push package-synchronous data to it, and another carries the memory card. The rest can be enabled and unused, which is what makes reading the switch alone misleading.

Two consequences worth stating plainly:

- **The `A->` prefix in the server log is `_connect.BRAIN`'s label for the connection, not the device's channel A.** Reading `GGW_SENDKANAL_A_ENABLE` because the log says `A` is a mistake, and it is an easy one.
- **Writing that switch to 0 stops package data to whatever really is on channel A.** On a multi-labeller line that is a second printer going quiet.

Three reads that fill in the picture, all safe: `GGV_CHANNEL_MULTI_LABELER` (GV0F) is the channel-to-address table and can answer empty even on a working multi-labeller line; `GGL_SBUS_ADR` (GL38) is the device's own system-bus address, `0` meaning the bus is not enabled here; `GGW_SBUS_KONFIG` (GW38) says what the bus runs over – 0 Profibus, 1 Ethernet, 2 both.

What actually maps channels to devices: `GGV_CHANNEL` (**GV0E**, 14.0), "Zuordnung eines Kanals zu einer Systembusadresse", carrying `GGL_SBUS_ADR`; and `GGV_CHANNEL_MULTI_LABELER` (**GV0F**) for the whole table at once. `GGL_SBUS_ADR` (GL38) is the device's own system-bus address, `0` meaning system bus not enabled; `GGW_SBUS_KONFIG` (GW38) says what the system bus runs over – 0 Profibus, 1 Ethernet, 2 both.

Note also which channels the switch even exists for: **A, B, C, D, E** (GWBF, GWC0–GWC3) and **J, K** (GW74, GW75, from 13.40). There is no `GGW_SENDKANAL_F_ENABLE`, which fits – the memory-card channel is configured through the send editor rather than through this switch.

#### And what goes out on a channel is a third, separate list

`SRV_NET_KONF_CONTENT_SEND_BY_CHANGE` (**SV5A**), `..._ADDON_PSV_PCK` (**SV5B**) and `..._CONTENT_PSV_DATA` (**SV5C**) read it. Each answers with `SRV_UFKENN_CHANNEL_INFO` blocks, and each block is a pair: `LGW_UFKENN` (**LW02**) and `SRL_NET_CHANNEL_BITMAP` (**SL8C**) – bit 0 internal, bit 1 = A ... bit 11 = K.

`LGW_UFKENN` is the token's 16-bit code: class in the high byte, index in the low, with class = `(group << 4) | type`. So `GT63` is `0x0763` = **1891** and `WZV_SDD_RESULT` is `0x9663` = **38499**. Worth writing out because the obvious misreading – taking `G` and `T` as the two nibbles in the order they appear in the name – gives `0x7763`, which is a different token entirely and looks plausible enough to search for. Group `G` is **0**, not 7; the captured frame settles it by carrying `WZV_SDD_START` as `9660` (W = 9, V = 6).

**What the lists actually offer is the finding.** Measured on one GLM-family device, `SV5B` had 148 entries and `SV5C` 140, and between them every entry was from group **A**, **G** or **P** – automatic labeller, basic unit, package synchronous. **Group `W` does not appear at all.** So a dialog answer or a softkey press cannot be routed through this mechanism: there is nothing to configure. "Bei Änderung versenden" is for values that change, not for events.

**`SV5E` is that nested block, not a command of its own.** It has a subfunction number and a row in the table like everything else, which makes it look readable; asking for it directly gets `fremdes Kommando` back. The readable ones are `SV5A`, `SV5B`, `SV5C`, `SV5D`, `SV5F` – each of which contains `SV5E` blocks. Worth remembering as a shape: a row in the coding table is not a promise that the device will answer a `?` for it.

Its writing counterparts are `SV6A`–`SV6F`, and those have error codes of their own – 30149–30153, "falscher Kanal bzw. falsche Kennung" – which lock labelling. Read freely, write only deliberately.

#### The memory card is not a way round this

It looks like one. `MDV_SEQUENZ` (MV07) is documented as carrying `PSV_PCK`, `PSV_DATA`, or **"beliebiges Kommando, das über den Sendeeditor 'Bei Änderung versenden' einstellbar ist" from the groups `<GG~, AM~, XC~, WZ~>`** – and `WZ~` is the input-tools group, where a dialog result lives. So on paper an answer could be routed into the memory-card buffer and read from there.

In practice it is a trap on any line where something else already reads that buffer. Reading it **deletes what it read** ("implicit deletion of this transferred data"), so two readers cannot share it, and adding unrelated records to the stream is likely to break whatever parses it.

## 2. The outgoing-lines configuration is readable and writable over the wire

The `<Add.data to PSV_PCK>` setting – the one that decides whether a value written on the master reaches a secondary printer – does not need a trip to the terminal and an authorisation level. It is a telegram, from 14.00:

| Read | Write | What |
|---|---|---|
| `SV5A` | `SV6A` | "send on change" |
| **`SV5B`** | **`SV6B`** | **"Add.data to PSV_PCK"** |
| `SV5C` | `SV6C` | "content of PSV_DATA" |
| `SV5D` | `SV6D` | "add.data to statistics" |
| `SV5F` | `SV6F` | contents of statistics reports 1–10 |

The read answers with a list of `SRV_UFKENN_CHANNEL_INFO` (SV5E) pairs: a subfunction code (`LGW_UFKENN`, LW02) and a channel bitmap (`SRL_NET_CHANNEL_BITMAP`, SL8C).

Channel bitmap: bit 0 internal, bit 1 = A, bit 2 = B, ... bit 11 = K.

> **The write replaces the list, it does not add to it** – and getting the
> channel or the identifier wrong does not merely fail. There are dedicated
> *locking* errors for it: **30150** `PAE_LOCK_NET_KONF_ADDON_PSV_PCK`, and
> 30149 / 30151 / 30152 / 30153 for the neighbouring commands. A locked device
> is a stopped line. Read first, add one entry, write the whole list back, and
> not on a running line.

Related settings that are ordinary reads:

- `GGW_SENDKANAL_A_ENABLE` ... `_K_ENABLE` (GWBF–GWC3, GW74, GW75): whether the GX is willing to send on a channel at all.
- `WZL_WDOUTFLG_A` ... `_K` (WL25–WL29, WL99, WL9A, WLA9–WLAC): which label kinds (single, totals) go out on each channel as `PSV_PCK`.
- `GGW_PACKUNGEN_PSV_DATA` (GWFC): 0 = only `PSV_PCK` for valid packages, 1 = `PSV_DATA` for valid and **`PSV_FEHLDATA` for invalid** ones. If the ERP only parses `PSV_DATA`, defective packages are arriving somewhere it is not looking.
- `GGW_TEL_QUIT_MODE` (GW58, 11.00): 0 = single acknowledgement, 1 = double. On "double" one command produces two acknowledgements, which a receive loop that stops after the first will leave in the queue for the next request to trip over.

## 3. This device never starts a conversation

Worth stating on its own, because every hypothesis in section 1 above was a way of avoiding it, and the evidence is simple enough to check on any installation.

Take the server's binary communication log – `CommU_<device>_<date>.commlog` – and count the inbound frames that are *not* preceded by an outbound one. On a GLM-family device, over three hours and roughly 5000 frames, that count was **three**, and all three belonged to the licence handshake at connect time. Every other frame the device sent was an answer to something asked microseconds earlier.

That includes the traffic that most looks like a push. Package data reaches the ERP through the memory card, and the vendor's own product **polls** for it: `MDW_GET_BUFF` (MW06) out, `MDV_SEQUENZ_END` (MV08) back, every five seconds. Nothing is delivered; everything is collected.

So `WZV_SDD_RESULT` not arriving is not a fault, a permission or a misconfiguration. Nine dialogs were opened across that log, each acknowledged with `LGW_QUIT_OK` within 3 ms, and the code `9663` does not occur anywhere in the file. Neither does `9605`, a softkey press.

**Do not read more into this than it says.** The send-editor lists of section 1 offer only groups A, G and P, which proves that *that* mechanism cannot carry a dialog answer – it does not prove the device has no other route. "Bei Änderung versenden" is about data accompanying package and statistics records; an answer to a command plausibly goes back to whoever sent the command, by machinery the lists never mention. What is measured is that nothing arrives here, and that this device initiates nothing on this link. Why is still open.

**The design consequence.** Anything built on this link should ask rather than wait – and the two things worth asking for have not been tried:

- `A?WV63|WW60|<handle>|LX02` – the result by its handle. The reference marks `WV63` with neither `?` nor `!`, so the direction is simply unspecified, and a `fremdes Kommando` would close the question in one telegram.
- `A?WW68` (`WZW_EXIT`, 0 = входа OK / 1 = cancelled with HOME), `A?WW64` (`WZW_SDD_ID`), `A?WW60` (`WZW_HDL`) – ordinary word subfunctions with printed value ranges. Reads are the one thing this link does reliably.

A spontaneous listener is still worth having – it costs one poll per idle frame and it is the only way to notice if this ever changes – but it should not be the plan.

## 4. Per-package error flags: `PSL_PCK_ERR_FLAGS` (PL13)

A 32-bit field in every package record, so it arrives with the data the ERP is already collecting – no extra polling, and in particular no need to go anywhere near the memory-card buffer.

| Bit | Meaning |
|---|---|
| 0 | internal error |
| 1 | label missing or faulty (printer or applicator) |
| 2 | package could not be weighed |
| 3 | weight too heavy or too light |
| 4 | package contains metal |
| 5 | a slave labeller acknowledged negatively |
| 6 | package data could not be sent |
| 7 | a user-defined formula returned an error |
| 8 | data changed at an impermissible moment |
| 9 | switched to transport mode |
| 10 | ejection triggered by the user |
| 11 | package too long or too short |
| 12 | separation error |
| 13 | **code read-back returned an error** |
| 14 | statistics report running |
| 15 | faulty but not cancelled, because the total was already sent |
| 16 | marked faulty by an external signal on the I/O unit |
| 17 | scanner could not supply data fast enough |
| 18 | RFID write error |
| 19 | TTI error |
| 20 | logo missing or faulty |
| 21 | could not be weighed during teach |
| 23 | empty package |
| **24** | **no unique data available** |
| 25 | test package in scale-check mode |

Bit 24 is the "the marking codes have run out" signal, and it is per package. Bit 13 is the verification that the printed code scanned back correctly.

Cumulative counters for the same causes exist as ordinary reads: `SRL_PCKE_*` (SL40–SL5D, 14.20) – among them `SRL_PCKE_CONSISTENCY_COUNT` (SL48), which counts packages spoiled by *an unfinished sequence or a PLU change*. That is a direct measurement of how badly changeovers go.

## 5. Device status without touching anything

`PSL_RECEIVE_GXSTATUS` (PL03) is a plain read and answers with a number from a long list. The ones worth knowing:

| Value | Meaning |
|---|---|
| 0 | ok / XON |
| 1 | XOFF, not ready to receive |
| 2 | label roll empty |
| 5 | no label on the carrier |
| 7 | memory card full |
| 8 | memory card not writable |
| 14 | label supply running out |
| 16 | data volume too large for the storage medium |
| 105 | no print image / print job |
| 107 | layout missing |
| 111 | signature in `PSV_PCK` missing or wrong |
| 114 / 115 | printer 1 / 2 not reachable |
| 120 | code build rule missing |
| 122 | code data wrong |
| 125 | code data too long |
| 128 | maximum code string length already reached |
| 129 | data error, e.g. code substring missing |
| 130 | error in the code structure |
| 131 | variable longer than the specified number of digits |
| 132 | code build rule empty |
| 133 | impermissible code content |
| 137 | invalid number of digits |
| 138 | faulty AI length |
| 141 | missing data in `PSV_PCK` |

Other cheap reads that change nothing:

- `SRW_LABELER_STATE` (SW98, 14.00): 1 active, 2 passive, 3 standby, 4 error.
- `SRL_LABEL_ROLL_DIAMETER` (SL85): current roll diameter in mm – a label shortage predicted rather than reported.
- `SRL_AM_OPERATING_TIME` (SL82): seconds in START.
- `SRV_LAST_PCK_INFO` (SV70): a full record of the last labelling process – article, weight, tare, belt speed, package length, print and application times. Does **not** touch the unique buffer.
- `SRV_MULTIPLE_DEVICE_ADDR` (SV25): the addresses of the devices making up a multiple labeller.
- `SRW_UNICODE_DEVICE` (SW85, 13.00): 0 codepage device, 1 unicode device. This is what the BCS `IsUnicodeDevice` call wraps.
- `LGV_XL_DATA` / `LGV_XL_INFO` (LV11 / LV10, 13.60) with `WZW_HDL` = **536** reads the device's message history in pages. Note the handle here is a fixed constant meaning *what to read*, not a session identifier – worth knowing before assuming `WZW_HDL` is always a caller's choice.

## 6. Unique data: the parts that bite

`GGW_UNIQUE_DATEN` (GW7D, 15.20) **is** documented, contrary to what the English edition suggests: range 0/1, 0 = without, 1 = with.

`XCX_DELETE_UNIQUE_DATA` (XX13, 15.20) carries the ordering rule in its own description: "delete all unique data, **only possible when unique data is deactivated** (`GGW_UNIQUE_DATEN` = 0)".

`SRW_UNIQUE_PCK_DATA_READY` (SW9B, 16.40) is settled as a **flag**, not a count: 0 = unique data not ready, 1 = ready. As a low-stock warning it is useless; the "ran out" event is `PSL_PCK_ERR_FLAGS` bit 24.

### Errors specific to the feature

| Code | Symbol | Meaning |
|---|---|---|
| 22551 | `PAE_UNIQUEDATA_NO_VALID_PUBLIC_FILE` | no valid file in `/public/uniquePckData/` |
| 22552 | `PAE_UNIQUE_BUF_EMTPY` | buffer empty on read → switch buffer; **error only when all are empty** |
| 22553 | `PAE_UNIQUE_EMTPY_LINE` | blank line read in the file |
| 22554 | `PAE_UNIQUE_LINE_LENGTH` | **maximum line length exceeded** |
| 22555 | `PAE_UNIQUEDATA_DELETE_FAILED` | deletion failed |
| 19768 | `PAE_ABLX_UNIQUE_DATA` | error while setting the unique data |
| **30183** | `PAE_LOCK_UNIQUE_PCK_DATA` | **"no unique data available" – locks labelling** |
| **30185** | `PAE_LOCK_WRONG_UNIQUE_PCK_DATA` | **"wrong structure of the unique data file. The file has been deleted!"** |

Three consequences worth stating plainly:

1. **A malformed file is deleted by the device and the line locks.** So "the file disappeared" does not mean "the file was accepted" – it means one of two opposite things, and only the log tells you which.
2. **There is a maximum line length.** The number is not printed; it is a real limit and a generator has to respect it.
3. **There are several buffers**, and the "empty" condition is only an error once all of them are. Remaining-code counting is not a single number.

Also: `PAE_LOCK_MEM_QUOTA` (30204) – space in the `public` directory running low or full. And `PAE_LOCK_BANDETECTED` (30211): repeated bad credentials **temporarily ban a service (SFTP...)**, so an upload client must not retry a failed login blindly.

## 7. Where a barcode's content actually comes from

Nothing sends a finished barcode to the device. The label carries a *code build rule* (`Codeaufbauvorschrift`, `GGT_CAB` / GT20, selected per code field by `GGL_CABNR1..7`), and an interpreter on the device assembles the string from that rule plus the current package data. So "which field does the marking code travel in" is a question about the rule, not about the protocol.

The pieces the rule can insert, for the parts that matter here:

| Identifier | Value | What it inserts |
|---|---|---|
| `CODE_PARAM` | 0001 | code family, ratio, module width, height, flags |
| `CODE_KONST` | 0002 | a constant, max 30 characters (`}` must be doubled) |
| `CODE_TEILSTR1..7` | 0005, 0034–0037, 0230, 0231 | code substrings `GGT_CTS1..7` |
| `CODE_TEXT1..20` | 0118–0137 | the full text fields |
| **`CODE_ETX1..30`** | 0192–0201, 0364–0383 | **the simple texts `GGT_SIMPLE_TXT1..30`** – ETX11–30 from 15.60 |
| `CODE_ANUM1..20` | 0105–0107, 0202, 0203, 0232–0246 | the free numbers `GGL_ANUM1..20` |
| `CODE_AI01`, `CODE_AI21`, `CODE_AI93`... | various | application identifiers |
| **`CODE_GS`** | 0308 | **a GS separator character** |
| `CODE_FNC1` | 0047 | the Code128 FNC1 |
| `PA_CODE_ENCODING` | 0351 | character encoding: `0000` ASCII, `0001` ISO8859-1, **`0002` UTF-8**, `0003` CP936. Goes second, right after `CODE_PARAM`. |

Formats sit in braces after the identifier. The one that matters for text: `CODE_FORM_STELLENANZ` (class J), a four-digit count where **`0000` means the whole text**, and any other value takes that many characters from the start and pads short texts with spaces.

Code flag bit 15, for `CODE_GS1DATAMATRIX`: 0 = FNC1 as separator, 1 = GS.

Which means a GS1 element string is not one field. `(01)` GTIN + `(21)` serial + GS + `(93)` tail is assembled from an AI, a constant or substring, a simple text, `CODE_GS`, another AI and another simple text. **To find out what a given label expects, read `GGL_CABNR1` (GL4A) for the rule number, then `GGT_CAB` (GT20) for the rule itself.** That answers it without asking anyone.

Relevant failures: `PAE_ETI_CODE_DAT_LEN` (15781) "code data too long for the code family", `PAE_ETI_CODE_OVF` (15769) "code field too small", `PAE_ETI_CODE_DRU_RESOLUTION` (15770) "printer resolution unsuitable for the code family".

## 8. Package-synchronous odds and ends

- `PSV_CTRL` (PV02) still exists but is marked **"do not use for new developments"**, and so is its handle `PSW_CTRLHDL` (PW0A). Use `LGV_SEQUENZ`.
- `PSV_DATA` (PV04) and `PSV_STSTK_INFO` (PV10) both carry an explicit **"this command must not be logically acknowledged"**. Answering them is an error, and there is a code for noticing it: `PAE_NET_WD_QUIT_TOO_MUCH` (17285), "more acknowledgements for `PSV_PCK` received than were sent".
- `PSW_PCK_VAL_REQ` (PW12, 6.60) is the device *asking* a real-time host to supply data for one specific package, sent when the package's leading edge passes trigger 3. The host answers with `PSV_PCK_VAL` (PV08) carrying the same handle. This is the mechanism for per-package data without a file at all – and it needs the spontaneous channel and hard real time.
- `PSW_CHANNEL_DISABLE` (PW13), inside `PSV_PCK_VAL`, suppresses printing per channel for a single package.
- `AMW_TRIG2_MSG_AKTIV` (AW75): 0 off, 1 released by a PLU change **or** a command, **2 (from 8.62) released only by `XCW_PCK_SYNC`**. Value 2 is the one that stops a PLU change from implicitly releasing a held package.
- Footnote 47: on fatal errors ("send failed") the GX **puts itself into the locked state**. A controlling program that drops its connection at the wrong moment stops the line.
