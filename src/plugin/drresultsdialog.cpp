#include "plugin/drresultsdialog.h"

#include "core/drlogformatter.h"
#include "plugin/drlogexporter.h"
#include "plugin/drresultsmodel.h"
#include "plugin/drsettings.h"
#include "plugin/drtagwriter.h"

#include <QAbstractItemView>
#include <QAction>
#include <QClipboard>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableView>
#include <QVBoxLayout>

#include <algorithm>
#include <ranges>

using namespace Qt::StringLiterals;

namespace Fooyin::DrMeter {
namespace {
bool confirmMultiAlbumLogWrite(QWidget* parent, const std::vector<LogWriteTarget>& targets)
{
    int overwriteCount = 0;
    QStringList lines;
    lines.reserve(static_cast<qsizetype>(targets.size()));

    for(const auto& target : targets) {
        if(target.alreadyExists) {
            ++overwriteCount;
            lines.push_back(u"[OVERWRITE] %1"_s.arg(target.filePath));
        }
        else {
            lines.push_back(target.filePath);
        }
    }

    QDialog dialog{parent};
    dialog.setWindowTitle(QObject::tr("Write DR Logs"));
    dialog.setModal(true);
    dialog.resize(620, 340);

    const QString summaryText
        = overwriteCount > 0
            ? QObject::tr("Write %1 log file(s)? %2 existing file(s) marked [OVERWRITE] will be replaced.")
                  .arg(targets.size())
                  .arg(overwriteCount)
            : QObject::tr("Write %1 log file(s)?").arg(targets.size());

    auto* summaryLabel = new QLabel(summaryText, &dialog);
    summaryLabel->setWordWrap(true);

    auto* pathList = new QPlainTextEdit(&dialog);
    pathList->setReadOnly(true);
    pathList->setLineWrapMode(QPlainTextEdit::NoWrap);
    pathList->setPlainText(lines.join(u'\n'));

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::Cancel, &dialog);
    if(auto* yesBtn = buttonBox->button(QDialogButtonBox::Yes)) {
        yesBtn->setText(QObject::tr("Write Logs"));
    }
    QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(summaryLabel);
    layout->addWidget(pathList, 1);
    layout->addWidget(buttonBox);

    return dialog.exec() == QDialog::Accepted;
}
} // namespace

DrResultsDialog::DrResultsDialog(MusicLibrary* library, std::shared_ptr<AudioLoader> audioLoader,
                                 SettingsManager* settings, DrScanSummary summary, QWidget* parent)
    : QDialog{parent}
    , m_library{library}
    , m_audioLoader{std::move(audioLoader)}
    , m_settings{settings}
    , m_summary{std::move(summary)}
    , m_hasValidAlbums{!DrLogExporter::collectValidAlbums(m_summary.albums).empty()}
    , m_tableView{new QTableView(this)}
    , m_model{new DrResultsModel(m_summary.albums, DrSettings{m_settings}.verboseDialog(), this)}
    , m_proxyModel{new DrResultsSortFilterProxyModel(this)}
    , m_copySelectionAction{new QAction(tr("Copy"), this)}
    , m_footerLabel{new QLabel(formatMeasurementFooter(m_summary.elapsedMs, m_summary.speedRatio), this)}
    , m_updateTagsButton{new QPushButton(tr("Update File Tags"), this)}
    , m_writeLogButton{new QPushButton(tr("Write Log"), this)}
    , m_copyLogButton{new QPushButton(tr("Copy Log"), this)}
    , m_copyResultsButton{new QPushButton(tr("Copy Results"), this)}
    , m_closeButton{new QPushButton(tr("Close"), this)}
{
    setWindowTitle(tr("DR Meter Results"));
    setAttribute(Qt::WA_DeleteOnClose);

    m_proxyModel->setSourceModel(m_model);
    m_tableView->setModel(m_proxyModel);
    m_tableView->setSortingEnabled(false);
    m_tableView->verticalHeader()->hide();
    m_tableView->setAlternatingRowColors(true);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tableView->setWordWrap(false);
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);

    m_copySelectionAction->setShortcut(QKeySequence::Copy);
    m_copySelectionAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    m_copySelectionAction->setEnabled(false);
    m_tableView->addAction(m_copySelectionAction);
    QObject::connect(m_copySelectionAction, &QAction::triggered, this, &DrResultsDialog::onCopySelection);
    QObject::connect(m_tableView, &QWidget::customContextMenuRequested, this, &DrResultsDialog::onTableContextMenu);
    if(auto* selectionModel = m_tableView->selectionModel()) {
        QObject::connect(selectionModel, &QItemSelectionModel::selectionChanged, this,
                         &DrResultsDialog::updateCopySelectionActionState);
    }

    auto* header = m_tableView->horizontalHeader();
    header->setStretchLastSection(false);
    header->setSectionsClickable(true);
    header->setSortIndicatorShown(false);
    QObject::connect(header, &QHeaderView::sectionClicked, this, &DrResultsDialog::onHeaderSectionClicked);
    m_tableView->resizeColumnsToContents();

    if(m_proxyModel->columnCount() > 0) {
        constexpr int ColumnPaddingPx = 16;
        const int nameWidth           = std::max(240, m_tableView->columnWidth(0) + ColumnPaddingPx);
        int numericColsTotalWidth     = 0;

        for(int col = 1; col < m_proxyModel->columnCount(); ++col) {
            header->setSectionResizeMode(col, QHeaderView::Interactive);
            const int paddedWidth = m_tableView->columnWidth(col) + ColumnPaddingPx;
            m_tableView->setColumnWidth(col, paddedWidth);
            numericColsTotalWidth += paddedWidth;
        }

        if(!m_model->isVerbose()) {
            header->setSectionResizeMode(0, QHeaderView::Stretch);
            m_preferredWidth = std::clamp(nameWidth + numericColsTotalWidth + 48, 560, 680);
        }
        else if(m_proxyModel->columnCount() <= 9) {
            header->setSectionResizeMode(0, QHeaderView::Stretch);
            m_preferredWidth = std::clamp(nameWidth + numericColsTotalWidth + 48, 920, 1280);
        }
        else {
            header->setSectionResizeMode(0, QHeaderView::Interactive);
            m_tableView->setColumnWidth(0, nameWidth);
            m_preferredWidth = 1120;
        }
    }

    m_updateTagsButton->setEnabled(m_hasValidAlbums && m_library != nullptr && m_audioLoader != nullptr);
    m_writeLogButton->setEnabled(m_hasValidAlbums);
    m_copyLogButton->setEnabled(m_hasValidAlbums);
    m_copyResultsButton->setEnabled(m_hasValidAlbums);

    QObject::connect(m_updateTagsButton, &QPushButton::clicked, this, &DrResultsDialog::onUpdateFileTags);
    QObject::connect(m_writeLogButton, &QPushButton::clicked, this, &DrResultsDialog::onWriteLog);
    QObject::connect(m_copyLogButton, &QPushButton::clicked, this, &DrResultsDialog::onCopyLog);
    QObject::connect(m_copyResultsButton, &QPushButton::clicked, this, &DrResultsDialog::onCopyResults);
    QObject::connect(m_closeButton, &QPushButton::clicked, this, &DrResultsDialog::close);

    auto* bottomLayout = new QHBoxLayout();
    bottomLayout->addWidget(m_footerLabel, 1);
    bottomLayout->addWidget(m_updateTagsButton);
    bottomLayout->addWidget(m_writeLogButton);
    bottomLayout->addWidget(m_copyLogButton);
    bottomLayout->addWidget(m_copyResultsButton);
    bottomLayout->addWidget(m_closeButton);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(m_tableView, 1);
    mainLayout->addLayout(bottomLayout);
}

DrResultsDialog::~DrResultsDialog()
{
    cancelPendingTagWrite();
}

QSize DrResultsDialog::sizeHint() const
{
    return {m_preferredWidth, 420};
}

void DrResultsDialog::reject()
{
    cancelPendingTagWrite();
    QDialog::reject();
}

void DrResultsDialog::closeEvent(QCloseEvent* event)
{
    cancelPendingTagWrite();
    QDialog::closeEvent(event);
}

void DrResultsDialog::cancelPendingTagWrite()
{
    if(m_tagWriter) {
        m_tagWriter->disconnect(this);
        if(m_tagWriter->isRunning()) {
            m_tagWriter->cancel();
        }
    }
}

void DrResultsDialog::onUpdateFileTags()
{
    if(!m_hasValidAlbums || !m_library || !m_audioLoader) {
        return;
    }

    if(!m_tagWriter) {
        m_tagWriter = new DrTagWriter(m_library, m_audioLoader, this);
        QObject::connect(m_tagWriter, &DrTagWriter::finished, this, [this](const DrTagWriteSummary& result) {
            m_updateTagsButton->setEnabled(m_hasValidAlbums && m_library != nullptr && m_audioLoader != nullptr);
            if(!result.cancelled) {
                QMessageBox::information(this, tr("DR Meter — Update File Tags"), result.formatSummary());
            }
        });
    }

    if(m_tagWriter->isRunning()) {
        return;
    }

    // Disable before invoking writeTags so synchronous completion (e.g., all CUE skipped) re-enables cleanly.
    m_updateTagsButton->setEnabled(false);
    const DrSettings drSettings{m_settings};
    m_tagWriter->writeTags(m_summary.albums, drSettings);
}

void DrResultsDialog::onWriteLog()
{
    const auto validAlbums = DrLogExporter::collectValidAlbums(m_summary.albums);
    if(validAlbums.empty()) {
        return;
    }

    const DrSettings drSettings{m_settings};
    const QString logFileName      = drSettings.logFileName();
    const LogFormatOptions options = DrLogExporter::buildLogFormatOptions();

    if(validAlbums.size() == 1U) {
        QString defaultDir;
        for(const auto& scanned : m_summary.albums) {
            if(!scanned.album.tracks.empty()) {
                defaultDir = scanned.directory;
                break;
            }
        }
        const QString cleanDir    = defaultDir.trimmed().isEmpty() ? QDir::currentPath() : QDir::cleanPath(defaultDir);
        const QString defaultPath = QDir{cleanDir}.filePath(logFileName);

        const QString selectedPath = QFileDialog::getSaveFileName(this, tr("Write DR Log"), defaultPath,
                                                                  tr("Text Files (*.txt);;All Files (*)"));
        if(selectedPath.isEmpty()) {
            return;
        }

        const std::string content = DrLogFormatter::formatAlbumLog(validAlbums.front(), options);
        QString err;
        if(!DrLogExporter::writeSingleLogFile(selectedPath, content, &err)) {
            QMessageBox::warning(this, tr("Write DR Log Failed"), err);
        }
        return;
    }

    const auto targets = DrLogExporter::planDirectoryLogWrites(m_summary.albums, logFileName, options);
    if(targets.empty()) {
        return;
    }

    if(!confirmMultiAlbumLogWrite(this, targets)) {
        return;
    }

    const LogWriteOutcome outcome = DrLogExporter::writePlannedLogs(targets);
    if(outcome.failedFiles > 0) {
        const QString message = tr("Wrote %1 log file(s), %2 failed:\n%3")
                                    .arg(outcome.writtenFiles)
                                    .arg(outcome.failedFiles)
                                    .arg(outcome.errors.join(u'\n'));
        QMessageBox::warning(this, tr("Write DR Logs"), message);
    }
    else {
        QMessageBox::information(this, tr("Write DR Logs"),
                                 tr("Successfully wrote %1 log file(s).").arg(outcome.writtenFiles));
    }
}

void DrResultsDialog::onCopyLog()
{
    const auto validAlbums = DrLogExporter::collectValidAlbums(m_summary.albums);
    if(validAlbums.empty()) {
        return;
    }

    const std::string text = DrLogFormatter::formatMultiAlbumLog(validAlbums, DrLogExporter::buildLogFormatOptions());
    if(auto* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(QString::fromStdString(text));
    }
}

void DrResultsDialog::onHeaderSectionClicked(int logicalIndex)
{
    m_sortState  = advanceTriStateSort(m_sortState, logicalIndex);
    auto* header = m_tableView->horizontalHeader();

    if(m_sortState.column < 0) {
        header->setSortIndicatorShown(false);
        m_proxyModel->sort(-1);
    }
    else {
        header->setSortIndicatorShown(true);
        header->setSortIndicator(m_sortState.column, m_sortState.order);
        m_proxyModel->sort(m_sortState.column, m_sortState.order);
    }
}

void DrResultsDialog::onTableContextMenu(const QPoint& pos)
{
    updateCopySelectionActionState();
    QMenu menu{this};
    menu.addAction(m_copySelectionAction);
    menu.exec(m_tableView->viewport()->mapToGlobal(pos));
}

void DrResultsDialog::onCopySelection()
{
    auto* selectionModel = m_tableView->selectionModel();
    if(!selectionModel) {
        return;
    }

    const QModelIndexList selected = selectionModel->selectedRows();
    if(selected.isEmpty()) {
        return;
    }

    std::vector<int> rows;
    rows.reserve(static_cast<size_t>(selected.size()));
    for(const QModelIndex& idx : selected) {
        rows.push_back(idx.row());
    }
    std::ranges::sort(rows);
    const auto dup = std::ranges::unique(rows);
    rows.erase(dup.begin(), dup.end());

    const QString text = formatRowsAsTsv(*m_proxyModel, rows, false);
    if(text.isEmpty()) {
        return;
    }

    if(auto* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(text);
    }
}

void DrResultsDialog::updateCopySelectionActionState()
{
    auto* selectionModel    = m_tableView->selectionModel();
    const bool hasSelection = selectionModel != nullptr && !selectionModel->selectedRows().isEmpty();
    m_copySelectionAction->setEnabled(hasSelection);
}

void DrResultsDialog::onCopyResults()
{
    const auto validRows = collectValidRows(*m_proxyModel);
    if(validRows.empty()) {
        return;
    }

    const QString text = formatRowsAsTsv(*m_proxyModel, validRows, true);
    if(auto* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(text);
    }
}
} // namespace Fooyin::DrMeter
