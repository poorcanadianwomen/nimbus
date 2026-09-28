#include "message_list.h"

#include "message_delegate.h"
#include "message_model.h"
#include "theme.h"

#include <QtWidgets/QScrollBar>
#include <QtGui/QWheelEvent>
#include <QtWidgets/QAbstractItemView>

namespace nimbus {

namespace {
// How close to the bottom still counts as "at the bottom". A few pixels of
// slack, because a wheel tick routinely overshoots by exactly that much and
// snapping back would fight the user.
constexpr int kBottomSlackPx = 24;

// Left and right inset for the whole transcript. The delegate draws relative to its
// item rect, so the margin has to be applied to the viewport: putting it in the
// delegate would mean every row re-deriving it, and the composer's bar is centred
// against a pane that has no such inset, so the two would not line up.
constexpr int kGutterPx = 12;
} // namespace

MessageList::MessageList(QWidget* parent)
    : QListView(parent) {
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setSelectionMode(QAbstractItemView::NoSelection);
    setFocusPolicy(Qt::NoFocus);
    setResizeMode(QListView::Adjust);
    setUniformItemSizes(false);
    setWordWrap(true);
    setSpacing(0);
    setViewportMargins(kGutterPx, 0, kGutterPx, 0);
    viewport()->setAttribute(Qt::WA_TranslucentBackground);

    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int) {
        QScrollBar* bar = verticalScrollBar();
        m_atBottom = bar->value() >= bar->maximum() - kBottomSlackPx;
    });
}

MessageList::~MessageList() = default;

void MessageList::setModel(QAbstractItemModel* model) {
    QListView::setModel(model);
    if (!model) return;

    connect(model, &QAbstractItemModel::rowsInserted, this, &MessageList::onRowsInserted);
    connect(model, &QAbstractItemModel::modelReset, this, [this]() {
        scrollToBottom();
    });
    scrollToBottom();
}

void MessageList::setStore(Store* store) {
    m_store = store;
    if (auto* delegate = itemDelegate()) {
        if (auto* messageDelegate = qobject_cast<MessageDelegate*>(delegate)) {
            messageDelegate->setStore(store);
        }
    }
}

void MessageList::onRowsInserted(const QModelIndex& parent, int first, int last) {
    Q_UNUSED(parent);
    Q_UNUSED(first);
    Q_UNUSED(last);
    // Only follow the tail when the user is already there. Following
    // unconditionally is what makes a chat window jump mid-sentence.
    if (m_atBottom) scrollToBottom();
}

void MessageList::scrollToBottom() {
    if (!model() || model()->rowCount() == 0) return;
    if (m_scrolling) return;

    // QListView::scrollToBottom runs a layout pass, and Qt delivers the viewport
    // resize that pass causes synchronously. resizeEvent then re-anchors, which
    // starts another pass, and the two call each other until the stack overflows.
    // Re-entering here is always redundant: the outer pass is still going to
    // finish and position the scrollbar.
    m_scrolling = true;
    QListView::scrollToBottom();
    m_atBottom = true;
    m_scrolling = false;
}

void MessageList::wheelEvent(QWheelEvent* event) {
    // Reaching the top is the signal to fetch older history. Checked before the
    // base handler so the request goes out on the wheel tick that crossed it
    // rather than a frame later.
    if (verticalScrollBar()->value() <= 0) {
        emit loadMoreRequested();
    }
    QListView::wheelEvent(event);
}

void MessageList::resizeEvent(QResizeEvent* event) {
    QListView::resizeEvent(event);
    // Every row was measured against the old width, so the scrollbar's maximum is
    // stale; re-anchor if the user was at the bottom.
    if (m_atBottom) scrollToBottom();
}

} // namespace nimbus
