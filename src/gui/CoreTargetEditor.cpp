// =================================================================
// src/gui/CoreTargetEditor.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. R3 Core session presentation and actions.
// =================================================================

#include "gui/CoreTargetEditor.h"

#include "core/session/RemoteStationOptions.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace NereusSDR {
namespace {

constexpr int kLabelMaximumLength = 512;
constexpr int kUrlMaximumLength = 4096;
constexpr int kTokenMaximumLength = 8192;
constexpr int kFingerprintMaximumLength = 256;

} // namespace

CoreTargetEditor::CoreTargetEditor(const SavedCoreTarget& initial, QWidget* parent)
    : QDialog(parent)
    , m_initial(initial)
{
    setWindowTitle(tr("Core setup"));
    setObjectName(QStringLiteral("coreTargetEditor"));

    auto* layout = new QVBoxLayout(this);
    auto* explanation = new QLabel(
        tr("Enter the Core address and its access token. A certificate pin must come from Core setup."),
        this);
    explanation->setObjectName(QStringLiteral("coreTargetEditorExplanation"));
    explanation->setTextFormat(Qt::PlainText);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);

    auto* form = new QFormLayout();
    m_labelEdit = new QLineEdit(initial.label, this);
    m_labelEdit->setObjectName(QStringLiteral("coreTargetEditorLabel"));
    m_labelEdit->setMaxLength(kLabelMaximumLength);
    m_addressEdit = new QLineEdit(initial.connection.url, this);
    m_addressEdit->setObjectName(QStringLiteral("coreTargetEditorAddress"));
    m_addressEdit->setMaxLength(kUrlMaximumLength);
    m_tokenEdit = new QLineEdit(initial.connection.token, this);
    m_tokenEdit->setObjectName(QStringLiteral("coreTargetEditorToken"));
    m_tokenEdit->setMaxLength(kTokenMaximumLength);
    m_tokenEdit->setEchoMode(QLineEdit::Password);
    m_fingerprintEdit = new QLineEdit(initial.connection.fingerprint, this);
    m_fingerprintEdit->setObjectName(QStringLiteral("coreTargetEditorFingerprint"));
    m_fingerprintEdit->setMaxLength(kFingerprintMaximumLength);
    m_allowUnpinnedCheck = new QCheckBox(tr("Allow unpinned certificate (bench only)"), this);
    m_allowUnpinnedCheck->setObjectName(QStringLiteral("coreTargetEditorAllowUnpinned"));
    m_allowUnpinnedCheck->setChecked(initial.connection.allowUnpinned);
    form->addRow(tr("Label:"), m_labelEdit);
    form->addRow(tr("Address:"), m_addressEdit);
    form->addRow(tr("Token:"), m_tokenEdit);
    form->addRow(tr("Certificate fingerprint:"), m_fingerprintEdit);
    form->addRow({}, m_allowUnpinnedCheck);
    layout->addLayout(form);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("coreTargetEditorError"));
    m_errorLabel->setTextFormat(Qt::PlainText);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setVisible(false);
    layout->addWidget(m_errorLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    auto* saveButton = buttons->button(QDialogButtonBox::Save);
    auto* cancelButton = buttons->button(QDialogButtonBox::Cancel);
    saveButton->setObjectName(QStringLiteral("coreTargetEditorSave"));
    cancelButton->setObjectName(QStringLiteral("coreTargetEditorCancel"));
    saveButton->setAutoDefault(false);
    cancelButton->setAutoDefault(false);
    layout->addWidget(buttons);
    connect(saveButton, &QPushButton::clicked, this, [this] {
        if (validate()) {
            accept();
        }
    });
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    resize(540, sizeHint().height());
}

SavedCoreTarget CoreTargetEditor::target() const
{
    SavedCoreTarget result = m_initial;
    result.label = m_labelEdit->text().trimmed();
    result.connection.url = m_addressEdit->text().trimmed();
    result.connection.token = m_tokenEdit->text();
    result.connection.fingerprint = m_fingerprintEdit->text();
    result.connection.allowUnpinned = m_allowUnpinnedCheck->isChecked();
    if (result.connection.url != m_initial.connection.url) {
        result.lastRadioName.clear();
        result.lastRadioMac.clear();
    }
    return result;
}

bool CoreTargetEditor::validate()
{
    if (!RemoteStationOptions::isValidStationUrl(m_addressEdit->text().trimmed())) {
        // Keep this fixed: QUrl's detailed error can reflect untrusted input.
        m_errorLabel->setText(tr("Enter a valid station address beginning with ws:// or wss://."));
        m_errorLabel->setVisible(true);
        return false;
    }
    m_errorLabel->clear();
    m_errorLabel->setVisible(false);
    return true;
}

} // namespace NereusSDR
