#pragma once

#include "core/store.h"

#include <QtCore/QAbstractListModel>
#include <QtCore/QHash>
#include <QtCore/QStringList>

namespace nimbus {

// Backs the transcript for exactly one channel.
//
// Rows are not mirrored copies of the store's messages: the model keeps only the
// ordered id list and re-derives it from the store after every change. That is
// what makes the pending-send re-key safe. When a send is confirmed the row's id
// changes from "pending:<nonce>" to the real ULID, and a model that tracked rows
// by id would fail to find the one it was told to update.
class MessageModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit MessageModel(QObject* parent = nullptr);
    ~MessageModel() override;

    void setStore(Store* store);
    void setChannel(const QString& channelId);
    QString channelId() const { return m_channelId; }
    Store* store() const { return m_store; }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Index of a message id, or -1. Used by the view to anchor scrolling.
    int rowForId(const QString& messageId) const;

    // True when the row continues the previous author's run and so should not
    // repeat the avatar and name. The store cannot answer this because it has no
    // notion of adjacency; the view needs it on every paint.
    bool isGrouped(int row) const;

    const Message* messageAt(int row) const;

    // Declares the channel has no further history, so scrolling to the top stops
    // asking. Only the view can know this: a fetch that returned fewer messages
    // than a page is the server saying there is nothing older.
    void setExhausted(bool exhausted) { m_exhausted = exhausted; }
    bool isExhausted() const { return m_exhausted; }

public slots:
    // Pulls history for the open channel. Called on open and on scroll-to-top.
    void loadMore();

signals:
    void olderMessagesRequested();

private:
    void resync();
    void invalidateFrom(int row);

    Store* m_store = nullptr;
    QString m_channelId;
    QStringList m_ids;

    bool m_exhausted = false;
};

} // namespace nimbus
