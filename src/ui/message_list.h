#pragma once

#include <QtWidgets/QListView>

namespace nimbus {

class MessageModel;

// Virtualised, bottom-anchored transcript.
//
// Scroll anchoring is the whole job here: a new message must not yank the view
// while the user is reading history, and a row that grows taller must not push
// the viewport out from under them.
class MessageList : public QListView {
    Q_OBJECT
public:
    explicit MessageList(QWidget* parent = nullptr);
    ~MessageList() override;

    void setModel(QAbstractItemModel* model);
    // Tolerant setter: the delegate needs the store, and the window builds the
    // two in either order.
    void setStore(class Store* store);
    void scrollToBottom();

signals:
    void loadMoreRequested();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onRowsInserted(const QModelIndex& parent, int first, int last);

private:
    bool m_atBottom = true;
    bool m_pinned = false;
    // Set while scrollToBottom is running. Qt delivers the viewport's geometry
    // event synchronously from inside the layout pass, which lands back in
    // resizeEvent, which calls back in here; without this the pair recurses until
    // the stack gives out.
    bool m_scrolling = false;
    class Store* m_store = nullptr;
};

} // namespace nimbus
