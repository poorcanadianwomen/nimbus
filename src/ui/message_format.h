#pragma once

#include <QtCore/QHash>
#include <functional>
#include <QtCore/QString>
#include <QtCore/QVector>

namespace nimbus {

class Store;

// Stoat's message syntax, parsed into spans the transcript can render. Pure logic:
// no widgets, no store access beyond an emoji lookup, so every rule here is testable
// without a window.
//
// The syntax, as other Stoat clients send it:
//   > quoted line, one per line, at the start of the message or of a paragraph
//   ||spoiler||
//   `inline code`, and ``` fenced blocks ```
//   :name: a server emote
//   <@id> a user mention
//
// Spans are returned sorted and non-overlapping, because the renderer walks them in
// order and a span that overlapped its neighbour would either double-paint or eat the
// text between. Where the syntax is ambiguous -- an unclosed fence, a stray `` ` `` --
// the parser resolves it by treating the marker as literal text, which is what every
// other implementation does and the only option that cannot lose a message.
enum class FormatKind {
    Quote,
    Spoiler,
    Code,
    CodeBlock,
    Emote,
    Mention,
};

struct FormatSpan {
    FormatKind kind = FormatKind::Quote;
    // Offsets into the source string, so a span can be turned into a selection, a hit
    // region or a replacement without re-scanning.
    int start = 0;
    int length = 0;
    // What the span resolves to: an emote id, or a user id for a mention. Empty for
    // the kinds that carry no target.
    QString target;

    int end() const { return start + length; }
};

// Resolves :name: to an emote id using the server's emoji. Passed in rather than held
// so the parser stays independent of the store and a test can hand it a fixed table.
using EmoteLookup = std::function<QString(const QString& name)>;

struct FormatOptions {
    EmoteLookup lookupEmote;
};

// The whole message, in one pass. Spans come back sorted by start and never overlap.
QVector<FormatSpan> parseMessageFormatting(const QString& content, const FormatOptions& options);

// Convenience over the store: an emoji name is only a span if the server actually has
// it, so ":notanemoji:" stays the literal text a person typed.
QVector<FormatSpan> parseMessageFormatting(const QString& content, const Store* store,
                                            const QString& serverId);

// The visible text of the message with the syntax markers removed -- the emote and
// mention targets substituted, so a notification or a search result reads as a
// sentence rather than as ":name: <@id>". Spoiler contents are kept: hiding them is a
// rendering decision, and a search that could not find a word because it was inside a
// spoiler would be a bug report.
QString plainTextOf(const QString& content, const QVector<FormatSpan>& spans);

// The emotes a message uses, in order, deduplicated. What the reaction list offers when
// you have reacted already, and what a "jump to emote" would need.
QVector<QString> emotesIn(const QString& content, const QVector<FormatSpan>& spans);

// Splits a partial word the user is typing for autocomplete: given "hello @ma" it
// returns the range of "ma" and the prefix "@". Returns an empty range when the caret
// is not in a trigger position, so a caller can decide what to show.
struct TriggerMatch {
    bool matched = false;
    int start = 0;
    int length = 0;
    QString prefix;   // "@" or ":"
    QString term;     // what has been typed after the trigger
};

TriggerMatch triggerAt(const QString& text, int caret);

} // namespace nimbus
