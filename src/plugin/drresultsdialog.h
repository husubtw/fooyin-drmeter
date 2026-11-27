#pragma once

#include "plugin/drresultsmodel.h"
#include "plugin/drscanner.h"

#include <QDialog>

#include <memory>

class QAction;
class QCloseEvent;
class QLabel;
class QPoint;
class QPushButton;
class QTableView;

namespace Fooyin {
class AudioLoader;
class MusicLibrary;
class SettingsManager;
} // namespace Fooyin

namespace Fooyin::DrMeter {
class DrTagWriter;

class DrResultsDialog : public QDialog
{
    Q_OBJECT

public:
    DrResultsDialog(MusicLibrary* library, std::shared_ptr<AudioLoader> audioLoader, SettingsManager* settings,
                    DrScanSummary summary, QWidget* parent = nullptr);
    ~DrResultsDialog() override;

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void reject() override;
    void closeEvent(QCloseEvent* event) override;

private:
    void onHeaderSectionClicked(int logicalIndex);
    void onTableContextMenu(const QPoint& pos);
    void onCopySelection();
    void updateCopySelectionActionState();
    void onUpdateFileTags();
    void onWriteLog();
    void onCopyLog();
    void onCopyResults();
    void cancelPendingTagWrite();

    MusicLibrary* m_library{nullptr};
    std::shared_ptr<AudioLoader> m_audioLoader;
    SettingsManager* m_settings{nullptr};
    DrScanSummary m_summary;
    bool m_hasValidAlbums{false};
    int m_preferredWidth{960};
    TriStateSort m_sortState;

    QTableView* m_tableView{nullptr};
    DrResultsModel* m_model{nullptr};
    DrResultsSortFilterProxyModel* m_proxyModel{nullptr};
    QAction* m_copySelectionAction{nullptr};
    QLabel* m_footerLabel{nullptr};
    QPushButton* m_updateTagsButton{nullptr};
    QPushButton* m_writeLogButton{nullptr};
    QPushButton* m_copyLogButton{nullptr};
    QPushButton* m_copyResultsButton{nullptr};
    QPushButton* m_closeButton{nullptr};

    DrTagWriter* m_tagWriter{nullptr};
};
} // namespace Fooyin::DrMeter
