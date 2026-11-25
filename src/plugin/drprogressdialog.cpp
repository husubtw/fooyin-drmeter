#include "plugin/drprogressdialog.h"

#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace Fooyin::DrMeter {
DrProgressDialog::DrProgressDialog(int totalTracks, QWidget* parent)
    : QDialog{parent}
    , m_countLabel{new QLabel(this)}
    , m_trackLabel{new QLabel(this)}
    , m_progressBar{new QProgressBar(this)}
    , m_buttonBox{new QDialogButtonBox(QDialogButtonBox::Cancel, this)}
{
    setWindowTitle(tr("DR Meter — Scanning…"));
    setModal(false);
    setMinimumWidth(420);

    const int safeTotal = std::max(1, totalTracks);
    m_progressBar->setRange(0, safeTotal);
    m_progressBar->setValue(0);

    m_countLabel->setText(u"0 / %1"_s.arg(totalTracks));
    m_trackLabel->setText(tr("Preparing scan…"));
    m_trackLabel->setWordWrap(false);
    m_trackLabel->setTextFormat(Qt::PlainText);

    QObject::connect(m_buttonBox, &QDialogButtonBox::rejected, this, &DrProgressDialog::triggerCancel);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_countLabel);
    layout->addWidget(m_trackLabel);
    layout->addWidget(m_progressBar);
    layout->addWidget(m_buttonBox);
}

void DrProgressDialog::setProgress(int completed, int total, const QString& currentTrack)
{
    if(m_cancelRequested || m_finished) {
        return;
    }

    const int safeTotal     = std::max(1, total);
    const int safeCompleted = std::clamp(completed, 0, safeTotal);

    if(m_progressBar->maximum() != safeTotal) {
        m_progressBar->setRange(0, safeTotal);
    }
    m_progressBar->setValue(safeCompleted);
    m_countLabel->setText(u"%1 / %2"_s.arg(safeCompleted).arg(total));

    const QString elided
        = m_trackLabel->fontMetrics().elidedText(currentTrack, Qt::ElideMiddle, std::max(360, m_trackLabel->width()));
    m_trackLabel->setText(elided);
}

void DrProgressDialog::markFinished()
{
    m_finished = true;
}

void DrProgressDialog::reject()
{
    if(m_finished) {
        QDialog::reject();
        return;
    }
    triggerCancel();
}

void DrProgressDialog::closeEvent(QCloseEvent* event)
{
    if(m_finished) {
        QDialog::closeEvent(event);
        return;
    }

    triggerCancel();
    event->ignore();
}

void DrProgressDialog::triggerCancel()
{
    if(m_cancelRequested || m_finished) {
        return;
    }

    m_cancelRequested = true;
    m_trackLabel->setText(tr("Cancelling…"));
    if(auto* cancelButton = m_buttonBox->button(QDialogButtonBox::Cancel)) {
        cancelButton->setEnabled(false);
    }
    Q_EMIT cancelRequested();
}
} // namespace Fooyin::DrMeter
