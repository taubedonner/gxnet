// SPDX-License-Identifier: MIT
#include "gxnet/link/operations.hpp"

#include <algorithm>

#include "gxnet/registry.hpp"

namespace gxnet::link {
namespace {

// Resolved at compile time, and by symbolic name rather than by four
// characters: a mistyped code that happens to exist addresses the wrong
// subfunction and compiles happily.
constexpr Token kDataset = knownToken("XCV_DBTAB_DATASET").token;  // XV00
constexpr Token kPlu = knownToken("GGL_PLUNR").token;              // GL19
constexpr Token kCustomer = knownToken("GGL_KDNR").token;          // GL1A
constexpr Token kReturn = knownToken("LGW_RETURN").token;          // LW01
constexpr Token kGetBuffer = knownToken("MDW_GET_BUFF").token;     // MW06

constexpr Token kSddStart = knownToken("WZV_SDD_START").token;          // WV60
constexpr Token kSddData = knownToken("WZV_SDD_DATA").token;            // WV62
constexpr Token kSddResult = knownToken("WZV_SDD_RESULT").token;        // WV63
constexpr Token kHandle = knownToken("WZW_HDL").token;                  // WW60
constexpr Token kSddType = knownToken("WZW_SDD_TYP").token;             // WW61
constexpr Token kElemType = knownToken("WZW_SDD_ELEM_TYP").token;       // WW63
constexpr Token kSddId = knownToken("WZW_SDD_ID").token;                // WW64
constexpr Token kDisplayAttr = knownToken("WZW_DISPLAY_ATTRIB").token;  // WW65
constexpr Token kExit = knownToken("WZW_EXIT").token;                   // WW68
constexpr Token kElemActive = knownToken("WZW_SDD_ELEM_ACTIVE").token;  // WW69
constexpr Token kHeadline = knownToken("WZT_HEADLINE").token;           // WT60
constexpr Token kLabel = knownToken("WZT_LABEL").token;                 // WT62

constexpr Token kSoftkeyTo = knownToken("WZV_REMOTE_TO_SOFTKEY").token;       // WV04
constexpr Token kSoftkeyFrom = knownToken("WZV_SOFTKEY_TO_REMOTE").token;     // WV05
constexpr Token kSoftkeyNr = knownToken("WZW_REMOTE_SOFTKEY_NR").token;       // WW06
constexpr Token kSoftkeyTyp = knownToken("WZW_REMOTE_SOFTKEY_TYP").token;     // WW07
constexpr Token kSoftkeyAttr = knownToken("WZW_REMOTE_SOFTKEY_ATTR").token;   // WW08
constexpr Token kSoftkeyDigits = knownToken("WZW_REMOTE_SOFTKEY_STELLEN").token;  // WW09
constexpr Token kSoftkeyText = knownToken("WZT_REMOTE_SOFTKEY_TEXT").token;       // WT00
constexpr Token kSoftkeyValue = knownToken("WZL_REMOTE_SOFTKEY_EINGABE").token;   // WL0A
constexpr Token kSoftkeyEntry = knownToken("WZT_REMOTE_SOFTKEY_EINGABE").token;   // WT0A
constexpr Token kSoftkeyInfo = knownToken("WZV_GXNET_META_SOFTKEY_INFO").token;   // WVA6

constexpr Token kMetaErrorText = knownToken("WZV_META_ERROR_TEXT").token;        // WV4A
constexpr Token kDebug = knownToken("LGW_DEBUG").token;                          // LW03
constexpr Token kAddonPsvPck = knownToken("SRV_NET_KONF_ADDON_PSV_PCK").token;   // SV5B

/// The 0x62 slot of the WZW group, which the coding table leaves unnamed.
///
/// A WZV_SDD_START carrying it is refused with LGW_RETURN 1 and an internal code
/// in the input-tool range; the same telegram without it is accepted. So
/// `DialogSpec` leaves it out by default and keeps a switch for it.
///
/// Spelled as a literal token rather than through knownToken(), which only
/// accepts names the registry carries.
constexpr Token kElemCount = "WW62"_tok;

/// WZW_SDD_ELEM_TYP, tied to the dialog kind by the reference's own table of
/// permitted combinations: type 8 takes display elements, type 7 selections.
constexpr std::int16_t kElemDisplay = 5;
constexpr std::int16_t kElemSelection = 4;

/// Clears the explicit LGX_CLOSE from every block, so the telegram ends with
/// its blocks still open. Both forms are legal and the reference's own worked
/// examples use both.
void openEnded(std::vector<Node>& nodes) {
    for (Node& node : nodes) {
        if (!node.children.empty() || node.token.type() == DataType::Block) {
            node.explicit_close = false;
            openEnded(node.children);
        }
    }
}

}  // namespace

std::optional<Value> valueOf(const Telegram& telegram, Token token) {
    const Record* record = telegram.records.empty() ? nullptr : &telegram.records.front();
    std::size_t field = 0;
    std::optional<Value> found;

    forEachNode(telegram.header.nodes, [&](const Node& node) {
        if (node.token.arity() == 0) return;
        const std::size_t index = field++;
        if (found || node.token != token) return;

        if (!isEmpty(node.value)) {
            found = node.value;
        } else if (record != nullptr && index < record->size()) {
            found = (*record)[index];
        }
    });
    return found;
}

std::optional<Value> valueOf(const Telegram& telegram, const Node& scope, Token token) {
    const Record* record = telegram.records.empty() ? nullptr : &telegram.records.front();
    std::size_t field = 0;
    std::optional<Value> found;

    // Walked by hand rather than with forEachNode, because the field counter has
    // to keep running across the whole header while only the subtree under
    // `scope` is eligible to match. A filtered walk would count the wrong
    // fields and read the wrong values out of the record.
    const auto visit = [&](auto&& self, const std::vector<Node>& nodes, bool inside) -> void {
        for (const Node& node : nodes) {
            const bool here = inside || &node == &scope;
            if (node.token.arity() > 0) {
                const std::size_t index = field++;
                if (!found && here && node.token == token) {
                    if (!isEmpty(node.value)) {
                        found = node.value;
                    } else if (record != nullptr && index < record->size()) {
                        found = (*record)[index];
                    }
                }
            }
            if (!node.children.empty()) self(self, node.children, here);
        }
    };
    visit(visit, telegram.header.nodes, false);
    return found;
}

namespace {

/// Numeric value of anything that carries one, so a decoder can be written once
/// instead of per width.
std::optional<std::int32_t> asNumber(const Value& value) {
    if (const auto* w = std::get_if<std::int16_t>(&value)) return *w;
    if (const auto* l = std::get_if<std::int32_t>(&value)) return *l;
    return std::nullopt;
}

std::string joinWith(const std::vector<std::string>& parts, const char* separator) {
    std::string out;
    for (const std::string& part : parts) {
        if (!out.empty()) out += separator;
        out += part;
    }
    return out;
}

/// SRL_NET_CHANNEL_BITMAP: bit 0 internal, bit 1 = A, ... bit 11 = K.
std::string channelBitmapText(std::int32_t bitmap) {
    if (bitmap == 0) return "no channel";
    std::vector<std::string> parts;
    for (int bit = 0; bit < 12; ++bit) {
        if ((bitmap >> bit & 1) == 0) continue;
        parts.push_back(bit == 0 ? std::string("intern")
                                 : std::string(1, static_cast<char>('A' + bit - 1)));
    }
    // Bits above 11 are not in the reference. Saying so beats dropping them.
    if ((bitmap & ~0xFFF) != 0) parts.push_back("and bits outside 0-11");
    return joinWith(parts, ", ");
}

/// LGW_UFKENN is a token's own 16-bit code: class in the high byte, index in
/// the low, class = (group << 4) | type.
std::string ufkennText(std::int32_t code) {
    const auto raw = static_cast<std::uint16_t>(code);
    const auto decoded =
        Token::fromClassCode(static_cast<std::uint8_t>(raw >> 8), static_cast<std::uint8_t>(raw & 0xFF));
    if (!decoded) return {};

    const Token token = *decoded;
    std::string out = token.str();
    if (const auto name = tokenName(token)) {
        out += " ";
        out += *name;
    }
    return out;
}

/// PSL_PCK_ERR_FLAGS: 32 bits, one cause each. Only the named ones are listed;
/// the reference leaves 22 and 26-31 unassigned.
std::string packageErrorFlagsText(std::int32_t flags) {
    static constexpr std::pair<int, const char*> kBits[] = {
        {0, "internal error"},
        {1, "label missing or faulty"},
        {2, "could not be weighed"},
        {3, "weight out of range"},
        {4, "metal detected"},
        {5, "slave labeller acknowledged negatively"},
        {6, "package data could not be sent"},
        {7, "user formula returned an error"},
        {8, "data changed at an impermissible moment"},
        {9, "switched to transport mode"},
        {10, "ejection triggered by the user"},
        {11, "package too long or too short"},
        {12, "separation error"},
        {13, "code read-back returned an error"},
        {14, "statistics report running"},
        {15, "faulty but not cancelled, total already sent"},
        {16, "marked faulty by an external signal"},
        {17, "scanner too slow"},
        {18, "RFID write error"},
        {19, "TTI error"},
        {20, "logo missing or faulty"},
        {21, "could not be weighed during teach"},
        {23, "empty package"},
        {24, "no unique data available"},
        {25, "test package in scale-check mode"},
    };

    if (flags == 0) return "no errors";
    std::vector<std::string> parts;
    for (const auto& [bit, text] : kBits) {
        if (flags >> bit & 1) parts.push_back(std::string("bit ") + std::to_string(bit) + " " + text);
    }
    if (parts.empty()) return "set bits, none of them named in the reference";
    return joinWith(parts, "; ");
}

/// Straight value-to-meaning tables, transcribed from the coding table.
struct Choice {
    std::int32_t value;
    const char* text;
};

std::string choiceText(std::span<const Choice> choices, std::int32_t value) {
    for (const Choice& choice : choices) {
        if (choice.value == value) return choice.text;
    }
    return {};
}

}  // namespace

std::string annotateValue(Token token, const Value& value) {
    const auto number = asNumber(value);
    if (!number) return {};
    const std::int32_t n = *number;

    if (token == "SL8C"_tok) return channelBitmapText(n);
    if (token == "LW02"_tok) return ufkennText(n);
    if (token == "PL13"_tok) return packageErrorFlagsText(n);

    if (token == "LW01"_tok) {
        const auto text = returnCodeText(static_cast<std::int16_t>(n));
        return std::string(text);
    }
    if (token == "LW03"_tok) return internalErrorText(n);

    if (token == "WW0C"_tok) {
        // A word whose low byte is an ASCII digit or letter. '9' and 'T' are
        // both TERMINAL: the reference says a device reports '9' from 10.00 and
        // 'T' before it, and levels '0'-'6' report themselves.
        const char level = static_cast<char>(n & 0xFF);
        std::string out = "level '";
        out += level;
        out += "'";
        if (level == '9' || level == 'T') out += ", TERMINAL";
        else if (level == '0') out += ", standby";
        else if (level == '1') out += ", checking settings";
        else if (level == '2') out += ", article change";
        return out;
    }

    static constexpr Choice kExitChoices[] = {{0, "input OK"}, {1, "cancelled with HOME"}};
    static constexpr Choice kSddTyp[] = {
        {1, "single numeric"}, {2, "double numeric"},  {3, "alphanumeric"},
        {4, "hidden alphanumeric"}, {5, "date"},       {6, "time"},
        {7, "selection (scroll menu)"}, {8, "display with confirmation"}, {9, "display only"}};
    static constexpr Choice kElemTyp[] = {
        {1, "numeric"}, {2, "alphanumeric"}, {3, "calendar"}, {4, "selection"}, {5, "display"}};
    static constexpr Choice kQuitMode[] = {{0, "single acknowledgement"}, {1, "double acknowledgement"}};
    static constexpr Choice kTrig2[] = {{0, "no package synchronisation"},
                                        {1, "released by a PLU change or a command"},
                                        {2, "released only by XCW_PCK_SYNC"}};
    static constexpr Choice kUnique[] = {{0, "unique-data intake off"}, {1, "unique-data intake on"}};
    static constexpr Choice kSbus[] = {{0, "Profibus"}, {1, "Ethernet"}, {2, "Profibus and Ethernet"}};
    static constexpr Choice kSendkanal[] = {{0, "will not send unasked"}, {1, "may send unasked"}};
    static constexpr Choice kLicenceVal[] = {{0, "no hardware id"}, {1, "no licence"}, {2, "modular licence present"},
                                             {3, "demo"},          {4, "trial"},      {5, "disabled"}};
    static constexpr Choice kLabelerState[] = {
        {1, "active"}, {2, "passive"}, {3, "standby"}, {4, "error"}};

    if (token == "WW68"_tok) return choiceText(kExitChoices, n);
    if (token == "WW61"_tok) return choiceText(kSddTyp, n);
    if (token == "WW63"_tok) return choiceText(kElemTyp, n);
    if (token == "GW58"_tok) return choiceText(kQuitMode, n);
    if (token == "AW75"_tok) return choiceText(kTrig2, n);
    if (token == "GW7D"_tok) return choiceText(kUnique, n);
    if (token == "GW38"_tok) return choiceText(kSbus, n);
    if (token == "WW2A"_tok) return choiceText(kLicenceVal, n);
    if (token == "SW98"_tok) return choiceText(kLabelerState, n);
    if (token == "SW85"_tok) return n != 0 ? "unicode device" : "codepage device";

    // GGW_SENDKANAL_A..E and J, K -- the switch exists for those channels only.
    for (Token gate : {"GWBF"_tok, "GWC0"_tok, "GWC1"_tok, "GWC2"_tok, "GWC3"_tok, "GW74"_tok, "GW75"_tok}) {
        if (token == gate) return choiceText(kSendkanal, n);
    }

    if (token == "GL38"_tok && n == 0) return "system bus not enabled on this device";

    return {};
}

std::string_view returnCodeText(std::int32_t code) {
    // LGW_RETURN, transcribed from the reference. Not every code can arise from
    // the commands this program sends; the whole table is here because the
    // point of showing a return code at all is to answer "what does 2650 mean"
    // without a manual on the desk.
    //
    // Transcribed from the **German** edition, and that is not a preference.
    // The English one stops at 2658 and omits codes 14 to 24 altogether -- the
    // same table, an older revision. Among the codes only German has are the
    // ones that say why an input tool refused something (17, 19, 2700) and the
    // one for a file that is not there (2157), which is exactly what an FTP
    // operation needs to be able to say.
    switch (code) {
        case 0: return "okay";
        case 1: return "internal error";
        case 2: return "access denied";
        case 3: return "conversion refused";
        case 4: return "third-party command: the device has no such subfunction";
        case 5: return "data format error";
        case 6: return "value undershot";
        case 7: return "value exceeded";
        case 8: return "warning: unit changed";
        case 9: return "error: set unit does not match the device configuration";
        case 10: return "warning: standardization changed";
        case 11:
        case 12: return "reserved";
        case 13: return "not enough memory";
        case 14: return "licence missing";
        case 15: return "Ethernet licence missing";
        case 16: return "service command failed";
        case 17: return "a mandatory parameter is missing";
        case 18: return "terminal refused: the last command is still being processed";
        case 19: return "terminal refused: an input tool is open";
        case 20: return "terminal refused: another operating unit holds the device exclusively";
        case 21: return "too many GxNet messages in flight over BMQ";
        case 22: return "bad character in a string: not valid UTF-8";
        case 23: return "data not available";
        case 24: return "value not permitted";
        case 150: return "labeling disabled";
        case 151: return "incorrect mode level";
        case 152: return "previous internal label not ready";
        case 153: return "total reached";
        case 154: return "previous label was not acknowledged by the downstream printer";
        case 155: return "conditional printing was not executed";
        case 156: return "the queue for remote print orders is not empty";
        case 157: return "the print or processing order cannot be carried out";
        case 158: return "the order was cancelled";
        case 159: return "label preparation calculation failed";
        case 160: return "the labeller is paused";
        case 161: return "the Windows printer handle does not fit";
        case 251: return "scale function refused: the automatic machine is running";
        case 850: return "general scale error during a calibration command";
        case 851: return "scale not connected, or scale type error";
        case 852: return "scale not stationary during a gross weight request";
        case 853: return "scale outside range";
        case 1000: return "error switching on PLU";
        case 1001: return "forwarded error from a downstream device";
        case 1002: return "data record incomplete";
        case 1250: return "cancellation could not be executed";
        case 2002: return "code editor access denied";
        case 2050: return "code structure data error";
        case 2151: return "internal error";
        case 2152: return "no access to the memory card";
        case 2154: return "communication error";
        case 2155: return "memory card is full";
        case 2156: return "warning: memory card almost full";
        case 2157: return "file does not exist";
        case 2250: return "connection lost";
        case 2251: return "timeout, remote currently occupied";
        case 2252: return "bridge has not made a connection";
        case 2253: return "bridge transmission error";
        case 2254: return "bridge overloaded";
        case 2255: return "response is missing";
        case 2256: return "count incorrect";
        case 2350: return "incorrect data index or addressing";
        case 2351: return "the total preselection value does not match the preselection type";
        case 2450: return "no statistics data available";
        case 2451: return "interpreter is occupied";
        case 2452: return "setup was not completely saved";
        case 2453: return "the statistics data this command requires to be sent first has not finished sending";
        case 2454: return "configuration error for XCV_SUMINFO";
        case 2455: return "no operation possible";
        case 2650: return "data record not available: no such PLU in the database";
        case 2651: return "data record cannot be deleted";
        case 2652: return "database key attribute incorrect";
        case 2653: return "database attribute incorrect";
        case 2654: return "incorrect database command parameter";
        case 2655: return "database error in dimension related values";
        case 2656: return "dimension related attributes have been converted";
        case 2657: return "an unknown attribute has been used in a table";
        case 2658: return "the device has to be switched off and on before this command";
        case 2659: return "warning: key type numeric/alphanumeric does not match, converting";
        case 2660: return "the teach data record is already on the internal CF card";
        case 2661: return "access not permitted";
        case 2662: return "access not permitted: the database is busy deleting tables";
        case 2663: return "text contains too many characters";
        case 2665: return "database server not reachable";
        // Input tools (EWZ), the family the SDD dialogs belong to. Present only
        // in the German edition, and worth having for exactly that reason.
        case 2700: return "input is already occupied";
        case 2722: return "timeout in the input-tool task";
        case 2750: return "timeout in the input-tool database task";
        case 2751: return "timeout in the input-tool remote task";
        case 3350: return "macro is not executable";
        // Label and resource errors. 3950 is the one to recognise: a telegram
        // the device cannot make structural sense of says so here, with its own
        // code, rather than as a generic internal error.
        case 3950: return "bad telegram length or telegram identification";
        case 3951: return "error in the layout or its parameters";
        case 3952: return "error in a field or a field parameter";
        case 3953: return "error building a resource";
        case 3954: return "error installing a TrueType font";
        case 3955: return "TrueType font error: the automatic machine is running";
        case 3956: return "error deleting a TrueType font";
        default: return {};
    }
}

std::optional<std::int16_t> returnCodeOf(const Telegram& reply) {
    if (const auto value = valueOf(reply, kReturn)) {
        if (const auto* word = std::get_if<std::int16_t>(&*value)) return *word;
    }
    return std::nullopt;
}

std::string internalErrorText(std::int32_t code) {
    if (code <= 0) return {};

    // The offsets, from the reference's own "Interne Codierung" table -- the
    // internal coding of the general manager and component errors. This is what
    // makes a code the appendix skipped readable at all.
    struct Offset {
        std::int32_t at;
        const char* what;
    };
    static constexpr Offset kOffsets[] = {
        {1, "overflow"},
        {2, "underflow"},
        // Not in that table, but every module in the appendix that prints a
        // +3 prints it as a data error, without exception.
        {3, "data error"},
        {4, "initialisation error"},
        {5, "function not available"},
        {6, "fatal manager error"},
        {7, "invalid manager id"},
        {8, "invalid semaphore handle"},
        {9, "invalid event handle"},
        {10, "invalid handle"},
        {11, "invalid queue id"},
        {12, "invalid resource id"},
        {13, "invalid task id"},
        {14, "invalid partition id"},
        {15, "memory manager error"},
        {16, "component bus error"},
        {17, "system bus error"},
        {18, "invalid message"},
        {19, "invalid parameter"},
        {20, "driver error"},
        {21, "task error"},
        {22, "timeout"},
        {47, "i/o busy"},
        {48, "i/o cancelled"},
        {49, "general error"},
    };

    // Only the groups whose printed entries actually follow the scheme. Several
    // others in the appendix start at a base that is not a round hundred and
    // number their errors freely from there; applying the offsets to those
    // would produce confident nonsense.
    struct Group {
        std::int32_t base;
        const char* name;
    };
    static constexpr Group kGroups[] = {
        {15100, "sequence control"},   {15200, "machine control"},
        {15300, "display"},            {15600, "printer"},
        {15700, "label preparation"},  {15800, "weight processing"},
        {15900, "initialisation"},     {16000, "operator input"},
        {17000, "code editor"},        {17200, "connection layer"},
        {17400, "remote interpreter"}, {17700, "input tools"},
        {21900, "operating data"},     {22200, "GxNet"},
    };

    for (const Group& group : kGroups) {
        const std::int32_t offset = code - group.base;
        if (offset < 1 || offset > 49) continue;
        for (const Offset& known : kOffsets) {
            if (known.at != offset) continue;
            return std::string(group.name) + ": " + known.what;
        }
        return {};
    }
    return {};
}

std::string readPlaceholderData(const Header& header) {
    std::string out;
    bool needed = false;

    for (const Token& token : header.payloadTokens()) {
        if (!out.empty()) out += '|';
        const auto type = token.type();
        if (type == DataType::Word || type == DataType::Long) {
            out += '0';
            needed = true;
        }
    }
    return needed ? out : std::string();
}

Telegram bufferPoll(std::int16_t timeout_ms) {
    Builder builder(Family::Automatic, Access::Read);
    builder.word(kGetBuffer.str(), timeout_ms);
    return builder.build();
}

Telegram pluChange(std::int32_t plu, std::optional<std::int32_t> customer) {
    Builder builder(Family::Automatic, Access::Write);
    builder.block(kDataset.str());
    builder.long_(kPlu.str(), plu);
    if (customer) builder.long_(kCustomer.str(), *customer);
    builder.end();
    return builder.build();
}

std::int16_t pairedElementType(std::int16_t dialog_type) {
    // Straight from the coding table for WZW_SDD_ELEM_TYP, which prints the
    // permitted combinations rather than leaving them to be guessed.
    switch (dialog_type) {
        case 1:
        case 2: return 1;  // numeric
        case 3:
        case 4: return 2;  // alphanumeric
        case 5:
        case 6: return 3;  // calendar
        case 7: return kElemSelection;
        case 8:
        case 9: return kElemDisplay;
        default: return 0;
    }
}

Telegram dialog(const DialogSpec& spec) {
    const auto count = static_cast<std::int16_t>(spec.elements.size());
    std::int16_t element_type = spec.element_type;
    if (element_type == 0) element_type = pairedElementType(spec.type);

    std::int16_t active = spec.active;
    if (active < 1 || active > count) active = 1;

    Builder builder(Family::Automatic, Access::Write);
    builder.block(kSddStart.str());
    builder.word(kHandle.str(), spec.handle);
    builder.word(kSddType.str(), spec.type);
    if (spec.with_element_count) builder.word(kElemCount.str(), count);
    if (spec.with_active) builder.word(kElemActive.str(), active);
    if (spec.with_headline) builder.text(kHeadline.str(), spec.headline);

    for (const DialogItem& item : spec.elements) {
        builder.block(kSddData.str());
        builder.word(kElemType.str(), element_type);
        builder.text(kLabel.str(), item.label);
        // The element-type-dependent tail. A selection is identified by its id
        // and a display carries an attribute; sending the other one addresses a
        // field the device is not expecting there.
        if (element_type == kElemSelection) {
            builder.word(kSddId.str(), item.id);
        } else if (element_type == kElemDisplay) {
            builder.word(kDisplayAttr.str(), item.attrib);
        }
        builder.end();
    }

    builder.end();

    Telegram telegram = builder.build();
    if (!spec.close_blocks) openEnded(telegram.header.nodes);
    return telegram;
}

Telegram confirmDialog(std::int16_t handle, std::string headline, std::string message, bool with_element_count) {
    DialogSpec spec;
    spec.type = static_cast<std::int16_t>(DialogKind::Confirm);
    spec.handle = handle;
    spec.headline = std::move(headline);
    spec.elements.push_back({std::move(message), 0, 0});
    spec.with_element_count = with_element_count;
    return dialog(spec);
}

Telegram selectionDialog(std::int16_t handle, std::string headline, std::span<const DialogItem> items,
                         std::int16_t active, bool with_element_count) {
    DialogSpec spec;
    spec.type = static_cast<std::int16_t>(DialogKind::Selection);
    spec.handle = handle;
    spec.headline = std::move(headline);
    spec.elements.assign(items.begin(), items.end());
    spec.active = active;
    spec.with_active = true;
    spec.with_element_count = with_element_count;
    return dialog(spec);
}

std::optional<DialogResult> parseDialogResult(const Telegram& reply) {
    bool present = false;
    forEachNode(reply.header.nodes, [&](const Node& node) {
        if (node.token == kSddResult) present = true;
    });
    if (!present) return std::nullopt;

    DialogResult result;
    if (const auto handle = valueOf(reply, kHandle)) {
        if (const auto* word = std::get_if<std::int16_t>(&*handle)) {
            result.handle = *word;
        }
    }
    // WZW_EXIT is mandatory in the telegram; if it is missing the answer is not
    // one we understand, and reporting "confirmed" would be a guess with a
    // running line behind it.
    const auto exit = valueOf(reply, kExit);
    if (!exit) return std::nullopt;
    const auto* exit_word = std::get_if<std::int16_t>(&*exit);
    if (exit_word == nullptr) return std::nullopt;
    result.exit = *exit_word;

    if (const auto id = valueOf(reply, kSddId)) {
        if (const auto* word = std::get_if<std::int16_t>(&*id)) result.id = *word;
    }
    if (const auto label = valueOf(reply, kLabel)) {
        if (const auto* text = std::get_if<std::string>(&*label)) {
            result.label = *text;
        }
    }
    return result;
}

Telegram remoteSoftkey(const SoftkeySpec& spec) {
    Builder builder(Family::Automatic, Access::Write);
    builder.block(kSoftkeyTo.str());

    // Field order is the reference's, and it is not negotiable: the number
    // first when it is sent at all, then the attribute, then the optional
    // type-and-digits pair, then the caption.
    if (spec.number) builder.word(kSoftkeyNr.str(), *spec.number);
    builder.word(kSoftkeyAttr.str(), spec.attribute);
    if (spec.type) {
        builder.word(kSoftkeyTyp.str(), static_cast<std::int16_t>(*spec.type));
        builder.word(kSoftkeyDigits.str(), spec.digits);
    }
    if (spec.with_label) builder.text(kSoftkeyText.str(), spec.label);

    builder.end();

    Telegram telegram = builder.build();
    if (!spec.close_blocks) openEnded(telegram.header.nodes);
    return telegram;
}

Telegram clearSoftkey(std::optional<std::int16_t> number) {
    SoftkeySpec spec;
    spec.number = number;
    spec.attribute = -1;
    // Deleting a key says nothing about what type it was or what it said, and
    // the reference makes both optional. Sending them anyway would be asking
    // the device to set properties on something being removed.
    spec.type = std::nullopt;
    spec.with_label = false;
    return remoteSoftkey(spec);
}

std::optional<SoftkeyInput> parseSoftkeyInput(const Telegram& reply) {
    bool present = false;
    forEachNode(reply.header.nodes, [&](const Node& node) {
        if (node.token == kSoftkeyFrom) present = true;
    });
    if (!present) return std::nullopt;

    // The key number is what identifies the press, so an answer without one is
    // not an answer we can attribute to a question.
    const auto number = valueOf(reply, kSoftkeyNr);
    if (!number) return std::nullopt;
    const auto* number_word = std::get_if<std::int16_t>(&*number);
    if (number_word == nullptr) return std::nullopt;

    SoftkeyInput input;
    input.number = *number_word;
    if (const auto type = valueOf(reply, kSoftkeyTyp)) {
        if (const auto* word = std::get_if<std::int16_t>(&*type)) input.type = *word;
    }
    if (const auto value = valueOf(reply, kSoftkeyValue)) {
        if (const auto* number_value = std::get_if<std::int32_t>(&*value)) input.value = *number_value;
    }
    if (const auto text = valueOf(reply, kSoftkeyEntry)) {
        if (const auto* string = std::get_if<std::string>(&*text)) input.text = *string;
    }
    return input;
}

Telegram errorTextQuery(std::int32_t debug_code) {
    Builder builder(Family::Automatic, Access::Read);
    builder.block(kMetaErrorText.str());
    builder.word(kDebug.str(), static_cast<std::int16_t>(debug_code));
    builder.end();
    return builder.build();
}

Telegram softkeyPressQuery(std::optional<std::int16_t> number) {
    Builder builder(Family::Automatic, Access::Read);
    builder.block(kSoftkeyFrom.str());
    if (number) builder.word(kSoftkeyNr.str(), *number);
    builder.end();
    return builder.build();
}

Telegram softkeyInfoQuery() {
    Builder builder(Family::Automatic, Access::Read);
    builder.block(kSoftkeyInfo.str());
    builder.end();
    return builder.build();
}

Telegram dialogResultQuery(std::int16_t handle) {
    Builder builder(Family::Automatic, Access::Read);
    builder.block(kSddResult.str());
    builder.word(kHandle.str(), handle);
    builder.end();
    return builder.build();
}

Telegram dialogExitQuery() {
    Builder builder(Family::Automatic, Access::Read);
    builder.word(kExit.str(), 0);
    return builder.build();
}

Telegram addonPsvPckQuery() {
    Builder builder(Family::Automatic, Access::Read);
    builder.block(kAddonPsvPck.str());
    builder.end();
    return builder.build();
}

}  // namespace gxnet::link
