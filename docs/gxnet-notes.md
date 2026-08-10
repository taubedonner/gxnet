# GxNet: what the reference does not make searchable

The telegram language of Bizerba Gx-family devices – subfunctions, codes, value encodings and the rules the manual states once, far from where they are needed.

**Originals these notes were taken from:** `GxNet-de.md` (Bizerba GxNet-Schnittstellenbeschreibung, German edition) and `IxNet-de.md` for the label-resource group.

## How to read these notes

The vendor manuals are **not in this repository** – they are Bizerba's and stay out of it. Every claim here therefore carries something to search the originals with, and that is the point of the file rather than a decoration:

- the **symbolic name** as the manual spells it (`GGW_SENDKANAL_A_ENABLE`), which is unique and greppable;
- the **subfunction code** (`GWBF`) and the release it appeared in;
- where the passage is not findable by name, a **chapter or page** and a short **German quote**, because the tables repeat their headings and a page number alone drifts between editions.

**Use the German edition.** Not because it is newer – both editions carry the same date, `28.05.2026` – but because the English translation is incomplete. `GxNet_de_de.pdf` runs to 661 pages against the English 527, and the gap is content: the English `LGW_RETURN` table jumps from 13 straight to 150, where the German has codes 14–24, and the `GGT_SIMPLE_TXT` limits are a version behind.

Checked by extracting both PDFs directly rather than through the markdown, so this is a translation gap and not a conversion artefact. The distinction matters because the markdown *did* mislead once: `GxNet.md` lost every table in conversion, which made the English edition look far emptier than it is.

Where a claim rests on a deduction rather than a reading, the deduction is shown. Where it was measured on a device, it says so.

Companion files:

- [`gxnet-notes.md`](gxnet-notes.md) – the telegram language itself.
- [`bcs-notes.md`](bcs-notes.md) – `_connect.BRAIN`, the COM/DCOM server that carries the telegrams.
- [`machine-notes.md`](machine-notes.md) – how a GLM-family installation is wired: channels, outgoing lines, the memory card, unique data.

---

## 1. Internal error codes are built from a published rule

`LGW_DEBUG` carries an internal Bizerba error number, and the appendix that lists them is incomplete on purpose – it prints the entries somebody wrote down. The input-tool group runs 17701, 17702, and then jumps straight to 17715.

The rule that fills the gaps is printed at the very end of the document, in **§4 "Interne Codierung"**, roughly 200 pages after the table it explains. Every module has a base, and offsets from that base always mean the same thing:

The scheme fixes the *symbol*. It does not quite fix the text: a module may give its own slot its own wording, and one demonstrably does. Of the 41 `_FKT_E` entries printed in the appendix, 38 read "function not available / not present / not implemented", and three do not:

    2405  TMR_FKT_E        no timer action function assigned
    4005  BOS_EVENT_FKT_E  function error
    5305  DIA_FKT_E        all scales processed        <- not about a function at all

So a decode is a strong inference, not a reading. Where it matters, ask the device: `WZV_META_ERROR_TEXT` (WV4A, 9.00) returns the text out of the firmware's own error resource.

| Offset | Meaning |
|---|---|
| +1 | overflow |
| +2 | underflow |
| +3 | data error (not in the §4 table, but every module that prints a +3 prints it as this) |
| +4 | initialisation error |
| **+5** | **function not available** (38 of 41; see above) |
| +6 | fatal manager error |
| +7 | invalid manager id |
| +8 | invalid semaphore handle |
| +9 | invalid event handle |
| +10 | invalid handle |
| +11 | invalid queue id |
| +12 | invalid resource id |
| +13 | invalid task id |
| +14 | invalid partition id |
| +15 | memory manager error |
| +16 | component bus error |
| +17 | system bus error |
| +18 | invalid message |
| +19 | invalid parameter |
| +20 | driver error |
| +21 | task error |
| +22 | timeout |
| +47 | i/o busy |
| +48 | i/o cancelled |
| +49 | general error |

Group bases that follow the scheme (their printed entries all line up):

| Base | Group |
|---|---|
| 15100 | sequence control |
| 15200 | machine control |
| 15300 | display |
| 15600 | printer |
| 15700 | label preparation |
| 15800 | weight processing |
| 15900 | initialisation |
| 16000 | operator input |
| 17000 | code editor |
| 17200 | connection layer |
| 17400 | remote interpreter |
| 17700 | input tools |
| 21900 | operating data |
| 22200 | GxNet |

Several other groups in the appendix start at a base that is not a round hundred (17150 storage medium, 17350 device data manager, 17650 database, 17950 list printer, ...) and number their errors freely from there. The scheme does not apply to those, and applying it anyway produces confident nonsense.

`link::internalErrorText()` implements exactly this, and returns empty for anything it cannot take apart.

### The case this was needed for

The device answers `WZV_SDD_START` with `LGW_RETURN` 1 and `LGW_DEBUG` 0x4529 = **17705**, which is in the printed gap. By the rule: 17700 + 5 =

> **input tools: function not available**

### How that turned out

**The dialog works.** Sent without `WW62`, on the same firmware, the window renders with its headline and its label as intended. The refusal was caused by the one field in the telegram whose token code was a deduction rather than a reading: `WZW_SDD_ELEM_COUNT` was being sent as `WW62` because 0x62 is printed in the coding table as an unnamed "counting variable" and its neighbours are 0x60 `WZW_HDL`, 0x61 `WZW_SDD_TYP`, 0x63 `WZW_SDD_ELEM_TYP`. It is not that. What 0x62 really is remains unknown; what is known is that `WZV_SDD_START` must not carry it. `link::DialogSpec` leaves it out by default.

The decode was accurate and misleading at once: "function not available" described the refusal correctly, the input-tool subsystem having no function matching *that field set*, but reads like a verdict on the command. Two rules follow:

1. **A decoded error names the refusal, not the capability.** Nothing in 17705 says the device cannot do dialogs.
2. **When a refused telegram contains a deduced field, vary that field before reasoning about the refusal.** Everything else here was transcribed; the single deduction was the fault.

The codes that did *not* come back still rule out what they ruled out – none of them was the cause, and the cause had no code of its own:

| Code | Would have meant |
|---|---|
| `LGW_RETURN` 151 | wrong mode level |
| `LGW_RETURN` 14, plus `WZW_LICENSE_NR` in `LGV_QUIT` | a licence is missing |
| 17751 `PAE_EWZ_RF_OCC` | synchronisation fault with the display/operating unit |
| 17758 `PAE_EWZ_RF_HDL` | access conflict between the GX and its AB |
| 17786 `PAE_EWZ_DISPL_FALSE` | input not possible with this display |
| 17755 `PAE_EWZ_OCC` | an input is already active |
| 17754 `PAE_EWZ_ANZ_EL` | impermissible number of list elements |

What remains open is the **answer**: `WZV_SDD_RESULT` (WV63) does not come back on the request handle, even when the operator presses well inside the timeout – and, measured afterwards, not on the spontaneous channel either. The server's own log settles where the gap is: between two dialogs three minutes apart there is **no inbound frame at all**, so the device is not sending the result rather than the client failing to read it. And it is not the channel gate: every `GGW_SENDKANAL_*_ENABLE` that exists read 1 on the device this was measured on.

**Where that ends up** is in [`machine-notes.md`](machine-notes.md), *This device never starts a conversation*: it initiates nothing at all, about anything, so the answer was never going to arrive on its own. The configuration path that turned out not to be the constraint is in the same file under *Where the device may send*, and why the send's own timeout says nothing either way is in [`bcs-notes.md`](bcs-notes.md).

There is a second, wrapped entry point, `WZV_LOCK_DIALOG` (WV3D), which contains a `WZV_SDD_START` inside it. It cannot be built: it needs `WZW_INDEX`, and that parameter appears in six telegram descriptions and **in no coding table at all**. Its answer, `WZV_LOCK_DIALOG_RESULT` (WV3F), is marked *reserviert*, so that route is closed at both ends.

Two shapes the captured binary closed along the way, both raised as suspects and both innocent: the two `LGX_CLOSE` are one per block, and the block lengths the server computes from the structure came out right, with none of the three structural error codes raised (3950, 22260 `PAE_GXNET_NESTING_LEVEL`, 30057); and whether the telegram went as one line or two is a property of what is handed to the COM server, not of what reaches the device.

Where field order genuinely matters the reference says so – `ELV_LOGO_FIRST` carries "Die Reihenfolge der Parameter darf nicht verändert werden". `WZV_SDD_START` carries no such note.

## 2. Programmable softkeys, the other way to ask the operator something

Same authorisation level as the terminal note, same subsystem as `WZV_REMOTE_DISPLAY`, all of it 6.21, and it needs no handle that has to be invented.

This was built as a replacement for the dialogs while those looked impossible. They are not impossible – see §1 – so the two are now alternatives rather than a fallback: a dialog is a modal window that can carry a question at length, a softkey is one key on a row. Which is right depends on the question. And the open problem in §1 is getting the *answer* back, which is a property of the connection rather than of the dialog, so it applies to a softkey press too.

**Programming a key – `WZV_REMOTE_TO_SOFTKEY` (WV04):**

| Field | Token | Notes |
|---|---|---|
| `WZW_REMOTE_SOFTKEY_NR` | WW06 | 1–16 (1–12 on a GD). **Optional, and leaving it out is not a no-op** – the properties then apply to every remote softkey at once. |
| `WZW_REMOTE_SOFTKEY_ATTR` | WW08 | -1 delete, 0 passive, 1 active, **3 active and locks every softkey** (undone only by `XCW_UNLOCK_EING`) |
| `WZW_REMOTE_SOFTKEY_TYP` | WW07 | 0 push button, 1 alphanumeric, 2 numeric, 3 switch (6.40), 4 date, 5 time. Optional; the digit count goes with it. |
| `WZW_REMOTE_SOFTKEY_STELLEN` | WW09 | 1–9 digits numeric, 0–30 characters alphanumeric |
| `WZT_REMOTE_SOFTKEY_TEXT` | WT00 | caption, max 20 characters – and the reference adds that how many *appear* depends on character and key width, so 20 is a ceiling, not a promise |

A switch (type 3) has separate captions for its two positions: `WZT_REM_SK_TEXT_ON` (WT04) and `WZT_REM_SK_TEXT_OFF` (WT03), 6 characters each.

**The answer – `WZV_SOFTKEY_TO_REMOTE` (WV05):** `WZW_REMOTE_SOFTKEY_NR`, `WZW_REMOTE_SOFTKEY_TYP`, then `WZT_REMOTE_SOFTKEY_EINGABE` (WT0A, up to 30 characters) for an alphanumeric key or `WZL_REMOTE_SOFTKEY_EINGABE` (WL0A) for a numeric one. A push button carries neither: the press *is* the answer.

Whether a press arrives on the request handle or on the spontaneous channel is not stated anywhere in the reference. That is a thing to measure, not to assume.

Related but not the same: `WZV_GXNET_META_SOFTKEY_TEXT` (WVA4, 9.20) reads and changes the text of a *built-in* softkey, and `WZV_GXNET_META_SOFTKEY_INFO` (WVA6, 12.00 SP5) reports whether one is locked or active.

### "TERMINAL level" is authorisation level 9

The GxNet reference calls it `Berechtigungsebene "TERMINAL"` and the GLM-Emaxx instruction manual never uses that word. The bridge between the two vocabularies is the coding table for `WZW_MODE` (WW0C):

| Value | Since | Meaning |
|---|---|---|
| `'1'`, `'2'` | 6.21 | mode levels 1 and 2 |
| `'T'` | 6.21 | mode level "Terminal" |
| `'0'` | 9.00 | standby |
| **`'9'`** | **10.00** | **mode level "Terminal"** |

The manual's own list of authorisation levels is 0 standby, 1 check settings, 2 article change, 3 create and maintain articles, 4 service and configuration, 5 Bizerba, and **9 "customer-specific operating level"** – the one a site configures for itself by copying softkeys out of the other levels (manual §9.8). So `'T'` is the older name for what newer firmware addresses as `'9'`, and a remote softkey lives in that level and nowhere else.

The value is a word whose **high byte is 0x00 and whose low byte is the ASCII character**: `'9'` = 0x39 = 57, `'T'` = 0x54 = 84. So `A?WW0C` reads the current level and `A!WW0C|57` sets level 9 – which changes what the operator is looking at, so not something to do casually on a running line.

Reading it back has a quirk worth knowing: while the device is in the Terminal level, you read `'9'` if `'9'` was what put it there and `'T'` otherwise. In any other level you simply read that level's own character.

### Remote softkeys take over a position, and the terminal has a way out

The manual, §24.8 "Deleting remote soft keys":

> A higher-ranking EDP system can assign the softkey of the device for its own
> purposes. If a softkey is pressed, in this case, the result goes directly to
> the EDP system. In the event of a fault, it might be necessary to delete the
> softkeys assigned to the EDP system and to switch off the online connection.
> Afterwards, the device can be operated directly again.
>
> `<Configuration> / <Communication configuration> / <Del. remote softkeys>`,
> authorisation level 4.

Three things follow:

1. **A remote softkey claims a position.** Whatever the site had copied into level 9 at that number stops doing its old job until the remote key is deleted. Which position collides with what is site-specific and is not something the protocol can tell you.
2. **There is a manual escape hatch**, at authorisation level 4. Worth telling an operator about before programming anything.
3. **They very probably survive a restart.** The reference does not say so anywhere – but a service function that exists *because the controlling system failed* would be pointless if a power cycle cleared them. Treat this as strong circumstantial evidence and confirm it by switching the device off and on, not as a documented guarantee.

The screen shows up to eight softkeys at once and indicates a second row when there is one, which is where the range of 1–16 comes from.

`GGW_TASTPROG_ENABLE` (GWEC) enables or disables "key programming", and the manual lists what that gates: macros, setting up level 9, renaming softkeys, assigning images, the quick keyboard. **Remote softkeys are not in that list**, so it probably does not gate them – but `A?GWEC` is a free read and settles it.

## 3. The standard-dialog type table

Not a spontaneous-channel matter, but it belongs with the frames above, because a wrong pair is refused and the refusal looks like everything else. `WZW_SDD_TYP` (WW61) and `WZW_SDD_ELEM_TYP` (WW63) are not independent – the reference lists the implemented combinations, and only these:

| `WZW_SDD_TYP` | | `WZW_SDD_ELEM_TYP` | |
|---|---|---|---|
| 1 | single numeric | 1 | numeric |
| 2 | double numeric | 1 | numeric |
| 3 | alphanumeric | 2 | alphanumeric |
| 4 | hidden alphanumeric | 2 | alphanumeric |
| 5 | date | 3 | calendar |
| 6 | time | 3 | calendar |
| 7 | selection (scroll menu) | 4 | selection |
| 8 | display with confirmation | 5 | display |
| 9 | display only | 5 | display |

And a correction to something this document said earlier: **`WW62` is not `WZW_SDD_ELEM_COUNT`.** The subfunction table gives 0x62 no symbolic name at all – the meaning column reads only "Zählvariable". `WZW_SDD_ELEM_COUNT` appears in the field list of `WZV_SDD_START` and nowhere else, so which index carries it is still unknown. What is known is that the device refuses a `WZV_SDD_START` carrying `WW62`, and accepts the same dialog without it.

## 4. Text field limits move with the firmware release

The single most misleading kind of row in the reference: a limit that is stated once and then raised in a later version, in a continuation row that is easy to read past.

| Token | Limit |
|---|---|
| `GGT_SIMPLE_TXT1..30` (GT61...GT7E) | 11 chars at 7.20, **15 from 7.61, 30 from 14.60**; 29 and 30 go to 50 at 16.20 |
| `WZT_REMOTE_DISPLAY_TEXT` (WT02) | 180 characters |
| `WZT_REMOTE_SOFTKEY_TEXT` (WT00) | 20 |
| `WZT_HEADLINE` (WT60), `WZT_LABEL` (WT62) | 30 |
| `WZT_UNIT` (WT61) | 4 |
| `GGT_CTS1..7` (GT31...GT37, code substrings) | 30 |
| `GGT_CAB` (GT20, code build rule) | 749 |
| `GGT_ATX` (GT00) | 1700, and 4000 from 13.20 SP3 on x86 |
| `GGT_ART_TEXT` (GT90) | 64 |
| `GGT_STTX1..50` (static texts) | 60, 70 from 14.20 |
| `WZT_GX_NAME` (WT64) | 35 |

From **14.60** the simple texts hold **30** characters, not 15; a device below that truncates silently. That last step is printed only in the German edition – the English one stops at "as from version 7.61: max. 15 characters" – so a program that trusts the English table refuses half of what a current device accepts. (The 13.20 SP3 in the row above belongs to `GGT_ATX` and its 4000 characters, which is what this line originally confused it with.)

## 5. Dimensional values: what the packed word means

`gxnet`'s binary codec reads the unit/exponent word of a `D` payload as a ten-bit unit code plus a six-bit exponent. That is inferred, and the reference documents something else – two different things, depending on which kind of dimension it is.

**Weights (`~D_GEW_~`)** – the coding of `GGW_GEW_DIMENSION` (GW20):

- bits 8–11: `0x0` kg, `0x1` LB, `0x2` lb oz (to 6.40), `0xF` %; bits 8–15 = `0x11` is lb
- bits 0–7: the exponent as a signed byte – `0x00` = 0, `0xFF` = -1, `0xFE` = -2, `0xFD` = -3, `0xFC` = -4

**Prices (`~D_PRS_~`)** – from the "Allgemein – ~3" table at the front:

- byte 1: not used
- byte 2: the low byte of the `GGW_LAND` code, i.e. **the country**, which is what fixes both the currency and its exponent

`GGW_LAND` (GW01) tabulates 130-odd countries with their BCT currency and exponent – 0 Germany `DEM;-2`, 6 EC `EUR;-2`, 46 Ukraine `UAH;-2`, **62 CIS `RUR;-2`**, 101 China `CNY;-2`, 128–131 the neutral countries `NEU0..NEU3` with 0 to 3 decimal places.

So a price word of `0x003E` is country 62, not "unit 0". A codec that maps 0 to EUR gets the right answer for the wrong reason on some installations and the wrong answer on others.

## 6. The reference's own notation

Printed once, on page 2, and used on every page after it.

| Symbol | Meaning |
|---|---|
| `~` | placeholder for any string inside a subfunction name |
| `?` / `!` | reading / writing subfunction |
| `→` / `←` | the command is **sent** by a GX / **received** by a GX |
| `%` | optionally sendable command |
| `†` / `‡` | an optional command and its conditional reply – **`‡` is sent when `†` was *not*** |
| `§` | non-standalone: at least one other subfunction must precede it |
| `#` | occurs only inside a block command |
| `*` | an unknown number of subfunctions, at least one |
| `[ ]` | logical grouping |
| `{ }` | comment |
| `<...>` | a set of subfunctions; `<>*` lists them all in turn |
| `╟ ... ╢` | the enclosed commands may be given in any order, **and the order chosen is the order they execute in** |
| `↔` | **global data in a distributed system: this command affects the whole system**, not just the addressed device |
| `⊥` | like `§`, but nothing may precede it whose completion time can outlast the receipt of this one |

The `↔` is the one to watch when automating a line with a second printer on it.

### "reserviert" is about the parameters, not the subfunction

The word appears in the *Kodierung der Unterfunktionsparameter* column, so it says that no parameter encoding is defined. It does not say the subfunction is unimplemented, and reading it that way is a mistake worth naming: `XCX_DELETE_UNIQUE_DATA` (**XX13**) is marked `reserviert` and is fully described one column further right, and it is the command this project uses to clear the unique-data buffer.

A command that takes no parameters has nothing to put in that column, which is why so many of them carry the word. The entries that really are undocumented are the ones where the range and the description are empty as well, such as `LGL_START_VALUE` (**LL14**) and `LGV_ERROR_LIST` (**LV02**). `gen_registry.py` sets its `reserved` flag on that combination rather than on the word alone.

### `siehe` and `dto.` carry as much as an explicit table

A coding column reading `siehe GGW_LAND` is not a gap: the reference codes a value set once and points several hundred subfunctions at it. `dto.` in the range column means the same, referred either to that other subfunction or, where there is none, to the row above. `WZW_DISPLAY_ATTRIB` (**WW65**) is the clearest case – it prints nothing at all except `siehe WZW_REMOTE_DISPLAY_ATTR`, and that is where its -1 delete / 0 normal / 1 flashing comes from. `gen_docs.py` follows both, which is worth roughly four hundred named values.

### One description in the English edition is a printing error

`MDW_GET_BUFF` (**MW06**) is described in the English edition as *"End notification that no more data is available"* – which is the description of `MDW_END_OK` (**MW00**), the row above it on the previous page. The German edition has the right one: *"Anfordern von auf der Memo-Card synchron aufgezeichneten Packungsdaten mit impliziter Datenlöschung"*. The difference is not academic, because the implicit deletion is the whole hazard of the command. Checked against both PDFs directly, so it is the vendor's error and not a conversion artefact.

## 7. Batching a changeover: `LGV_SEQUENZ` (LV01)

Permitted contents, printed in both editions: `GG~ | AM~ | XC~ | VM~ | WZV~REMOTE~`. So `GGW_UNIQUE_DATEN`, `XCX_DELETE_UNIQUE_DATA` and `XCV_DBTAB_DATASET` may all travel in one telegram, and a terminal note with them. The SDD family may **not** – only the REMOTE part of W is allowed in.

The acknowledgement rules are specific and easy to get wrong:

- positive acknowledgements of the *individual* commands are **not** sent;
- but the individual commands' `LGW_ACCEPT` messages **are**, when a command signals logical acceptance ahead of its own completion;
- on failure the block *and* the failing command are acknowledged negatively, and processing stops there.

So one `LGV_SEQUENZ` can produce several telegrams in reply. A receive loop that stops after the first leaves the rest in the queue.

`LGV_QUIT` (LV00) carries `LGW_RETURN`, `LGW_UFKENN` (the class code of the command that failed), and optionally `LGW_DEBUG`, `LGW_UFKENN_INTERN` (the *single* command inside a block that failed) and `WZW_LICENSE_NR`.

## 8. Text encoding and formatting

The reference specifies the character range for strings as **0x20–0x7F** for ordinary texts, and 0x01–0x7F for code substrings and database filters. Cyrillic going through intact is a property of a Unicode device (`GGW_ZEICHEN_SATZ` / GW0C = 8 is UTF-8, from 13.00), not something the string specification promises.

**`^` introduces a formatting sequence** in any text that allows one, and `^^` is how you get a literal `^`. The sequences: `^(0...x);` character size, `^f±` frame, `^g±` grey, `^i±` invert, `^u±` underline, `^x±` strike, `^h±` shading, `^b±` bold, `^A±` autoline, `^Ac` / `^Ar` / `^Al` alignment, `^O(0...x)` character spacing, `^L(0...x)` line spacing. Tab is 0x09, line break 0x0A, soft hyphen 0x0D – single control characters, hex-coded, i.e. escaped as `@09`, `@0A`, `@0D`.

Anything that puts operator-supplied text into a label field has to consider that a `^` in it is not a `^`.

## 9. Numbers that need a decoder, and where they turn up

Several answers are correct, complete and unreadable. `SRL_NET_CHANNEL_BITMAP = 66` and `LGW_UFKENN = 1891` are the two that cost the most time here, and they are not special: the reference prints the reading for each of them in a different chapter from the telegram that carries them.

`link::annotateValue(token, value)` is the one place that knows these readings, so the console tree, the exchange log and the panels say the same thing about the same number. It returns empty for anything it has nothing to add about, which is most tokens.

What it currently decodes, and why each earned a place:

| Token | Reading |
|---|---|
| `SL8C` `SRL_NET_CHANNEL_BITMAP` | bit 0 internal, bit 1 = A ... bit 11 = K → `A, F` |
| `LW02` `LGW_UFKENN` | the token's own 16-bit code → `GT63 GGT_SIMPLE_TXT3` |
| `PL13` `PSL_PCK_ERR_FLAGS` | 26 named bits, of which 24 is "no unique data" |
| `LW01` `LGW_RETURN`, `LW03` `LGW_DEBUG` | the two error tables (§1) |
| `WW0C` `WZW_MODE` | ASCII level in the low byte; `'9'`/`'T'` are TERMINAL |
| `WW68` `WZW_EXIT`, `WW61` `WZW_SDD_TYP`, `WW63` `WZW_SDD_ELEM_TYP` | the dialog tables (section 3) |
| `GW58` `GGW_TEL_QUIT_MODE`, `AW75` `AMW_TRIG2_MSG_AKTIV`, `GW7D` `GGW_UNIQUE_DATEN` | small enumerations that are always looked up |
| `GW38` `GGW_SBUS_KONFIG`, `GL38` `GGL_SBUS_ADR`, `GWBF`/`GWC0`–`GWC3`/`GW74`/`GW75` | the channel settings, see [`machine-notes.md`](machine-notes.md) |
| `WW2A` `WZW_LICENSE_VAL`, `SW98` `SRW_LABELER_STATE`, `SW85` `SRW_UNICODE_DEVICE` | three more where the number alone says nothing |

**Where the same blocks turn up.** `SRV_UFKENN_CHANNEL_INFO` (SV5E) is not a command – it is the nested block inside **`SV5A`** (bei Änderung versenden), **`SV5B`** (Zusatz zu PSV_PCK), **`SV5C`** (Inhalt von PSV_DATA), **`SV5D`** (Zusatz zu Statistik) and **`SV5F`** (Statistik-Reports), and their writing counterparts `SV6A`–`SV6F`. Every one of those answers is a list of `LW02` + `SL8C` pairs, so decoding the pair once covers all ten telegrams. `SL8C` also appears on its own as an ordinary read.

## 10. Two shapes a value can arrive in, and the bug that follows

A reply carries its values in one of two places, and which one depends on how the server handed it over rather than on anything about the telegram:

- **interleaved** – `A!GW7D|0`, one line, values following their tokens. This is what `ReceiveOne` gives, and what `parseOneLine` accepts. **The values end up in `telegram.records[0]`, positionally; the nodes it builds carry the token layout and empty values.**
- **header plus data** – a token line and then a record line, which `parseLines` accepts, and which also fills `records`.

The trap is the first case, because the shape suggests otherwise. A buffer read comes back as one long interleaved line full of `PSV_DATA` blocks, and code that walks the node tree reading `node.value` finds every block and reads every field as absent. The Buffer tab did exactly this and filled a table with the right number of rows of dashes – a failure that looks like a device with no data rather than a client that cannot read it.

`link::valueOf(telegram, token)` resolves both forms, and `link::valueOf(telegram, scope, token)` does the same within one block, which is what a list of packages needs: the unscoped one returns the first `GGL_PLUNR` in the telegram for every package in it.

Anything reading a device's answer should go through those rather than touching `node.value`.
