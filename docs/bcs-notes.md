# `_connect.BRAIN`: the server between the program and the device

The COM/DCOM automation server that carries GxNet telegrams. What the manual documents, what it documents in the wrong place, and what only the traffic shows. The method list itself is in [`../README.md`](../README.md) under *The automation interface*.

**Originals these notes were taken from:** `_connect.BRAIN.md` (Руководство пользователя _connect.BRAIN, 38001212005 ru), `connectService-Interface.md`, and `Get-Member` on the automation object.

## How to read these notes

The vendor manuals are **not in this repository** – they are Bizerba's and stay out of it, so every claim here carries something to search the originals with. The citation convention, and why the German edition, are set out in [`gxnet-notes.md`](gxnet-notes.md), *How to read these notes*. Same rules apply here: a deduction is shown as one, and a measurement says so.

Companion files:

- [`gxnet-notes.md`](gxnet-notes.md) – the telegram language itself.
- [`bcs-notes.md`](bcs-notes.md) – `_connect.BRAIN`, the COM/DCOM server that carries the telegrams.
- [`machine-notes.md`](machine-notes.md) – how a GLM-family installation is wired: channels, outgoing lines, the memory card, unique data.

---

## 1. Collecting what belongs to no request

`ReceiveOne(out szHeaderData, szHandle, lTimeout, out lStatus)` documents `szHandle` as "дескриптор, который отдается методом Send". Taken at face value that closes the subject: a record belonging to no send can never be received.

It is not the whole story, and the correction is in a different method's paragraph. `ReceiveOneWithoutAck` has the same parameter list, and there the same argument is described as **"Дескриптор для спонтанных данных (DUSTBIN или созданная очередность)"**. The two methods differ only in who acknowledges, so the handle means the same thing in both – and passing a queue name works with either.

The queues:

- `CreateReceiveQueue(out szQueueName)` makes one.
- `SetReceiveQueueFilter(szQueueName, szFilter)` routes arrivals into it by leading token – the manual's own example is `PV05` for package records, and it says to call it twice with the same queue to catch `PV05` and `PV06`.
- Without any filter, spontaneous data goes to a queue named **`DUSTBIN`**, and that name is a handle you can pass, not a description of what happened to it.

So the minimum viable listener is: receive against `"DUSTBIN"` on a short timeout whenever the line is idle. No queue creation, no filters, nothing lost to a filter written for the wrong token. `link::Transport::receiveSpontaneous` is exactly that, and `ReceiveOne` rather than `ReceiveOneWithoutAck` because the former acknowledges by itself – an unacknowledged record leaves the device waiting, and the manual warns specifically that a GLP will not resume after a lock until the record is acknowledged.

There are also `DataArrival(szQueueName)` and `RemoteDataArrival(szQueueName)` events, which would remove the polling. They need a COM event sink; polling a queue costs one call every few hundred milliseconds and needs none.

## 2. Subscribing to spontaneous messages

`nTelegramType = 1` in `Open`. Only one client at a time may have it. On a line where BRAIN2 is the other client, the question is whether BRAIN2 wants it – and here it does not: package data reaches it through the memory-card buffer, which it polls with `MW06`. **Measured, 2026-08-06: an open connection with spontaneous messages enabled does not stop BRAIN2 reading the memory card.**

## 3. A `Send` timeout is not a delivery failure

BCS `Send(szHeader, szData, out szHandle, nTimeout, out lStatus)` documents `nTimeout` as "интервал времени, в течение которого метод ожидает **ответа от устройства**". Not delivery – the answer. A telegram that produces no application-level answer therefore burns the whole timeout and reports one, and it does so *after arriving perfectly well*.

Measured, both halves together:

- the client sends `WZV_SDD_START`, waits 3033 ms, logs a timeout;
- the server's own log, decoded, shows the same telegram acknowledged by the device with `LGW_QUIT_OK` **3 ms** after it went out, and the dialog appears on the terminal.

So a timeout on a write says nothing about whether the device got it. Two things follow for anything built on this transport:

1. **Do not report it as a failure.** The window was on the operator's terminal while the program said the send had failed.
2. **The protocol-level acknowledgement never reaches the client.** `LGW_QUIT_OK` is consumed by `_connect.BRAIN` as "the send succeeded"; a refusal *does* come through, which is why 17705 was visible. So an answer arriving is evidence, and an answer not arriving is not.

`Exchange::send_status` carries `lStatus` as `Send` itself reported it, kept apart from the exchange's final status. The vendor's samples receive only while that value is 2; this code receives whenever the caller asked for a reply. Which is right cannot be settled by argument, so the value is now in the log beside every sent line rather than folded away.

## 4. Reading the server's own logs

`Bizerba._connect.BRAIN.Server_<date>.log` writes each frame as hex:

    13:09:00::336 A-> D05101029660003891600001916100079760000CD09...
    13:09:00::339 A<- D077020141009660

That is the compact binary form, and `gxlint` reads it – pipe the log lines in and it prints the token tree with symbolic names. `gxnet::binary` exists for this and nothing else.

Two things this settles that no amount of client-side logging can. First, what the device actually received, as opposed to what was handed to the server. Second, and more useful in a case like the dialog answer: **the absence of an inbound frame**. A gap in the log between two outbound telegrams is direct evidence that the device sent nothing, which separates "our program is not reading it" from "there is nothing to read".
