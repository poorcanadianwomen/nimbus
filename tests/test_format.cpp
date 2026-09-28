#include "../src/ui/message_format.h"

#include "../src/core/store.h"

#include <QtTest/QtTest>

namespace {

// A synthetic ULID, 26 characters so it clears the mention parser's id test. An
// earlier version of this file had a real account id in it, pasted in while probing
// the CDN, which has no business being in a public repository.
const QString kUserId = QStringLiteral("01AAAAAAAAAAAAAAAAAAAAAA");

nimbus::FormatOptions withEmotes(const QStringList& names) {
    nimbus::FormatOptions options;
    options.lookupEmote = [names](const QString& name) {
        return names.contains(name) ? QStringLiteral("ID_") + name : QString();
    };
    return options;
}

QVector<nimbus::FormatKind> kindsOf(const QVector<nimbus::FormatSpan>& spans) {
    QVector<nimbus::FormatKind> out;
    for (const nimbus::FormatSpan& span : spans) out.append(span.kind);
    return out;
}

QString slice(const QString& text, const nimbus::FormatSpan& span) {
    return text.mid(span.start, span.length);
}

} // namespace

class TestFormat : public QObject {
    Q_OBJECT
private slots:
    void test_plain_text_has_no_spans();
    void test_quote_lines_become_one_span();
    void test_quote_may_open_on_the_first_line();
    void test_quote_stops_at_the_first_unquoted_line();
    void test_inline_code();
    void test_unclosed_code_is_literal();
    void test_empty_code_pair_is_literal();
    void test_code_may_not_cross_a_line();
    void test_fenced_block();
    void test_unclosed_fence_takes_the_rest();
    void test_fence_inside_a_block_is_content();
    void test_spoiler();
    void test_spoiler_may_not_cross_a_line();
    void test_emote_needs_to_exist();
    void test_emote_names_allow_hyphens();
    void test_longest_name_is_not_split();
    void test_mention();
    void test_mention_needs_a_plausible_id();
    void test_spans_never_overlap_and_are_sorted();
    void test_mixed_syntax_in_one_message();
    void test_plain_text_drops_markers_and_keeps_spoilers();
    void test_emotes_in_is_deduplicated_and_ordered();
    void test_trigger_requires_a_word_boundary();
    void test_trigger_term_and_range();
    void test_trigger_follows_the_word_it_is_typing();
    void test_parsing_is_deterministic();
};

// --- nothing to do ---

void TestFormat::test_plain_text_has_no_spans() {
    const QString text = QStringLiteral("just a normal message");
    const auto spans = nimbus::parseMessageFormatting(text, withEmotes({}));
    QVERIFY(spans.isEmpty());
    QCOMPARE(nimbus::plainTextOf(text, spans), text);
}

// --- quotes ---

void TestFormat::test_quote_lines_become_one_span() {
    const QString text = QStringLiteral("> first\n> second");
    const auto spans = nimbus::parseMessageFormatting(text, withEmotes({}));
    QCOMPARE(kindsOf(spans), QVector<nimbus::FormatKind>{nimbus::FormatKind::Quote});
    // The span keeps the markers so it stays one contiguous run; plainTextOf is what
    // strips them, per line.
    QCOMPARE(slice(text, spans.first()), QStringLiteral("> first\n> second"));
    QCOMPARE(nimbus::plainTextOf(text, spans), QStringLiteral("first second"));
}

void TestFormat::test_quote_stops_at_the_first_unquoted_line() {
    const QString text = QStringLiteral("> quoted\nplain");
    const auto spans = nimbus::parseMessageFormatting(text, withEmotes({}));
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans.first().kind, nimbus::FormatKind::Quote);
    QVERIFY(!text.mid(spans.first().start, spans.first().length).contains(QStringLiteral("plain")));
}

void TestFormat::test_quote_may_open_on_the_first_line() {
    const QString text = QStringLiteral("> only line");
    const auto spans = nimbus::parseMessageFormatting(text, withEmotes({}));
    QCOMPARE(kindsOf(spans), QVector<nimbus::FormatKind>{nimbus::FormatKind::Quote});
    QCOMPARE(slice(text, spans.first()), QStringLiteral("> only line"));
    QCOMPARE(nimbus::plainTextOf(text, spans), QStringLiteral("only line"));
}

// --- inline code ---

void TestFormat::test_inline_code() {
    const QString text = QStringLiteral("before `some code` after");
    const auto spans = nimbus::parseMessageFormatting(text, withEmotes({}));
    QCOMPARE(kindsOf(spans), QVector<nimbus::FormatKind>{nimbus::FormatKind::Code});
    // Inclusive of the backticks, like every other span, so a selection or a hit region
    // covers the whole token.
    QCOMPARE(slice(text, spans.first()), QStringLiteral("`some code`"));
}

void TestFormat::test_unclosed_code_is_literal() {
    // No closing backtick anywhere. The backtick must survive as text, or the rest of
    // the message is silently swallowed.
    const QString text = QStringLiteral("a ` never closed");
    QVERIFY(nimbus::parseMessageFormatting(text, withEmotes({})).isEmpty());
    QCOMPARE(nimbus::plainTextOf(text, {}), text);
}

void TestFormat::test_empty_code_pair_is_literal() {
    // "``" is a pair of backticks, not empty code.
    const QString text = QStringLiteral("a `` b");
    QVERIFY(nimbus::parseMessageFormatting(text, withEmotes({})).isEmpty());
}

void TestFormat::test_code_may_not_cross_a_line() {
    const QString text = QStringLiteral("`start\nend`");
    QVERIFY(nimbus::parseMessageFormatting(text, withEmotes({})).isEmpty());
}

// --- fenced blocks ---

void TestFormat::test_fenced_block() {
    const QString text = QStringLiteral("```\nline one\nline two\n```");
    const auto spans = nimbus::parseMessageFormatting(text, withEmotes({}));
    QCOMPARE(kindsOf(spans), QVector<nimbus::FormatKind>{nimbus::FormatKind::CodeBlock});
    QCOMPARE(slice(text, spans.first()), QStringLiteral("```\nline one\nline two\n```"));
}

void TestFormat::test_unclosed_fence_takes_the_rest() {
    const QString text = QStringLiteral("```\nnever closed");
    const auto spans = nimbus::parseMessageFormatting(text, withEmotes({}));
    QCOMPARE(kindsOf(spans), QVector<nimbus::FormatKind>{nimbus::FormatKind::CodeBlock});
    QCOMPARE(slice(text, spans.first()), QStringLiteral("never closed"));
}

void TestFormat::test_fence_inside_a_block_is_content() {
    // Only the first closing fence ends it.
    const QString text = QStringLiteral("```\na ``` b\n```");
    const auto spans = nimbus::parseMessageFormatting(text, withEmotes({}));
    QCOMPARE(spans.size(), 1);
    QCOMPARE(slice(text, spans.first()), QStringLiteral("```\na ``` b\n```"));
}

// --- spoilers ---

void TestFormat::test_spoiler() {
    const QString text = QStringLiteral("the answer is ||42|| honestly");
    const auto spans = nimbus::parseMessageFormatting(text, withEmotes({}));
    QCOMPARE(kindsOf(spans), QVector<nimbus::FormatKind>{nimbus::FormatKind::Spoiler});
    QCOMPARE(slice(text, spans.first()), QStringLiteral("||42||"));
}

void TestFormat::test_spoiler_may_not_cross_a_line() {
    // Otherwise one stray || hides the rest of the message.
    const QString text = QStringLiteral("||start\nend||");
    QVERIFY(nimbus::parseMessageFormatting(text, withEmotes({})).isEmpty());
    // plainTextOf collapses to a single line, so the newline becomes a space; what
    // matters is that both markers survive, because a stray || that was swallowed
    // instead of shown would be a spoiler that reveals itself by hiding.
    const QString plain = nimbus::plainTextOf(text, {});
    QVERIFY(plain.contains(QStringLiteral("||start")));
    QVERIFY(plain.contains(QStringLiteral("end||")));
}

// --- emotes ---

void TestFormat::test_emote_needs_to_exist() {
    // The whole point of the lookup: ":nope:" is what a person typed, and turning it
    // into a broken image is worse than leaving the text.
    QVERIFY(nimbus::parseMessageFormatting(QStringLiteral(":nope:"), withEmotes({})).isEmpty());

    const auto spans =
        nimbus::parseMessageFormatting(QStringLiteral(":yes:"), withEmotes({QStringLiteral("yes")}));
    QCOMPARE(kindsOf(spans), QVector<nimbus::FormatKind>{nimbus::FormatKind::Emote});
    QCOMPARE(spans.first().target, QStringLiteral("ID_yes"));
    // The span covers the whole ":yes:" including both colons, so replacing it with an
    // image does not leave a stray colon behind.
    QCOMPARE(spans.first().length, 5);
}

void TestFormat::test_emote_names_allow_hyphens() {
    const auto spans = nimbus::parseMessageFormatting(QStringLiteral(":neko-sad:"),
                                                      withEmotes({QStringLiteral("neko-sad")}));
    QCOMPARE(kindsOf(spans), QVector<nimbus::FormatKind>{nimbus::FormatKind::Emote});
    QCOMPARE(spans.first().target, QStringLiteral("ID_neko-sad"));
}

void TestFormat::test_longest_name_is_not_split() {
    // ":ab:" must not match inside ":abc:" -- the scan runs to the closing colon, so it
    // sees the longer name and the lookup decides.
    const auto spans = nimbus::parseMessageFormatting(QStringLiteral(":abc:"),
                                                      withEmotes({QStringLiteral("abc")}));
    QCOMPARE(spans.size(), 1);
    QCOMPARE(spans.first().target, QStringLiteral("ID_abc"));
}

// --- mentions ---

void TestFormat::test_mention() {
    const QString id = kUserId;
    const QString text = QStringLiteral("hi <@") + id + QStringLiteral("> there");
    const auto spans = nimbus::parseMessageFormatting(text, withEmotes({}));
    QCOMPARE(kindsOf(spans), QVector<nimbus::FormatKind>{nimbus::FormatKind::Mention});
    QCOMPARE(spans.first().target, id);
    QCOMPARE(slice(text, spans.first()), QStringLiteral("<@") + id + QStringLiteral(">"));
}

void TestFormat::test_mention_needs_a_plausible_id() {
    // "<@everyone>" is not a user id and must not become a broken mention chip.
    QVERIFY(nimbus::parseMessageFormatting(QStringLiteral("<@everyone>"), withEmotes({})).isEmpty());
    // Too short to be a ULID.
    QVERIFY(nimbus::parseMessageFormatting(QStringLiteral("<@abc>"), withEmotes({})).isEmpty());
}

// --- invariants ---

void TestFormat::test_spans_never_overlap_and_are_sorted() {
    // A deliberately hostile message: every rule fighting over the same characters.
    const QStringList emotes = {QStringLiteral("a")};
    const QString id = kUserId;
    const QStringList cases = {
        QStringLiteral("> `code` :a: <@%1> ||spoiler||").arg(id),
        QStringLiteral("```\n> quoted `:a:`\n```"),
        QStringLiteral(":a::a::a:"),
        QStringLiteral("> > > deep"),
        QStringLiteral("`> :a:`"),
        QStringLiteral("||`x`||"),
    };

    for (const QString& text : cases) {
        const auto spans = nimbus::parseMessageFormatting(text, withEmotes(emotes));
        int previousEnd = -1;
        for (const nimbus::FormatSpan& span : spans) {
            QVERIFY2(span.start >= previousEnd,
                     qPrintable(QStringLiteral("overlapping spans in: %1").arg(text)));
            QVERIFY2(span.length >= 0, qPrintable(QStringLiteral("negative length: %1").arg(text)));
            QVERIFY2(span.end() <= text.size(),
                     qPrintable(QStringLiteral("span past the end in: %1").arg(text)));
            previousEnd = span.end();
        }
    }
}

void TestFormat::test_mixed_syntax_in_one_message() {
    const QString id = kUserId;
    const QString text = QStringLiteral("> :a: quoted\nthen `code` and :a: and <@") + id +
                         QStringLiteral("> plus ||hidden||");
    const auto spans = nimbus::parseMessageFormatting(text, withEmotes({QStringLiteral("a")}));

    // Every rule fired, and the order is the order they appear in the text.
    QCOMPARE(kindsOf(spans),
             QVector<nimbus::FormatKind>({nimbus::FormatKind::Quote, nimbus::FormatKind::Emote,
                                           nimbus::FormatKind::Code, nimbus::FormatKind::Emote,
                                           nimbus::FormatKind::Mention,
                                           nimbus::FormatKind::Spoiler}));
}

void TestFormat::test_plain_text_drops_markers_and_keeps_spoilers() {
    const QString id = kUserId;
    const QString text = QStringLiteral("> quoted `code` :a: <@") + id +
                         QStringLiteral("> ||secret||");
    const auto spans = nimbus::parseMessageFormatting(text, withEmotes({QStringLiteral("a")}));
    const QString plain = nimbus::plainTextOf(text, spans);

    QVERIFY(!plain.contains(QLatin1Char('`')));
    QVERIFY(!plain.contains(QStringLiteral("> ")));
    // A search that could not find a word because it sat inside a spoiler would be a
    // bug report, so the contents stay.
    QVERIFY2(plain.contains(QStringLiteral("secret")), "the spoiler contents were dropped");
    // Mentions and emotes become readable tokens rather than vanishing.
    QVERIFY(plain.contains(QLatin1Char('@')));
}

void TestFormat::test_emotes_in_is_deduplicated_and_ordered() {
    const QString text = QStringLiteral(":a: :b: :a:");
    const auto spans = nimbus::parseMessageFormatting(
        text, withEmotes({QStringLiteral("a"), QStringLiteral("b")}));
    const QVector<QString> found = nimbus::emotesIn(text, spans);
    QCOMPARE(found.size(), 2);
    QCOMPARE(found.at(0), QStringLiteral("ID_a"));
    QCOMPARE(found.at(1), QStringLiteral("ID_b"));
}

// --- autocomplete trigger ---

void TestFormat::test_trigger_requires_a_word_boundary() {
    // An email address must not open a mention list.
    const QString email = QStringLiteral("someone@example.com");
    QVERIFY(!nimbus::triggerAt(email, email.size()).matched);

    const QString colon = QStringLiteral("a:b");
    QVERIFY(!nimbus::triggerAt(colon, colon.size()).matched);
}

void TestFormat::test_trigger_term_and_range() {
    const QString text = QStringLiteral("hello @ma there");
    const auto match = nimbus::triggerAt(text, 9); // just after "ma"
    QVERIFY(match.matched);
    QCOMPARE(match.prefix, QStringLiteral("@"));
    QCOMPARE(match.term, QStringLiteral("ma"));
    QCOMPARE(text.mid(match.start, match.length), QStringLiteral("@ma"));

    const QString colonText = QStringLiteral("look :neko");
    const auto colon = nimbus::triggerAt(colonText, colonText.size());
    QVERIFY(colon.matched);
    QCOMPARE(colon.prefix, QStringLiteral(":"));
    QCOMPARE(colon.term, QStringLiteral("neko"));
}

void TestFormat::test_trigger_follows_the_word_it_is_typing() {
    // Every caret position in "@maple" is the same token being typed, so the popup
    // stays open and the term grows with it rather than waiting for the word to end.
    const QString text = QStringLiteral("@maple");
    for (int caret = 2; caret <= text.size(); ++caret) {
        const auto match = nimbus::triggerAt(text, caret);
        QVERIFY2(match.matched, qPrintable(QStringLiteral("no trigger at caret %1").arg(caret)));
        QCOMPARE(match.term, text.mid(1, caret - 1));
    }

    // A second word in the same message opens a fresh trigger with its own term.
    const QStringList two = {QStringLiteral("@maple and @neo")};
    const auto second = nimbus::triggerAt(two.first(), two.first().size());
    QVERIFY(second.matched);
    QCOMPARE(second.term, QStringLiteral("neo"));
    QCOMPARE(second.start, 11); // the second @ in "@maple and @neo"
}

void TestFormat::test_parsing_is_deterministic() {
    // The same input must give the same spans every time, or the delegate's layout
    // cache and the paint would disagree. Run enough times that a QHash-order
    // dependency would show up.
    const QString id = kUserId;
    const QString text = QStringLiteral("> :b: :a: `c` <@") + id + QStringLiteral("> :a: :b:");
    const auto first =
        nimbus::parseMessageFormatting(text, withEmotes({QStringLiteral("a"), QStringLiteral("b")}));
    for (int i = 0; i < 200; ++i) {
        const auto again =
            nimbus::parseMessageFormatting(text, withEmotes({QStringLiteral("a"), QStringLiteral("b")}));
        QCOMPARE(kindsOf(again), kindsOf(first));
        for (int j = 0; j < again.size(); ++j) {
            QCOMPARE(again.at(j).start, first.at(j).start);
            QCOMPARE(again.at(j).length, first.at(j).length);
            QCOMPARE(again.at(j).target, first.at(j).target);
        }
    }
}

QTEST_MAIN(TestFormat)
#include "test_format.moc"
