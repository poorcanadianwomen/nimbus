#pragma once

#include <QtWidgets/QWidget>

class QLabel;
class QLineEdit;
class QToolButton;

namespace nimbus {

class AvatarCache;
class IconLoader;
class Store;
class ThemeManager;

// The message input: a single bar, a send control, and a cancel affordance that
// only exists while editing an existing message.
//
// One line, not a text area. A bar is what the rest of the client looks like, it
// cannot grow to swallow the transcript, and Shift+Enter is a modifier nobody
// discovers. A message that needs more than a line is pasted in and the client
// sends it as-is; the API takes the newlines.
class Composer : public QWidget {
    Q_OBJECT
public:
    explicit Composer(QWidget* parent = nullptr);
    ~Composer() override;

    // An empty channel id disables the whole widget, so a closed transcript cannot
    // be typed into. The placeholder says why rather than leaving a dead field.
    void setChannel(const QString& channelId);
    QString channel() const { return m_channelId; }

    // The store the emoji button reads the server's emoji from, and the channel
    // whose server they come from.
    void setStore(Store* store);
    void setServerId(const QString& serverId);
    // The emote images, and the host they are addressed on. Without the host the
    // picker has no URLs to ask the cache for and every cell stays a shortcode.
    void setAvatarCache(AvatarCache* cache);
    void setCdnUrl(const QString& url);

    void clear();

    // Loads an existing message for editing. The send control becomes Save and the
    // cancel bar appears; Esc leaves edit mode without sending.
    void beginEdit(const QString& messageId, const QString& content);
    bool isEditing() const { return !m_editId.isEmpty(); }

    // Text in the bar, trimmed. Empty when only whitespace.
    QString content() const;

signals:
    void sendMessage(const QString& content);
    void editMessage(const QString& messageId, const QString& content);
    void cancelEdit();
    // The client has no server route for announcing typing, so this is emitted for
    // whoever wires one up rather than being sent anywhere today.
    void typingStateChanged(bool typing);
    // The + was pressed. The composer does not open a file dialog itself: it has no
    // business knowing about files, and doing it here would put a modal in front of a
    // widget that a test cannot drive.
    void attachRequested();
    // The GIF control has no picker yet and says so in its tooltip; this is the hook
    // one would arrive on. The emoji control does not need one -- it opens its own
    // picker, which is why there is no emojiRequested.
    void gifRequested();

protected:
    // Escape leaves edit mode. Enter needs no handler: QLineEdit emits
    // returnPressed for it, which is wired straight to submit(). Escape emits
    // nothing at all, so it is filtered on the field, which has the focus.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void submit();
    void openEmojiPicker();
    void setEditing(bool editing);
    void updateSendState();

    ThemeManager* m_theme = nullptr;
    QLineEdit* m_input = nullptr;
    QToolButton* m_send = nullptr;
    QToolButton* m_attach = nullptr;
    QToolButton* m_emoji = nullptr;
    QToolButton* m_gif = nullptr;
    QLabel* m_placeholder = nullptr;
    QWidget* m_editBar = nullptr;
    IconLoader* m_icons = nullptr;
    Store* m_store = nullptr;
    AvatarCache* m_avatars = nullptr;
    QString m_cdnUrl;
    QString m_serverId;
    class EmojiPicker* m_picker = nullptr;

    QString m_channelId;
    QString m_editId;
};

} // namespace nimbus
