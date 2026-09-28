#include "message_format.h"

#include "core/store.h"

#include <QtCore/QSet>
#include <QtCore/QStringList>

namespace nimbus {

namespace {

// Longest marker first, so ``` is not read as an empty inline code followed by a
// stray backtick. Ordered, not sorted, so the intent is readable.
constexpr const char* kFence = "```";

bool isWordChar(QChar c) {
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

void sortAndDropOverlaps(QVector<FormatSpan>& spans) {
    std::stable_sort(spans.begin(), spans.end(),
                     [](const FormatSpan& a, const FormatSpan& b) { return a.start < b.start; });

    // A span that starts inside the previous one is dropped. The parser only ever
    // pushes non-overlapping spans, so reaching here means two rules disagreed -- and
    // dropping the later one is the safe resolution, because the earlier rule already
    // accounts for the text.
    QVector<FormatSpan> kept;
    int consumed = -1;
    for (const FormatSpan& span : spans) {
        if (span.start < consumed) continue;
        kept.append(span);
        consumed = span.end();
    }
    spans = kept;
}

} // namespace

QVector<FormatSpan> parseMessageFormatting(const QString& content, const FormatOptions& options) {
    QVector<FormatSpan> spans;
    const int length = content.size();
    const QString fence = QString::fromLatin1(kFence);

    int i = 0;
    int quoteRunStart = -1;

    // A quote is a run of lines that each begin with "> ". It is one span covering the
    // whole run rather than one per line, so the renderer can indent the block once.
    const auto endQuote = [&] {
        if (quoteRunStart >= 0) {
            spans.append({FormatKind::Quote, quoteRunStart, i - quoteRunStart, {}});
            quoteRunStart = -1;
        }
    };

    while (i < length) {
        // --- block quote ---
        if (content.at(i) == QLatin1Char('\n')) {
            // A line starting with "> " opens or continues a quote.
            const int lineStart = i + 1;
            if (lineStart + 1 < length && content.at(lineStart) == QLatin1Char('>') &&
                content.at(lineStart + 1) == QLatin1Char(' ')) {
                if (quoteRunStart < 0) quoteRunStart = lineStart;
                i = lineStart + 2;
                continue;
            }
            endQuote();
            ++i;
            continue;
        }
        // A quote may also open on the very first line.
        if (i == 0 && length > 1 && content.at(0) == QLatin1Char('>') &&
            content.at(1) == QLatin1Char(' ')) {
            quoteRunStart = 0;
            i = 2;
            continue;
        }

        // --- fenced code block ---
        if (content.mid(i, 3) == fence) {
            const int openEnd = i + 3;
            // A newline straight after the opening fence is the marker, not content.
            int bodyStart = openEnd;
            if (bodyStart < length && content.at(bodyStart) == QLatin1Char('\n')) ++bodyStart;

            // The closing fence has to start a line. Without that, a fence written
            // inside the block -- which is ordinary in a code sample -- ends it early
            // and the rest of the block is parsed as prose.
            int close = -1;
            for (int at = bodyStart; at >= 0 && at < length; at = content.indexOf(fence, at + 1)) {
                const bool atLineStart = at == 0 || content.at(at - 1) == QLatin1Char('\n');
                const bool lineEnds = at + 3 >= length ||
                                     content.at(at + 3) == QLatin1Char('\n') ||
                                     content.at(at + 3) == QLatin1Char('\r');
                if (atLineStart && lineEnds) {
                    close = at;
                    break;
                }
            }
            if (close >= 0) {
                int bodyEnd = close;
                // Trim the newline that belongs to the closing fence.
                if (bodyEnd > bodyStart && content.at(bodyEnd - 1) == QLatin1Char('\n')) --bodyEnd;
                spans.append({FormatKind::CodeBlock, i, close + 3 - i, {}});
                endQuote();
                i = close + 3;
                continue;
            }
            // Unclosed: the rest of the message is the block, which is what a person
            // who typed it meant, and better than showing three backticks.
            spans.append({FormatKind::CodeBlock, bodyStart, length - bodyStart, {}});
            endQuote();
            i = length;
            continue;
        }

        const QChar c = content.at(i);

        // --- inline code ---
        if (c == QLatin1Char('`')) {
            const int close = content.indexOf(QLatin1Char('`'), i + 1);
            // An empty pair is a literal backtick pair, and a newline inside means the
            // author started inline code and gave up; both fall through as text.
            const bool usable = close > i + 1 && !content.mid(i, close - i).contains(QLatin1Char('\n'));
            if (usable) {
                spans.append({FormatKind::Code, i, close - i + 1, {}});
                endQuote();
                i = close + 1;
                continue;
            }
            ++i;
            continue;
        }

        // --- spoiler ---
        if (content.mid(i, 2) == QLatin1String("||")) {
            const int close = content.indexOf(QLatin1String("||"), i + 2);
            // Spoilers may not cross a line: a message that opens one and never closes
            // it would otherwise hide everything after it.
            if (close > i + 2) {
                const bool singleLine = !content.mid(i, close + 2 - i).contains(QLatin1Char('\n'));
                if (singleLine) {
                    spans.append({FormatKind::Spoiler, i, close - i + 2, {}});
                    endQuote();
                    i = close + 2;
                    continue;
                }
            }
            i += 2;
            continue;
        }

        // --- emote ---
        if (c == QLatin1Char(':')) {
            int j = i + 1;
            while (j < length && (isWordChar(content.at(j)) || content.at(j) == QLatin1Char('-'))) {
                ++j;
            }
            if (j > i + 1 && j < length && content.at(j) == QLatin1Char(':')) {
                const QString name = content.mid(i + 1, j - i - 1);
                QString id;
                if (options.lookupEmote) id = options.lookupEmote(name);
                if (!id.isEmpty()) {
                    spans.append({FormatKind::Emote, i, j - i + 1, id});
                    endQuote();
                    i = j + 1;
                    continue;
                }
            }
            ++i;
            continue;
        }

        // --- mention ---
        if (content.mid(i, 2) == QLatin1String("<@")) {
            const int close = content.indexOf(QLatin1Char('>'), i + 2);
            if (close > i + 2) {
                const QString id = content.mid(i + 2, close - i - 2);
                const bool looksLikeId = id.size() >= 20 && isWordChar(id.at(0));
                if (looksLikeId) {
                    spans.append({FormatKind::Mention, i, close - i + 1, id});
                    endQuote();
                    i = close + 1;
                    continue;
                }
            }
            ++i;
            continue;
        }

        // Any other character inside a quote keeps the run open.
        ++i;
    }
    endQuote();

    sortAndDropOverlaps(spans);
    return spans;
}

QVector<FormatSpan> parseMessageFormatting(const QString& content, const Store* store,
                                            const QString& serverId) {
    FormatOptions options;
    if (store) {
        // const store, so this is the const overload. A store that is not const
        // here is a caller mistake, not something to copy a hundred emotes to work around.
        const QList<Emoji> available = const_cast<Store*>(store)->emojisForServer(serverId);
        // A snapshot rather than a lambda that re-queries: parsing walks the message
        // once per emote candidate, and a hash lookup per candidate is fine, but a
        // sorted list also means the longest name can win.
        QHash<QString, QString> byName;
        for (const Emoji& emoji : available) byName.insert(emoji.name, emoji.id);
        options.lookupEmote = [byName](const QString& name) { return byName.value(name); };
    }
    return parseMessageFormatting(content, options);
}

QString plainTextOf(const QString& content, const QVector<FormatSpan>& spans) {
    // The delimiters a span is wrapped in. Spans are inclusive of their markers, so
    // they have to come off here rather than being gaps between spans -- which is how
    // the backticks and pipes survived the first version of this.
    const auto delimiters = [](FormatKind kind, int* pre, int* post) {
        switch (kind) {
            case FormatKind::Code: *pre = 1; *post = 1; break;
            case FormatKind::CodeBlock: *pre = 3; *post = 3; break;
            case FormatKind::Spoiler: *pre = 2; *post = 2; break;
            case FormatKind::Emote: *pre = 1; *post = 1; break;
            case FormatKind::Mention: *pre = 2; *post = 1; break;
            case FormatKind::Quote: *pre = 0; *post = 0; break;
        }
    };

    QString out;
    int cursor = 0;
    for (const FormatSpan& span : spans) {
        int pre = 0;
        int post = 0;
        delimiters(span.kind, &pre, &post);

        // Text before the span, minus the span's own opening delimiter.
        const int gapStart = span.start - pre;
        if (gapStart > cursor) out += content.mid(cursor, gapStart - cursor);

        const int bodyStart = span.start + pre;
        const int bodyEnd = qMax(bodyStart, span.end() - post);

        switch (span.kind) {
            case FormatKind::Emote:
                // A readable token rather than nothing, so a search hit or a
                // notification still says something happened here.
                out += QStringLiteral("@") + span.target.left(8);
                break;
            case FormatKind::Mention:
                out += QStringLiteral("@");
                break;
            case FormatKind::Quote: {
                // Per line, because the markers are per line. The span covers them
                // so it stays one contiguous run.
                const QString block = content.mid(bodyStart, bodyEnd - bodyStart);
                QStringList kept;
                for (const QString& line : block.split(QLatin1Char('\n'))) {
                    QString stripped = line;
                    if (stripped.startsWith(QLatin1String("> "))) stripped.remove(0, 2);
                    kept.append(stripped);
                }
                out += kept.join(QLatin1Char('\n'));
                break;
            }
            default:
                // Code and spoiler keep their contents. A search that could not find a
                // word because it sat inside a spoiler would be a bug report.
                out += content.mid(bodyStart, bodyEnd - bodyStart);
                break;
        }
        cursor = span.end();
    }
    if (cursor < content.size()) out += content.mid(cursor);
    return out.simplified();
}

QVector<QString> emotesIn(const QString& content, const QVector<FormatSpan>& spans) {
    Q_UNUSED(content);
    QVector<QString> out;
    QSet<QString> seen;
    for (const FormatSpan& span : spans) {
        if (span.kind != FormatKind::Emote) continue;
        if (seen.contains(span.target)) continue;
        seen.insert(span.target);
        out.append(span.target);
    }
    return out;
}

TriggerMatch triggerAt(const QString& text, int caret) {
    TriggerMatch match;
    if (caret <= 0 || caret > text.size()) return match;

    // Walk back over the word the caret sits at the end of. A caret in the middle of a
    // word is the same token, because the word is what is being typed.
    int start = caret;
    while (start > 0 && (isWordChar(text.at(start - 1)) || text.at(start - 1) == QLatin1Char('-'))) {
        --start;
    }
    if (start == caret) return match; // nothing typed after the trigger yet
    if (start == 0) return match;     // no trigger before the word

    const QChar trigger = text.at(start - 1);
    if (trigger != QLatin1Char('@') && trigger != QLatin1Char(':')) return match;

    // The trigger itself must start a word. Without this, "someone@example.com" ends
    // with the token "@example.com" and every email address opens a mention list.
    if (start >= 2 && (isWordChar(text.at(start - 2)) || text.at(start - 2) == QLatin1Char('-'))) {
        return match;
    }

    match.matched = true;
    match.start = start - 1;
    match.length = caret - (start - 1);
    match.prefix = QString(trigger);
    match.term = text.mid(start, caret - start);
    return match;
}

} // namespace nimbus
