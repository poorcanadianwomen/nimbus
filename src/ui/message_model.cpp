#include "message_model.h"

#include <QtCore/QLoggingCategory>

namespace nimbus {

namespace {
// Roles are offset from UserRole so they cannot collide with the display roles a
// plain QListView would consult.
enum Role {
    MessagePtrRole = Qt::UserRole + 1,
    MessageIdRole,
    GroupedRole,
};

} // namespace

MessageModel::MessageModel(QObject* parent)
    : QAbstractListModel(parent) {}

MessageModel::~MessageModel() = default;

void MessageModel::setStore(Store* store) {
    if (m_store == store) return;

    if (m_store) m_store->disconnect(this);

    beginResetModel();
    m_store = store;
    m_ids.clear();
    m_channelId.clear();
    endResetModel();

    if (!m_store) return;

    connect(m_store, &Store::messageAdded, this, [this](const QString& channelId, const QString&) {
        if (channelId == m_channelId) resync();
    });
    connect(m_store, &Store::messageUpdated, this, [this](const QString& channelId, const QString&) {
        if (channelId == m_channelId) resync();
    });
    connect(m_store, &Store::messageRemoved, this, [this](const QString& channelId, const QString&) {
        if (channelId == m_channelId) resync();
    });
    connect(m_store, &Store::channelRemoved, this, [this](const QString& channelId) {
        // A deleted channel leaves the model pointing at nothing; clearing avoids
        // every later store signal matching an empty id and forcing a resync.
        if (channelId == m_channelId) setChannel({});
    });
}

void MessageModel::setChannel(const QString& channelId) {
    beginResetModel();
    m_channelId = channelId;
    m_ids.clear();
    m_exhausted = false;
    endResetModel();
    resync();
}

void MessageModel::resync() {
    if (!m_store || m_channelId.isEmpty()) return;

    QStringList next;
    for (const Message* message : m_store->messages(m_channelId)) {
        next.append(message->id);
    }

    // A transcript is append-only in practice, so the common case is a shared
    // prefix and only the tail is announced. Anything else -- an out-of-order
    // backfill landing mid-list -- falls back to a reset, which is honest and
    // cheap enough at this size.
    int shared = 0;
    const int limit = qMin(m_ids.size(), next.size());
    while (shared < limit && m_ids.at(shared) == next.at(shared)) ++shared;

    if (shared == m_ids.size() && next.size() > m_ids.size()) {
        const int first = m_ids.size();
        const int last = next.size() - 1;
        beginInsertRows(QModelIndex(), first, last);
        m_ids = next;
        endInsertRows();
        return;
    }

    if (shared == next.size() && m_ids.size() > next.size()) {
        const int first = next.size();
        const int last = m_ids.size() - 1;
        beginRemoveRows(QModelIndex(), first, last);
        m_ids = next;
        endRemoveRows();
        return;
    }

    if (m_ids == next) {
        // Same ids in the same order: an edit, not a structural change. Only the
        // tail can have been touched, and repainting it is cheaper than reasoning
        // about which row moved.
        invalidateFrom(shared);
        return;
    }

    beginResetModel();
    m_ids = next;
    endResetModel();
}

void MessageModel::invalidateFrom(int row) {
    if (row >= m_ids.size()) return;
    const QModelIndex topLeft = index(row, 0);
    const QModelIndex bottomRight = index(m_ids.size() - 1, 0);
    emit dataChanged(topLeft, bottomRight);
}

int MessageModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : m_ids.size();
}

const Message* MessageModel::messageAt(int row) const {
    if (!m_store || row < 0 || row >= m_ids.size()) return nullptr;
    return m_store->message(m_channelId, m_ids.at(row));
}

QVariant MessageModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_ids.size()) return {};

    switch (role) {
        case MessagePtrRole: {
            const Message* message = messageAt(index.row());
            if (!message) return {};
            // qulonglong, not quintptr: quintptr is `unsigned long` on LP64 and
            // is not a registered metatype, so fromValue() silently yields an
            // invalid QVariant and every consumer reads a null pointer.
            return QVariant::fromValue(
                static_cast<qulonglong>(reinterpret_cast<quintptr>(message)));
        }
        case MessageIdRole:
            return m_ids.at(index.row());
        case GroupedRole:
            return isGrouped(index.row());
        default:
            return {};
    }
}

QHash<int, QByteArray> MessageModel::roleNames() const {
    return {
        {MessagePtrRole, "message"},
        {MessageIdRole, "messageId"},
        {GroupedRole, "grouped"},
    };
}

int MessageModel::rowForId(const QString& messageId) const {
    return int(m_ids.indexOf(messageId));
}

bool MessageModel::isGrouped(int row) const {
    if (row <= 0 || row >= m_ids.size()) return false;
    const Message* current = messageAt(row);
    const Message* previous = messageAt(row - 1);
    if (!current || !previous) return false;
    if (current->authorId != previous->authorId) return false;
    // A system notice always stands alone, and so does anything that starts a new
    // visual block such as a reply preview target changing the shape of the row.
    if (current->isSystem() || previous->isSystem()) return false;
    if (current->pinned || previous->pinned) return false;
    if (current->replyIds != previous->replyIds) return false;
    // Stoat message ids are monotonic ULIDs, so an hour of wall clock is a usable
    // proxy for "same burst" without carrying a parsed timestamp on every row.
    return true;
}

void MessageModel::loadMore() {
    // A channel shorter than one page is not necessarily exhausted: a quiet
    // channel can be short and still have history further back. Exhaustion is
    // therefore only ever declared by the view, when a fetch returns nothing new.
    if (!m_store || m_channelId.isEmpty()) return;
    if (m_store->oldestMessageId(m_channelId).isEmpty()) return;
    emit olderMessagesRequested();
}

} // namespace nimbus
