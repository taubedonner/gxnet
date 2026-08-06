// SPDX-License-Identifier: MIT
#ifndef GXNET_LINK_OPERATIONS_HPP
#define GXNET_LINK_OPERATIONS_HPP

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "gxnet/telegram.hpp"

/// Composite operations: telegrams that are more than one subfunction, and the
/// reading of the answers that come back.
///
/// These live here rather than in the panels that use them so their wire form
/// is covered by tests. Every shape below is transcribed from the reference,
/// and the transcription is what the tests pin down -- a device is a slow and
/// expensive way to find out that a block was assembled in the wrong order.
namespace gxnet::link {

/// The value the device reported for one token, wherever the parser put it.
///
/// An answer arrives in one of two shapes and the caller should not have to
/// know which: `ReceiveOne` hands back the interleaved form, where values sit
/// on the nodes, while a header followed by data lines carries them in the
/// records, in header order. Both are searched, first match wins.
[[nodiscard]] std::optional<Value> valueOf(const Telegram& telegram, Token token);

/// The same, but only inside one block.
///
/// A buffer read answers with many `PSV_DATA` blocks that each carry a `GGL_PLUNR`,
/// and the unscoped lookup above would return the first one in the telegram for
/// every package. `scope` must be a node of `telegram`; field positions are
/// still counted across the whole header, because that is how the record is
/// laid out.
[[nodiscard]] std::optional<Value> valueOf(const Telegram& telegram, const Node& scope, Token token);

/// What a value means, for the tokens whose numbers say nothing on their own.
///
/// `SRL_NET_CHANNEL_BITMAP = 66` and `intern, A, F` are the same fact, as are
/// `LGW_UFKENN = 1891` and `GT63 GGT_SIMPLE_TXT3`. One place knows these
/// readings so that every view of a telegram agrees about the same number.
///
/// Returns empty when there is nothing to add, which is the common case.
[[nodiscard]] std::string annotateValue(Token token, const Value& value);

/// LGW_RETURN, decoded. Empty for a code the reference does not list.
///
/// Worth decoding rather than printing raw: `4` is the "fremdes Kommando" that
/// a device answers when it has no such subfunction, and `2650` is the one a
/// PLU change gives for a number that is not in the database -- both are
/// ordinary outcomes that read as mysterious numbers.
[[nodiscard]] std::string_view returnCodeText(std::int32_t code);

/// LGW_RETURN out of a reply, when it carried one. An acknowledgement may
/// arrive as LGW_QUIT_OK instead, which has no code and means it went through.
[[nodiscard]] std::optional<std::int16_t> returnCodeOf(const Telegram& reply);

/// LGW_DEBUG, decoded. Empty when the code cannot be taken apart this way.
///
/// The reference prints an appendix of several thousand internal error codes
/// and then, on its last pages, prints the rule they are built from: every
/// module is given a base, and a fixed set of offsets from that base always
/// means the same thing.
///
///     +1 overflow   +2 underflow   +3 data error   +4 initialisation
///     +5 function not available    +6 fatal manager error   +7 invalid id
///     +10 invalid handle  +15 memory  +19 parameter  +22 timeout   ...
///
/// That matters because the appendix does not print every code -- it prints the
/// ones somebody bothered to list. The input-tool group runs 17701, 17702, then
/// jumps to 17715, and the dialogs were refused with **17705**, in the gap. By
/// the published rule that is 17700 + 5, "function not available".
///
/// Which was true and misleading at once: the function was unavailable *for the
/// field set being sent*. Removing the one deduced field made the same dialog
/// render. Read a decode as a description of the refusal, never as a verdict on
/// the command -- and the device is no help here either, its own error resource
/// has no text for 17705.
///
/// Only groups whose base is a round hundred and whose printed entries follow
/// the scheme are decoded here. Everything else returns empty rather than a
/// plausible invention -- a wrong decoding of an error code is worse than none.
[[nodiscard]] std::string internalErrorText(std::int32_t code);

/// The data line to send for a telegram that has no record of its own.
///
/// A read is a header and nothing else: that is what the reference describes
/// and what the codec produces. BCS wants more than that. Its parser rejects an
/// empty field for a D, L or W command, and says so three times over:
///
///     2713  Datenwert fuer ein Kommando ist keine Zahl
///     2721  Die Zeichenfolgen '| |' bzw. '||' sind nicht erlaubt
///     2718  Der fehlerhafte Datensatz wird in einer der naechsten Versionen
///           von _connect.BRAIN nicht mehr toleriert
///
/// The last one is the reason this exists: it goes through today under a
/// compatibility mode the server says it is going to withdraw. Sending a zero
/// costs nothing, because the direction is `?` and the value in a read is not
/// what the device answers with.
///
/// Text fields are left empty; the server's complaint names D, L and W only.
/// Dimension fields are left empty too, and that is a known gap: a placeholder
/// would need a unit as well as a number, and the reference does not say what a
/// neutral unit is.
///
/// Returns an empty string when nothing needs filling in, which is the case for
/// a command, a block, or a read of text alone.
[[nodiscard]] std::string readPlaceholderData(const Header& header);

// --- PLU ------------------------------------------------------------------

/// A full PLU change: what pressing the PLU key on the terminal does.
///
/// `XCV_DBTAB_DATASET` (XV00), and the distinction from simply writing
/// GGL_PLUNR is the whole point of it. Writing GL19 sets a number in the
/// current working record and nothing else; XV00 performs, in the reference's
/// words, "the related preparatory and follow-up (internal) actions":
///
///   - save the article total,
///   - import the PLU data,
///   - import all indirect data related to that PLU.
///
/// The same command imports label, MinMax, machine, code-structure and text
/// data records when the matching numbers are supplied; every parameter is
/// optional, so a telegram carrying only GGL_PLUNR is a PLU change and nothing
/// more. That is what this builds.
///
/// The customer number is a second key: the database holds a value set per
/// (PLU, customer) pair, and leaving it out selects the PLU alone.
///
/// The device answers LGW_QUIT_OK or LGV_QUIT with LGW_RETURN. Documented
/// codes for this command: 8 = the unit changed, 9 = the new record needs an
/// operating mode this configuration does not support, 2650 = no such record.
[[nodiscard]] Telegram pluChange(std::int32_t plu, std::optional<std::int32_t> customer = std::nullopt);

// --- package buffer -------------------------------------------------------

/// Poll the memory-card package buffer: MDW_GET_BUFF (MW06).
///
/// A **read**, and that is the entire content of this function. The command
/// carries a timeout in milliseconds as its word payload, which makes it look
/// like a write, and sent as one the device answers LGW_RETURN 2154,
/// "communication error". A polling client appears in the device commlog as
///
///     9077 0102  D106 0BB8
///
/// where the leading 0x90 is `?` and 0xD0 is `!`. Same payload, other
/// direction.
///
/// **Reading the buffer deletes what it returns.** The reference describes the
/// command as "transfer of package data and implicit deletion of this
/// transferred data to the memory card". Where another client already polls the
/// buffer, every record collected here is one it will never see, and the codes
/// in it cannot be recovered. Nothing running beside a live line should call
/// this.
///
/// The word payload is documented as a requested buffer size in bytes, range
/// 0 / 2000, rather than as a timeout. Values above that range are seen in
/// practice, so which of the two readings is right is unsettled.
[[nodiscard]] Telegram bufferPoll(std::int16_t timeout_ms);

// --- standard dialogs (SDD) -----------------------------------------------

/// WZW_SDD_TYP: the kind of dialog as a whole.
///
/// The reference lists nine; these are the two that need no keyboard from the
/// operator and no undocumented parameter from us. Each pairs with exactly one
/// element type, which is why `WZW_SDD_ELEM_TYP` is not a separate argument
/// below -- the reference tabulates the permitted combinations and the wrong
/// pairing is not a thing a caller should be able to express.
enum class DialogKind : std::int16_t {
    /// Type 8, elements of type 5. Text plus a confirmation.
    Confirm = 8,
    /// Type 7, elements of type 4. A scroll menu; the result names the entry.
    Selection = 7,
};

/// One element of a dialog: one WZV_SDD_DATA record.
struct DialogItem {
    /// WZT_LABEL. The reference caps it at 30 characters.
    std::string label;
    /// WZW_SDD_ID, echoed back in the result. Free to choose: the reference
    /// only requires it to identify the element among several. Sent for
    /// selection elements (WZW_SDD_ELEM_TYP 4).
    std::int16_t id = 0;
    /// WZW_DISPLAY_ATTRIB, sent for display elements (type 5). Shares the
    /// coding of WZW_REMOTE_DISPLAY_ATTR: -1 delete, 0 normal, 1 flashing.
    std::int16_t attrib = 0;
};

/// WZW_SDD_ELEM_TYP the reference pairs with a dialog type.
///
/// The two are not independent -- the coding table for WZW_SDD_ELEM_TYP prints
/// the permitted combinations, and a pairing outside them is not something the
/// device is obliged to make sense of. Returns 0 for a type the table does not
/// list.
[[nodiscard]] std::int16_t pairedElementType(std::int16_t dialog_type);

/// A standard dialog, spelled out field by field.
///
/// Every optional and every deduced part of WZV_SDD_START is a member here,
/// because for a long time the device refused the telegram and the reason was
/// not known: LGW_RETURN 1 with LGW_DEBUG 0x4529, an input-tool error the
/// reference's own list skips over, and an empty window on the terminal.
///
/// **Settled by varying it.** The offending field was WZW_SDD_ELEM_COUNT, whose
/// token code was a deduction rather than a reading. Without it the dialog
/// renders correctly. The knobs stay, because the answer to the next question
/// came the same way.
struct DialogSpec {
    /// WZW_SDD_TYP. 7 = selection, 8 = display with confirmation, 9 = display
    /// only. The lower types need numeric or alphanumeric input elements, whose
    /// fields this does not build.
    std::int16_t type = static_cast<std::int16_t>(DialogKind::Confirm);

    /// WZW_SDD_ELEM_TYP for every element. Zero takes the paired value.
    std::int16_t element_type = 0;

    /// WZW_HDL, chosen by the caller and returned in the result.
    std::int16_t handle = 1;

    /// WZT_HEADLINE, capped at 30 characters by the reference.
    std::string headline;

    std::vector<DialogItem> elements;

    /// WZW_SDD_ELEM_ACTIVE: a position among the elements, one based, not an
    /// id. The reference marks it optional.
    std::int16_t active = 1;

    /// Send WZT_HEADLINE at all. Not marked optional, so leaving it out is an
    /// experiment rather than a supported form.
    bool with_headline = true;
    /// Send WZW_SDD_ELEM_COUNT as WW62.
    ///
    /// Off by default: a WZV_SDD_START carrying WW62 is refused with an internal
    /// code in the input-tool range, and the same telegram without it renders.
    /// See the note on the token in operations.cpp.
    bool with_element_count = false;
    /// Send WZW_SDD_ELEM_ACTIVE.
    bool with_active = false;
    /// Close each block with an explicit LGX_CLOSE. Both forms are legal -- the
    /// reference's own worked examples include one that never closes its block
    /// -- so this too is worth being able to vary.
    bool close_blocks = true;
};

[[nodiscard]] Telegram dialog(const DialogSpec& spec);

/// A dialog with a message and a confirmation -- WZW_SDD_TYP = 8.
///
/// `handle` is WZW_HDL, chosen by the caller and returned in the result, which
/// is what pairs an answer with the question that asked it.
///
/// Unlike a WZV_REMOTE_DISPLAY note, this one the operator can get out of: the
/// result carries WZW_EXIT, and 1 means they left with HOME. That is the whole
/// reason to prefer it for anything the operator has to answer.
///
/// `with_element_count` sends WZW_SDD_ELEM_COUNT as WW62, and defaults to off
/// because the device refuses a dialog that carries it. See `DialogSpec`.
[[nodiscard]] Telegram confirmDialog(std::int16_t handle, std::string headline, std::string message,
                                     bool with_element_count = false);

/// A scroll menu -- WZW_SDD_TYP = 7.
///
/// `active` is the one-based index of the entry the cursor starts on
/// (WZW_SDD_ELEM_ACTIVE); the reference numbers it as a consecutive count of
/// the WZV_SDD_DATA records within the telegram, so it is a position, not an
/// id. Out-of-range values are clamped to the first entry.
[[nodiscard]] Telegram selectionDialog(std::int16_t handle, std::string headline, std::span<const DialogItem> items,
                                       std::int16_t active = 1, bool with_element_count = false);

/// What WZV_SDD_RESULT (WV63) carried.
struct DialogResult {
    /// WZW_HDL, as given when the dialog was opened.
    std::int16_t handle = 0;
    /// WZW_EXIT: 0 = input okay, 1 = cancelled with HOME.
    std::int16_t exit = 0;
    /// WZW_SDD_ID of the chosen entry, for a selection dialog.
    std::optional<std::int16_t> id;
    /// WZT_LABEL of the chosen entry, for a selection dialog.
    std::optional<std::string> label;

    [[nodiscard]] bool confirmed() const { return exit == 0; }
};

/// Reads a WZV_SDD_RESULT out of a reply. Nullopt when the reply is something
/// else -- an acknowledgement, an error, or nothing recognisable.
[[nodiscard]] std::optional<DialogResult> parseDialogResult(const Telegram& reply);

/// `A?WV63|WW60|<handle>|LX02` — asks for a dialog's answer by its handle.
///
/// For devices that deliver nothing unasked: the answer has to be requested
/// rather than waited for. The reference marks `WV63` with neither `?` nor `!`,
/// so reading it is unspecified rather than forbidden, and a device without it
/// answers `fremdes Kommando`.
[[nodiscard]] Telegram dialogResultQuery(std::int16_t handle);

/// `A?WW68` — `WZW_EXIT` on its own: 0 input OK, 1 cancelled with HOME.
///
/// The fallback when `dialogResultQuery` is refused. `WZW_EXIT`, `WZW_SDD_ID`
/// and `WZW_HDL` are ordinary word subfunctions with printed value ranges, so
/// each may be readable even where the block containing them is not.
[[nodiscard]] Telegram dialogExitQuery();

// --- remote softkeys ------------------------------------------------------

/// WZW_REMOTE_SOFTKEY_TYP: what the key does when it is pressed.
enum class SoftkeyType : std::int16_t {
    /// A push button. Pressing it sends the answer and nothing else -- no
    /// keyboard, no value. This is the one that answers a yes/no question.
    Button = 0,
    Alphanumeric = 1,
    Numeric = 2,
    /// Since 6.40. Has separate captions for its two positions.
    Switch = 3,
    Date = 4,
    Time = 5,
};

/// One programmable key in the terminal's "TERMINAL" authorisation level.
///
/// This is the other way to ask the operator something, and on this device it
/// is the way that works. `WZV_SDD_START` comes back refused with an internal
/// code that decodes to "function not available" (see `internalErrorText`),
/// while `WZV_REMOTE_DISPLAY` -- the note, same subsystem as this -- is
/// verified against the live line. The softkey family is 6.21, needs no handle
/// that we have to invent, and its answer arrives as `WZV_SOFTKEY_TO_REMOTE`.
///
/// The trade against a dialog: a softkey is a key on a row, not a modal window,
/// so it cannot state a question at length. Pair it with a terminal note for
/// the text and use the key for the answer.
struct SoftkeySpec {
    /// WZW_REMOTE_SOFTKEY_NR, 1 to 16 (1 to 12 on a GD). **Leaving it out is
    /// not a no-op**: the reference says the properties then apply to every
    /// remote softkey at once, which is how they are all cleared in one go.
    std::optional<std::int16_t> number = 1;

    /// WZW_REMOTE_SOFTKEY_ATTR: -1 delete, 0 passive, 1 active.
    ///
    /// 3 is also active, and additionally locks every softkey once the telegram
    /// has been sent successfully -- the lock comes off with `XCW_UNLOCK_EING`.
    /// Deliberately not offered as a named constant: a lock left set by a
    /// program that died is a stopped line.
    std::int16_t attribute = 1;

    /// WZW_REMOTE_SOFTKEY_TYP. Optional in the reference; when it is left out
    /// so is the digit count below.
    std::optional<SoftkeyType> type = SoftkeyType::Button;

    /// WZW_REMOTE_SOFTKEY_STELLEN: 1 to 9 digits for a numeric key, 0 to 30
    /// characters for an alphanumeric one. Ignored unless `type` is set.
    std::int16_t digits = 0;

    /// WZT_REMOTE_SOFTKEY_TEXT, the caption. The reference caps it at 20
    /// characters and notes that how many actually fit depends on the width of
    /// the characters and of the key, so 20 is a limit rather than a promise.
    std::string label;

    /// Send the caption at all. A telegram that only changes the attribute
    /// leaves the text alone.
    bool with_label = true;

    /// Close the block with an explicit LGX_CLOSE, as everywhere else.
    bool close_blocks = true;
};

/// WZV_REMOTE_TO_SOFTKEY (WV04): program one softkey, or all of them.
[[nodiscard]] Telegram remoteSoftkey(const SoftkeySpec& spec);

/// Attribute -1 on one key, or on every key when `number` is nullopt.
///
/// Worth having as its own call for the same reason the Terminal tab has an
/// auto-clear: what a program puts on the operator's terminal, the program has
/// to be able to take away again.
[[nodiscard]] Telegram clearSoftkey(std::optional<std::int16_t> number = std::nullopt);

/// What WZV_SOFTKEY_TO_REMOTE (WV05) carried: the operator pressed a key.
struct SoftkeyInput {
    /// WZW_REMOTE_SOFTKEY_NR of the key that was pressed.
    std::int16_t number = 0;
    /// WZW_REMOTE_SOFTKEY_TYP, echoed.
    std::int16_t type = 0;
    /// WZL_REMOTE_SOFTKEY_EINGABE, for a numeric key.
    std::optional<std::int32_t> value;
    /// WZT_REMOTE_SOFTKEY_EINGABE, for an alphanumeric one.
    std::optional<std::string> text;
};

/// Reads a WZV_SOFTKEY_TO_REMOTE out of a telegram. Nullopt for anything else.
///
/// A press may arrive as the answer to a request or on the spontaneous channel,
/// depending on how the connection was opened; this parses the telegram either
/// way and does not care which brought it.
[[nodiscard]] std::optional<SoftkeyInput> parseSoftkeyInput(const Telegram& reply);

/// `A?WV05|WW06|<number>|LX02`, or without the number when none is given.
///
/// The counterpart of `dialogResultQuery`: where a device delivers nothing
/// unasked, a key press has to be requested. `WV05` carries neither `?` nor `!`
/// in the reference, so reading it is unspecified rather than forbidden.
[[nodiscard]] Telegram softkeyPressQuery(std::optional<std::int16_t> number = std::nullopt);

/// `A?WVA6|LX02` — `WZV_GXNET_META_SOFTKEY_INFO`, 12.00 SP5.
///
/// Answers with `WZW_REMOTE_SOFTKEY_ATTR` (0 locked, 1 active) and `WZT_LABEL`
/// per softkey: whether a key exists and whether it is usable, without looking
/// at the terminal.
[[nodiscard]] Telegram softkeyInfoQuery();

// --- asking the device about itself ---------------------------------------

/// WZV_META_ERROR_TEXT (WV4A, 9.00): read the text of an error number.
///
/// The device carries the error-text resource the appendix was printed from, so
/// a code the document skips can still be looked up -- from the device, which
/// is the copy that matches its own firmware. This asks by LGW_DEBUG, the
/// internal code; the reference allows LGW_RETURN in the same slot.
[[nodiscard]] Telegram errorTextQuery(std::int32_t debug_code);

/// SRV_NET_KONF_ADDON_PSV_PCK (SV5B, 14.00): read which subfunctions travel
/// with PSV_PCK, and on which channels.
///
/// This is the "Add.data to PSV_PCK" setting of the outgoing-lines menu, the
/// one that decides whether a value written on the master reaches a secondary
/// printer. It is readable over the interface, which saves a trip to the
/// terminal and an authorisation level.
///
/// The answer is a list of `SRV_UFKENN_CHANNEL_INFO` pairs: a subfunction code
/// (LGW_UFKENN) and a channel bitmap (SRL_NET_CHANNEL_BITMAP, bit 0 internal,
/// bit 1 channel A, up to bit 11 channel K).
///
/// **The matching write, SV6B, is not built here on purpose.** It replaces the
/// whole list rather than adding to it, and the reference has a dedicated
/// locking error for getting the channel or the identifier wrong --
/// 30150, which stops labelling. A read costs nothing; a wrong write stops the
/// line.
[[nodiscard]] Telegram addonPsvPckQuery();

}  // namespace gxnet::link

#endif  // GXNET_LINK_OPERATIONS_HPP
