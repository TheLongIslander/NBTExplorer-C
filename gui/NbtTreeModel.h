#ifndef CNBT_TREE_MODEL_H
#define CNBT_TREE_MODEL_H

#include <memory>
#include <vector>

#include <QAbstractItemModel>

#include "Document.h"

class NbtTreeModel final : public QAbstractItemModel {
    Q_OBJECT

public:
    explicit NbtTreeModel(NbtDocument* document, QObject* parent = nullptr);

    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QModelIndexList& indexes) const override;
    bool dropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column,
                      const QModelIndex& parent) override;
    Qt::DropActions supportedDropActions() const override;

    NBTTag* tagForIndex(const QModelIndex& index) const;
    NBTTag* parentTagForIndex(const QModelIndex& index) const;
    int rowInParent(const QModelIndex& index) const;
    QModelIndex indexForTag(const NBTTag* tag) const;

public slots:
    void rebuild();

signals:
    void operationError(const QString& message);

private:
    struct Node {
        NBTTag* tag = nullptr;
        Node* parent = nullptr;
        int row = 0;
        std::vector<std::unique_ptr<Node>> children;
    };

    static std::unique_ptr<Node> buildNode(NBTTag* tag, Node* parent, int row);
    static QString valueSummary(const NBTTag* tag);
    static Node* nodeFromIndex(const QModelIndex& index);
    QModelIndex indexForNode(const Node* node, int column = 0) const;
    Node* findNode(const Node* node, const NBTTag* tag) const;

    NbtDocument* document_;
    std::unique_ptr<Node> rootNode_;
};

#endif
