// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 - present Mikael Sundell

#pragma once

#include <QDialog>
#include <QList>
#include <QScopedPointer>
#include <QString>

namespace stageviz {

class ChangesDialogPrivate;

/**
 * @brief Presents the root document and layer files involved in a save decision.
 */
class ChangesDialog : public QDialog {
    Q_OBJECT
public:
    /**
     * @brief Kind of item shown in the changes table.
     */
    enum class Type { RootLayer, Sublayer, SessionLayer };

    /**
     * @brief One root-document or layer summary, without retained USD or widget pointers.
     */
    struct Entry {
        QString identifier;
        QString name;
        Type type = Type::Sublayer;
        QString changes;
        QString location;
        QString status;
        bool canSave = true;
        QString parentIdentifier;
    };

    /**
     * @brief Choice made by the user; dismissal is always Cancel.
     */
    enum class Result { Save, Discard, Cancel };

    /**
     * @brief Constructs the save decision dialog.
     *
     * @param parent Owning window.
     */
    explicit ChangesDialog(QWidget* parent = nullptr);

    /**
     * @brief Releases the dialog and its private implementation.
     */
    ~ChangesDialog() override;

    /**
     * @brief Sets the document name displayed above the table.
     */
    void setDocumentName(const QString& name);

    /**
     * @brief Replaces the summary rows and updates save availability.
     */
    void setEntries(const QList<Entry>& entries);

    /**
     * @brief Controls whether discarding changes is offered.
     *
     * Save As confirmation only offers Save all and Cancel.
     */
    void setDiscardAllowed(bool allowed);

    /**
     * @brief Returns the user's save, discard or cancel choice after exec().
     */
    Result choice() const;

public Q_SLOTS:
    /**
     * @brief Cancels without saving or discarding changes.
     */
    void reject() override;

private:
    Q_DISABLE_COPY_MOVE(ChangesDialog)
    QScopedPointer<ChangesDialogPrivate> p;
};

}  // namespace stageviz
