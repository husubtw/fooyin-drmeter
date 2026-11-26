#pragma once

#include <core/plugins/coreplugin.h>
#include <core/plugins/plugin.h>
#include <gui/plugins/guiplugin.h>

#include <QObject>
#include <QPointer>

#include <memory>
#include <vector>

class QAction;

namespace Fooyin {
class ActionManager;
class AudioLoader;
class MusicLibrary;
class SettingsManager;
class TrackSelectionController;
} // namespace Fooyin

namespace Fooyin::DrMeter {
class DrProgressDialog;
class DrResultsDialog;
class DrScanner;

class DrMeterPlugin : public QObject,
                      public Plugin,
                      public CorePlugin,
                      public GuiPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID FOOYIN_PLUGIN_IID FILE "drmeter.json")
    Q_INTERFACES(Fooyin::Plugin Fooyin::CorePlugin Fooyin::GuiPlugin)

public:
    void initialise(const CorePluginContext& context) override;
    void initialise(const GuiPluginContext& context) override;
    void shutdown() override;

private:
    void setupContextMenu();
    void startDynamicRangeScan();
    void cleanupActiveScan();
    void pruneClosedDialogs();

    std::shared_ptr<AudioLoader> m_audioLoader;
    MusicLibrary* m_library{nullptr};
    SettingsManager* m_settings{nullptr};
    ActionManager* m_actionManager{nullptr};
    TrackSelectionController* m_selectionController{nullptr};
    QAction* m_measureAction{nullptr};

    QPointer<DrScanner> m_activeScanner;
    QPointer<DrProgressDialog> m_progressDialog;
    bool m_scanCancelRequested{false};
    std::vector<QPointer<DrResultsDialog>> m_openDialogs;
};
} // namespace Fooyin::DrMeter
