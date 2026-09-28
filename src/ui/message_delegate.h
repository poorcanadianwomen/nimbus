#pragma once

#include "core/models.h"

#include <QtCore/QHash>
#include <QtCore/QVector>
#include <QtGui/QPixmap>
#include <QtWidgets/QStyledItemDelegate>

namespace nimbus {

class Store;
class ThemeManager;

// Everything needed to paint and to measure one transcript row.
//
// Measured once per row and reused by both sizeHint and paint. Measuring in both
// independently is how a list ends up with a scrollbar that disagrees with its
// own contents: the two computations drift apart the moment a font hint, a wrap
// width or a reaction count changes between the two calls.
struct RowLayout {
    int height = 0;
    int headerHeight = 0;
    int contentTop = 0;
    int contentHeight = 0;
    int contentWidth = 0;
    int replyHeight = 0;
    int attachmentTop = 0;
    int attachmentHeight = 0;
    int embedTop = 0;
    // One height per embed, with the running total prepended. Storing the parts as
    // well as the total is what lets paint place each card without re-measuring,
    // which is the rule the rest of this delegate already follows.
    QVector<int> embedHeights;
    int reactionTop = 0;
    int reactionHeight = 0;
    bool grouped = false;
    bool valid = false;
};

class MessageDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    explicit MessageDelegate(ThemeManager* theme, QObject* parent = nullptr);
    ~MessageDelegate() override;

    void setStore(Store* store);
    void setAvatarProvider(const std::function<QPixmap(const QString& userId, int size)>& provider);
    // Path/colour/size in, pixmap out. A seam rather than an IconLoader member so
    // the delegate stays independent of the icon set and a test can hand it a stub.
    void setIconProvider(
        const std::function<QPixmap(const QString& path, const QColor& colour, const QSize& size)>& provider);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    // Row geometry, exposed for tests that need to assert on layout without
    // depending on a live view.
    RowLayout layoutFor(const QModelIndex& index, int width) const;

    void invalidateLayout(const QString& messageId);
    void clearLayoutCache();

private:
    QString cacheKey(const QModelIndex& index, int width) const;
    const Message* messageFor(const QModelIndex& index) const;
    RowLayout measure(const Message* message, bool grouped, int width) const;
    int measureEmbed(const Embed& embed, int width) const;
    void paintEmbed(QPainter* painter, const QRect& card, const Embed& embed) const;
    void paintContent(QPainter* painter, const QRect& rect, const Message* message,
                      const RowLayout& layout) const;
    void paintReactions(QPainter* painter, const QRect& rect, const Message* message,
                        const RowLayout& layout, const QString& selfId) const;

    ThemeManager* m_theme = nullptr;
    Store* m_store = nullptr;
    std::function<QPixmap(const QString& userId, int size)> m_avatar;
    std::function<QPixmap(const QString& path, const QColor& colour, const QSize& size)> m_icon;

    mutable QHash<QString, RowLayout> m_layoutCache;

    static const int kAvatarSize = 34;
    static const int kGutter = 8;
    static const int kRowPadding = 3;
    static const int kHeaderLine = 18;
    static const int kReactionHeight = 22;
    static const int kAttachmentHeight = 72;
};

} // namespace nimbus
