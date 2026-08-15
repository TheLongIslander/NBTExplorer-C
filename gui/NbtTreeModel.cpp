#include "NbtTreeModel.h"

#include <cmath>

#include <QBrush>
#include <QColor>
#include <QDataStream>
#include <QIODevice>
#include <QMimeData>

extern "C" {
#include "nbt_json.h"
}

namespace {
constexpr auto kMimeType = "application/x-cnbt-tree-node";

QString tagName(const NBTTag* tag) {
    if (!tag) return {};
    if (tag->name && tag->name[0]) return QString::fromUtf8(tag->name);
    return QStringLiteral("(unnamed)");
}
}

NbtTreeModel::NbtTreeModel(NbtDocument* document, QObject* parent)
    : QAbstractItemModel(parent), document_(document) {
    connect(document_, &NbtDocument::treeChanged, this, &NbtTreeModel::rebuild);
    rebuild();
}

std::unique_ptr<NbtTreeModel::Node> NbtTreeModel::buildNode(NBTTag* tag, Node* parent, int row) {
    auto node = std::make_unique<Node>();
    node->tag = tag;
    node->parent = parent;
    node->row = row;
    if (!tag) return node;

    if (tag->type == TAG_Compound) {
        node->children.reserve(tag->value.compound.count);
        for (int i = 0; i < tag->value.compound.count; ++i) {
            node->children.push_back(buildNode(tag->value.compound.items[i], node.get(), i));
        }
    } else if (tag->type == TAG_List) {
        node->children.reserve(tag->value.list.count);
        for (int i = 0; i < tag->value.list.count; ++i) {
            node->children.push_back(buildNode(tag->value.list.items[i], node.get(), i));
        }
    }
    return node;
}

void NbtTreeModel::rebuild() {
    beginResetModel();
    rootNode_ = buildNode(document_ ? document_->root() : nullptr, nullptr, 0);
    endResetModel();
}

NbtTreeModel::Node* NbtTreeModel::nodeFromIndex(const QModelIndex& index) {
    return index.isValid() ? static_cast<Node*>(index.internalPointer()) : nullptr;
}

QModelIndex NbtTreeModel::index(int row, int column, const QModelIndex& parentIndex) const {
    if (!rootNode_ || column < 0 || column >= columnCount() || row < 0) return {};
    if (!parentIndex.isValid()) {
        return row == 0 ? createIndex(0, column, rootNode_.get()) : QModelIndex();
    }
    Node* parentNode = nodeFromIndex(parentIndex);
    if (!parentNode || static_cast<size_t>(row) >= parentNode->children.size()) return {};
    return createIndex(row, column, parentNode->children[row].get());
}

QModelIndex NbtTreeModel::parent(const QModelIndex& childIndex) const {
    Node* child = nodeFromIndex(childIndex);
    if (!child || !child->parent) return {};
    return createIndex(child->parent->row, 0, child->parent);
}

int NbtTreeModel::rowCount(const QModelIndex& parentIndex) const {
    if (!rootNode_) return 0;
    if (!parentIndex.isValid()) return 1;
    if (parentIndex.column() != 0) return 0;
    Node* parentNode = nodeFromIndex(parentIndex);
    return parentNode ? parentNode->children.size() : 0;
}

int NbtTreeModel::columnCount(const QModelIndex&) const {
    return 3;
}

QString NbtTreeModel::valueSummary(const NBTTag* tag) {
    if (!tag) return {};
    switch (tag->type) {
        case TAG_End: return QStringLiteral("End");
        case TAG_Byte: return QString::number(tag->value.byte_val);
        case TAG_Short: return QString::number(tag->value.short_val);
        case TAG_Int: return QString::number(tag->value.int_val);
        case TAG_Long: return QString::number(tag->value.long_val);
        case TAG_Float: return QString::number(tag->value.float_val, 'g', 9);
        case TAG_Double: return QString::number(tag->value.double_val, 'g', 17);
        case TAG_String: return QString::fromUtf8(tag->value.string_val ? tag->value.string_val : "");
        case TAG_Byte_Array: return tr("%1 bytes").arg(tag->value.byte_array.length);
        case TAG_Int_Array: return tr("%1 integers").arg(tag->value.int_array.length);
        case TAG_Long_Array: return tr("%1 longs").arg(tag->value.long_array.length);
        case TAG_List:
            return tr("%1 × %2")
                .arg(tag->value.list.count)
                .arg(QString::fromLatin1(nbt_tag_type_name(tag->value.list.element_type)));
        case TAG_Compound: return tr("%1 tags").arg(tag->value.compound.count);
        default: return {};
    }
}

QVariant NbtTreeModel::data(const QModelIndex& item, int role) const {
    Node* node = nodeFromIndex(item);
    if (!node || !node->tag) return {};
    NBTTag* tag = node->tag;

    if (role == Qt::DisplayRole) {
        if (item.column() == 0) {
            if (node->parent && node->parent->tag && node->parent->tag->type == TAG_List) {
                return QStringLiteral("[%1]").arg(node->row);
            }
            return tagName(tag);
        }
        if (item.column() == 1) return QString::fromLatin1(nbt_tag_type_name(tag->type));
        if (item.column() == 2) return valueSummary(tag);
    }

    if (role == Qt::ToolTipRole) {
        return tr("%1 · Type %2 (%3)\n%4")
            .arg(tagName(tag))
            .arg(static_cast<int>(tag->type))
            .arg(QString::fromLatin1(nbt_tag_type_name(tag->type)))
            .arg(valueSummary(tag));
    }

    if (role == Qt::ForegroundRole && item.column() == 1) {
        switch (tag->type) {
            case TAG_Compound: return QBrush(QColor(QStringLiteral("#a855f7")));
            case TAG_List: return QBrush(QColor(QStringLiteral("#3b82f6")));
            case TAG_String: return QBrush(QColor(QStringLiteral("#16a34a")));
            case TAG_Byte_Array:
            case TAG_Int_Array:
            case TAG_Long_Array: return QBrush(QColor(QStringLiteral("#d97706")));
            default: return QBrush(QColor(QStringLiteral("#dc2626")));
        }
    }
    return {};
}

QVariant NbtTreeModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    if (section == 0) return tr("Name");
    if (section == 1) return tr("Type");
    if (section == 2) return tr("Value");
    return {};
}

Qt::ItemFlags NbtTreeModel::flags(const QModelIndex& item) const {
    if (!item.isValid()) return Qt::ItemIsDropEnabled;
    Node* node = nodeFromIndex(item);
    Qt::ItemFlags result = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (node && node->parent) result |= Qt::ItemIsDragEnabled;
    if (node && node->tag && (node->tag->type == TAG_Compound || node->tag->type == TAG_List)) {
        result |= Qt::ItemIsDropEnabled;
    }
    return result;
}

QStringList NbtTreeModel::mimeTypes() const {
    return {QString::fromLatin1(kMimeType)};
}

QMimeData* NbtTreeModel::mimeData(const QModelIndexList& indexes) const {
    const QModelIndexList firstColumn = [&] {
        QModelIndexList filtered;
        for (const QModelIndex& index : indexes) {
            if (index.column() == 0) filtered.push_back(index);
        }
        return filtered;
    }();
    if (firstColumn.isEmpty()) return nullptr;

    Node* node = nodeFromIndex(firstColumn.first());
    if (!node || !node->parent || !node->tag) return nullptr;
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream << static_cast<quint64>(reinterpret_cast<quintptr>(node->tag))
           << static_cast<quint64>(reinterpret_cast<quintptr>(node->parent->tag))
           << static_cast<qint32>(node->row);
    auto* mime = new QMimeData();
    mime->setData(QString::fromLatin1(kMimeType), payload);
    return mime;
}

bool NbtTreeModel::dropMimeData(
    const QMimeData* mime,
    Qt::DropAction action,
    int row,
    int,
    const QModelIndex& parentIndex
) {
    if (action == Qt::IgnoreAction) return true;
    if (action != Qt::MoveAction || !mime || !mime->hasFormat(QString::fromLatin1(kMimeType))) return false;

    QByteArray payload = mime->data(QString::fromLatin1(kMimeType));
    QDataStream stream(&payload, QIODevice::ReadOnly);
    quint64 sourceAddress = 0;
    quint64 parentAddress = 0;
    qint32 sourceRow = -1;
    stream >> sourceAddress >> parentAddress >> sourceRow;

    Node* destinationNode = parentIndex.isValid() ? nodeFromIndex(parentIndex) : rootNode_.get();
    if (!destinationNode || !destinationNode->tag) return false;
    QString error;
    const bool ok = document_->moveTag(
        reinterpret_cast<NBTTag*>(static_cast<quintptr>(sourceAddress)),
        reinterpret_cast<NBTTag*>(static_cast<quintptr>(parentAddress)),
        sourceRow,
        destinationNode->tag,
        row,
        &error
    );
    if (!ok) emit operationError(error);
    return ok;
}

Qt::DropActions NbtTreeModel::supportedDropActions() const {
    return Qt::MoveAction;
}

NBTTag* NbtTreeModel::tagForIndex(const QModelIndex& item) const {
    Node* node = nodeFromIndex(item);
    return node ? node->tag : nullptr;
}

NBTTag* NbtTreeModel::parentTagForIndex(const QModelIndex& item) const {
    Node* node = nodeFromIndex(item);
    return node && node->parent ? node->parent->tag : nullptr;
}

int NbtTreeModel::rowInParent(const QModelIndex& item) const {
    Node* node = nodeFromIndex(item);
    return node ? node->row : -1;
}

QModelIndex NbtTreeModel::indexForNode(const Node* node, int column) const {
    if (!node) return {};
    return createIndex(node->row, column, const_cast<Node*>(node));
}

NbtTreeModel::Node* NbtTreeModel::findNode(const Node* node, const NBTTag* tag) const {
    if (!node) return nullptr;
    if (node->tag == tag) return const_cast<Node*>(node);
    for (const auto& child : node->children) {
        if (Node* match = findNode(child.get(), tag)) return match;
    }
    return nullptr;
}

QModelIndex NbtTreeModel::indexForTag(const NBTTag* tag) const {
    return indexForNode(findNode(rootNode_.get(), tag));
}
