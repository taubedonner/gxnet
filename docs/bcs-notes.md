# `_connect.BRAIN`: the server between the program and the device

The COM/DCOM automation server that carries GxNet telegrams. What the manual documents, what it documents in the wrong place, and what only the traffic shows.

**Originals these notes were taken from:** `_connect.BRAIN.md` (Руководство пользователя _connect.BRAIN, 38001212005 ru), `connectService-Interface.md`, and `Get-Member` on the automation object.

## How to read these notes

The vendor manuals are **not in this repository** – they are Bizerba's and stay out of it, so every claim here carries something to search the originals with. The citation convention, and why the German edition, are set out in [`gxnet-notes.md`](gxnet-notes.md), *How to read these notes*. Same rules apply here: a deduction is shown as one, and a measurement says so.

Companion files:

- [`gxnet-notes.md`](gxnet-notes.md) – the telegram language itself.
- [`bcs-notes.md`](bcs-notes.md) – `_connect.BRAIN`, the COM/DCOM server that carries the telegrams.
- [`machine-notes.md`](machine-notes.md) – how a GLM-family installation is wired: channels, outgoing lines, the memory card, unique data.

`link/` is the code these notes describe: `Transport`, `BcsTransport`, `Worker`, and the composite telegrams built on them.

---

## 1. The automation interface

**The entry point is misspelled in the manual.** The COM object registers as `BCS.BCSComunnication.1` – one `m`, two `n`. The documented `BCS.BCSCommunication.1` does not exist. Of the three interface versions only `.1` is complete: `.2` and `.3` are progressively stripped and lack `SendOne`, `ReceiveOne` and the receive-queue methods.

`Get-Member` on the object, which is the only complete listing there is – the manual documents a subset and omits the FTP family entirely.

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

**The listing does not say which parameters are out.** `Get-Member` prints a positional type list and nothing else, so the direction of every parameter has to be established by use. What is settled:

```
Open(user, device, nTelegramType, nAccess, bLightLicenceEnable)
Send(szHeader, szData, out szHandle, nTimeout, out lStatus)
SendOne(szHeaderData, out szHandle, nTimeout, out lStatus)
ReceiveOne(out szHeaderData, szHandle, nTimeout, out lStatus)
```

The return value is the call's own success; `lStatus` is the transfer state (0 ok, 1 timeout, 2 more data waiting). `IsUnicodeDevice(short)` is undocumented in both directions – the `short` may be in or out, and this code passes it by reference.

Every method returns `int` except `Error`, `ErrorHeaderData` and `SetParam`. The FTP and SFTP families, `DeviceTest`, `GetCategory`, `SetParam` and `IsUnicodeDevice` appear nowhere in the manual.

**The FTP family is undocumented in both directions**, and one of its methods has been measured rather than guessed. `ListFilesOnServerFTP` called as `(out string, path)` returns **0 for every path**, including `""` and `"/"`, leaves the out slot empty and hands the second argument back unchanged. A method that succeeds on an empty path is not reporting on the path, so its return value cannot distinguish one directory from another – and neither argument is being written to, which puts the listing somewhere the caller is not looking. The reading that fits: `DownloadFileFTP(szLocalFilepath, szServerFilepath)` names the **local** side first, so by analogy the first parameter here is a local file to write the listing into, an in parameter rather than an out one. Untested at the time of writing; the panel's preset now sends that shape.

## 2. Collecting what belongs to no request

`ReceiveOne(out szHeaderData, szHandle, lTimeout, out lStatus)` documents `szHandle` as "дескриптор, который отдается методом Send". Taken at face value that closes the subject: a record belonging to no send can never be received.

It is not the whole story, and the correction is in a different method's paragraph. `ReceiveOneWithoutAck` has the same parameter list, and there the same argument is described as **"Дескриптор для спонтанных данных (DUSTBIN или созданная очередность)"**. The two methods differ only in who acknowledges, so the handle means the same thing in both – and passing a queue name works with either.

The queues:

- `CreateReceiveQueue(out szQueueName)` makes one.
- `SetReceiveQueueFilter(szQueueName, szFilter)` routes arrivals into it by leading token – the manual's own example is `PV05` for package records, and it says to call it twice with the same queue to catch `PV05` and `PV06`.
- Without any filter, spontaneous data goes to a queue named **`DUSTBIN`**, and that name is a handle you can pass, not a description of what happened to it.

So the minimum viable listener is: receive against `"DUSTBIN"` on a short timeout whenever the line is idle. No queue creation, no filters, nothing lost to a filter written for the wrong token. `link::Transport::receiveSpontaneous` is exactly that, and `ReceiveOne` rather than `ReceiveOneWithoutAck` because the former acknowledges by itself – an unacknowledged record leaves the device waiting, and the manual warns specifically that a GLP will not resume after a lock until the record is acknowledged.

There are also `DataArrival(szQueueName)` and `RemoteDataArrival(szQueueName)` events, which would remove the polling. They need a COM event sink; polling a queue costs one call every few hundred milliseconds and needs none.

## 3. Subscribing to spontaneous messages

`nTelegramType = 1` in `Open`. Only one client at a time may have it. On a line where BRAIN2 is the other client, the question is whether BRAIN2 wants it – and here it does not: package data reaches it through the memory-card buffer, which it polls with `MW06`. **Measured, 2026-08-06: an open connection with spontaneous messages enabled does not stop BRAIN2 reading the memory card.**

## 4. Use `Send`, not `SendOne`

`Send(header, data)` takes the two parts separately. `SendOne` takes one string and splits it itself, and what it expects between the parts is undocumented; an interleaved telegram gives it nothing to split on and it fails with

    2712 (BCS_GX) Telegrammaufbau ist fehlerhaft
    source: CConvDataToBxNetBase::SeperteHeaderData

– "separate header data": the server takes the single string apart into a header and a data part, and our interleaved encoding gives it nothing to split on. `Send` hands over the two parts already separated, so that step does not arise. `BcsTransport::Options::send_one_separator` is a guess at what `SendOne` wants between them, and it is a guess.

## 5. The exchange is asymmetric

Requests go out as two parts and answers come back from `ReceiveOne` interleaved on one line:

```
->  A!GT63    ->  1111        header, then data
<-  A!GT63|1111               interleaved, one line
```

Read the answer as a header-plus-data pair and the parser takes the value for a token and rejects the whole reply.

## 6. A `Send` timeout is not a delivery failure

BCS `Send(szHeader, szData, out szHandle, nTimeout, out lStatus)` documents `nTimeout` as "интервал времени, в течение которого метод ожидает **ответа от устройства**". Not delivery – the answer. A telegram that produces no application-level answer therefore burns the whole timeout and reports one, and it does so *after arriving perfectly well*.

Measured, both halves together:

- the client sends `WZV_SDD_START`, waits 3033 ms, logs a timeout;
- the server's own log, decoded, shows the same telegram acknowledged by the device with `LGW_QUIT_OK` **3 ms** after it went out, and the dialog appears on the terminal.

So a timeout on a write says nothing about whether the device got it. Two things follow for anything built on this transport:

1. **Do not report it as a failure.** The window was on the operator's terminal while the program said the send had failed.
2. **The protocol-level acknowledgement never reaches the client.** `LGW_QUIT_OK` is consumed by `_connect.BRAIN` as "the send succeeded"; a refusal *does* come through, which is why 17705 was visible. So an answer arriving is evidence, and an answer not arriving is not.

`Exchange::send_status` carries `lStatus` as `Send` itself reported it, kept apart from the exchange's final status. The vendor's samples receive only while that value is 2; this code receives whenever the caller asked for a reply. Which is right cannot be settled by argument, so the value is now in the log beside every sent line rather than folded away.

**Never wait for a reply to a write.** Waiting buys the full timeout and then reports a timeout over an operation that worked. A negative acknowledgement is not lost – it comes back as a failed `Send` carrying the device's own words. Confirm a write by reading the value back.

## 7. Fill the data field of a read

The reference is right that a read is a header alone, but the BCS parser rejects an empty field for a `D`, `L` or `W` command, and says so three times over:

    2713  Datenwert fuer ein Kommando ist keine Zahl
    2721  Die Zeichenfolgen '| |' bzw. '||' sind nicht erlaubt
    2718  Der fehlerhafte Datensatz wird in einer der naechsten Versionen
          von _connect.BRAIN nicht mehr toleriert

The last one is the reason this matters: it goes through today under a compatibility mode the server says it is going to withdraw. `link::readPlaceholderData()` supplies a `0` per numeric payload token; sending a zero costs nothing, because the direction is `?` and the value in a read is not what the device answers with.

Text fields are left empty; the server's complaint names D, L and W only. Dimension fields are left empty too, and that is a known gap: a placeholder would need a unit as well as a number, and the reference does not say what a neutral unit is.

## 8. Direction can be the whole bug

A command carrying a parameter looks like a write and may still be a read: `MDW_GET_BUFF` takes a timeout in milliseconds and is polled with `A?MW06|3000`. Sent as a write, the device answers `LGW_RETURN` 2154.

## 9. An unreachable device does not look like a timeout

A send to a device that is switched off fails with *Senden der Anfrage der Unicodeeinstellung des Gerätes fehlgeschlagen* – asking the device for its Unicode setting is simply the first thing the server does, so it is the first thing that fails when nobody answers.

`Open` succeeds against an offline device: it only registers the client with the server.

The same message once looked like evidence against `BcsTransport::Options::probe_text_mode`, the start-up `IsUnicodeDevice` call, and it was not: the device was simply off. The probe is kept because the setting decides how text is escaped, and getting that wrong shows up as a mangled label rather than an error. `SRW_UNICODE_DEVICE` (SW85) answers the same question with an ordinary telegram, so it can be turned off without losing the information.

## 10. Reading the server's own logs

`Bizerba._connect.BRAIN.Server_<date>.log` writes each frame as hex:

    13:09:00::336 A-> D05101029660003891600001916100079760000CD09...
    13:09:00::339 A<- D077020141009660

That is the compact binary form, and `gxlint` reads it – pipe the log lines in and it prints the token tree with symbolic names. `gxnet::binary` exists for this and nothing else.

Two things this settles that no amount of client-side logging can. First, what the device actually received, as opposed to what was handed to the server. Second, and more useful in a case like the dialog answer: **the absence of an inbound frame**. A gap in the log between two outbound telegrams is direct evidence that the device sent nothing, which separates "our program is not reading it" from "there is nothing to read".

It carries an error number, source file and line for every failure, where the client sees only the last message. The numbers in a `(GX)` error are the device's `LGW_RETURN`, so the same table decodes both.
