#ifndef CNBT_BEDROCK_DATABASE_DIALOG_H
#define CNBT_BEDROCK_DATABASE_DIALOG_H

#include <QByteArray>
#include <QDialog>
#include <QString>

class QWidget;

class BedrockDatabaseDialog final : public QDialog {
public:
    static bool chooseRecord(
        QWidget* parent,
        const QString& databaseDirectory,
        QByteArray* selectedKey,
        QString* selectedLabel,
        QString* error
    );

private:
    explicit BedrockDatabaseDialog(QWidget* parent = nullptr);
};

#endif
