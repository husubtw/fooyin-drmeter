#include "plugin/drmeterplugin.h"

#include "plugin/drprogressdialog.h"
#include "plugin/drresultsdialog.h"
#include "plugin/drscanner.h"
#include "plugin/drsettings.h"

#include <gui/guiconstants.h>
#include <gui/trackselectioncontroller.h>
#include <utils/actions/actionmanager.h>
#include <utils/actions/command.h>
#include <utils/utils.h>

#include <QAction>
#include <QMainWindow>
#include <QMenu>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace Fooyin::DrMeter {
namespace {
constexpr auto MeasureActionId = "DrMeter.MeasureDynamicRange";

bool isScannableTrack(const Track& track)
{
    return !track.isRemote() && !track.isVirtual();
}

bool hasScannableTracks(const TrackSelection& selection)
{
    return std::ranges::any_of(selection.tracks, isScannableTrack);
}
} // namespace

void DrMeterPlugin::initialise(const CorePluginContext& context)
{
    m_audioLoader = context.audioLoader;
    m_library     = context.library;
    m_settings    = context.settingsManager;
}

void DrMeterPlugin::initialise(const GuiPluginContext& context)
{
    m_actionManager       = context.actionManager;
    m_selectionController = context.trackSelection;

    setupContextMenu();
}

void DrMeterPlugin::shutdown()
{
    m_scanCancelRequested = false;

    if(m_progressDialog) {
        m_progressDialog->setAttribute(Qt::WA_DeleteOnClose, false);
        m_progressDialog->markFinished();
        m_progressDialog->close();
        if(m_progressDialog) {
            delete m_progressDialog.data();
        }
        m_progressDialog.clear();
    }

    if(m_activeScanner) {
        m_activeScanner->disconnect(this);
        m_activeScanner->cancel();
        m_activeScanner->waitForFinished();
        delete m_activeScanner.data();
        m_activeScanner.clear();
    }

    pruneClosedDialogs();
    for(const auto& dialog : m_openDialogs) {
        if(dialog) {
            dialog->setAttribute(Qt::WA_DeleteOnClose, false);
            dialog->close();
            if(dialog) {
                delete dialog.data();
            }
        }
    }
    m_openDialogs.clear();
}

void DrMeterPlugin::setupContextMenu()
{
    if(!m_selectionController || !m_actionManager) {
        return;
    }

    m_measureAction = new QAction(tr("Dynamic Range Meter"), this);
    m_measureAction->setStatusTip(tr("Measure Dynamic Range (TT DR14) for the selected tracks"));
    QObject::connect(m_measureAction, &QAction::triggered, this, &DrMeterPlugin::startDynamicRangeScan);

    const Id actionId{MeasureActionId};
    if(auto* cmd = m_actionManager->registerAction(m_measureAction, actionId)) {
        cmd->setDescription(tr("Measure Dynamic Range (TT DR14)"));
        cmd->setCategories({tr("Tracks"), tr("Dynamic Range Meter")});
    }

    m_selectionController->setSelectionActionValidator(m_measureAction, hasScannableTracks);

    m_selectionController->registerTrackContextAction(
        this, TrackContextMenuArea::Track, Constants::Menus::Context::TrackSelection, actionId, m_measureAction->text(),
        [this](QMenu* menu, const TrackSelection& selection) {
            if(selection.tracks.empty()) {
                return;
            }
            m_measureAction->setEnabled(hasScannableTracks(selection));
            menu->addAction(m_measureAction);
        },
        Constants::Menus::Context::TrackFinalSeparator);
}

void DrMeterPlugin::startDynamicRangeScan()
{
    if(!m_selectionController || !m_audioLoader) {
        return;
    }

    if(m_activeScanner) {
        if(m_progressDialog) {
            m_progressDialog->raise();
            m_progressDialog->activateWindow();
        }
        return;
    }

    TrackList tracksToScan;
    for(const Track& track : m_selectionController->selectedTracks()) {
        if(isScannableTrack(track)) {
            tracksToScan.push_back(track);
        }
    }

    if(tracksToScan.empty()) {
        return;
    }

    auto* mainWindow = Utils::getMainWindow();
    auto* progress   = new DrProgressDialog(static_cast<int>(tracksToScan.size()), mainWindow);
    progress->setAttribute(Qt::WA_DeleteOnClose);

    auto* scanner = new DrScanner(m_audioLoader, this);

    m_scanCancelRequested = false;
    m_progressDialog      = progress;
    m_activeScanner       = scanner;

    QObject::connect(scanner, &DrScanner::progressChanged, progress, &DrProgressDialog::setProgress);

    QObject::connect(progress, &DrProgressDialog::cancelRequested, this, [this] {
        m_scanCancelRequested = true;
        if(m_activeScanner) {
            m_activeScanner->cancel();
        }
    });

    QObject::connect(scanner, &DrScanner::scanCancelled, this, [this] { cleanupActiveScan(); });

    QObject::connect(scanner, &DrScanner::scanFinished, this, [this](const DrScanSummary& summary) {
        const bool cancelRequested = m_scanCancelRequested;
        cleanupActiveScan();
        if(summary.cancelled || cancelRequested) {
            return;
        }

        pruneClosedDialogs();
        auto* resultsDialog
            = new DrResultsDialog(m_library, m_audioLoader, m_settings, summary, Utils::getMainWindow());
        m_openDialogs.emplace_back(resultsDialog);
        resultsDialog->show();
    });

    const DrSettings drSettings{m_settings};
    scanner->startScan(tracksToScan, drSettings.albumGroupingPattern());
    progress->show();
}

void DrMeterPlugin::cleanupActiveScan()
{
    m_scanCancelRequested = false;

    if(m_progressDialog) {
        m_progressDialog->markFinished();
        m_progressDialog->close();
        m_progressDialog.clear();
    }

    if(m_activeScanner) {
        DrScanner* scanner = m_activeScanner.data();
        m_activeScanner.clear();
        scanner->disconnect(this);
        scanner->cancel();
        scanner->waitForFinished();
        scanner->deleteLater();
    }
}

void DrMeterPlugin::pruneClosedDialogs()
{
    std::erase_if(m_openDialogs, [](const QPointer<DrResultsDialog>& dlg) { return dlg.isNull(); });
}
} // namespace Fooyin::DrMeter
