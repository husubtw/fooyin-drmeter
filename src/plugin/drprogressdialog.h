#pragma once

#include <QDialog>
#include <QString>

class QCloseEvent;
class QDialogButtonBox;
class QLabel;
class QProgressBar;

namespace Fooyin::DrMeter {
class DrProgressDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DrProgressDialog(int totalTracks, QWidget* parent = nullptr);

    void setProgress(int completed, int total, const QString& currentTrack);
    void markFinished();

Q_SIGNALS:
    void cancelRequested();

protected:
    void reject() override;
    void closeEvent(QCloseEvent* event) override;

private:
    void triggerCancel();

    QLabel* m_countLabel{nullptr};
    QLabel* m_trackLabel{nullptr};
    QProgressBar* m_progressBar{nullptr};
    QDialogButtonBox* m_buttonBox{nullptr};
    bool m_cancelRequested{false};
    bool m_finished{false};
};
} // namespace Fooyin::DrMeter
