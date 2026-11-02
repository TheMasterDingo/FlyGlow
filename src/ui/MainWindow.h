#pragma once

#include "DockView.h"
#include "Project.h"
#include "core/KeyframeEffect.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QTimer>
#include <QUrl>
#include <memory>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QTabBar;
class QToolButton;

namespace fg {

class ColorButton;
class EffectLibrary;
class HidDock;
class Inspector;
class OnlineUpdates;
class PresetBrowser;
class ProgressButton;
class ShaderEditor;
class SliderRow;
class SpaceStationClient;
class Timeline;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /** Brings the window forward and opens each scene as openScene() does; from the command line or a second launch. */
    void openExternal(const QStringList& paths);

protected:
    void closeEvent(QCloseEvent* e) override;
    void changeEvent(QEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    void retranslateUi();
    void setUploadProgress(double fraction);
    void switchLanguage(const QString& language);
    void syncSettingsUi();
    enum class PaintTool { Brush, Eraser, Fill, Picker };
    enum class UploadVia { None, Hid, Service };

    QWidget* buildHeader();
    QWidget* buildToolStrip();
    QWidget* buildTransport();
    void wireProject();
    void wireDevices();
    void wireShortcuts();
    void selectTool(PaintTool t);
    void finishUpload(bool ok, const QString& message);

    void tick();
    void setPlaying(bool on);
    void setPhase(double phase);
    void renderPreview();

    KeyframeEffect* currentKeys();
    void updateToolState();
    void updateKeyUi();
    void updateBakeInfo();
    void updateTitle();
    void updateConnectionUi();

    void onStrokeBegan(bool erase);
    void onPainted(const QVector<int>& leds, const QVector<float>& weights);
    void onFill(int led);
    void onPick(int led);
    void setBrushColor(const QColor& c);

    void addKeyAtPlayhead();
    void removeSelectedKey();
    void showKeyMenu(int index, const QPoint& globalPos);
    void showTimelineMenu(double phase, const QPoint& globalPos);
    void trimToSelection();

    void replaceCurrentEffect(const QString& typeId);
    void importMedia();
    void addShaderLayer();
    void openShaderEditor();
    void sendToDock();
    void showOutputSettings();

    bool openFile();
    /** Shows the scene if a tab has it open, else opens it here or in a new tab as the user chooses. */
    bool openScene(const QString& path);
    /** Loads into the current tab, replacing its scene. */
    bool openPath(const QString& path);
    bool openInNewTab(const QString& path);
    enum class OpenTarget { Here, NewTab, Cancel };
    /** The remembered choice, or asks; name is what the question calls the scene. */
    OpenTarget askOpenTarget(const QString& name);
    void openPresetBrowser();
    /** Opens a built-in preset as a new unsaved scene, in a new tab or, if the current one is empty, there. */
    bool openPreset(const QString& path, bool newTab);
    /** Saves the active scene into My presets with a title, a description and the playhead as its cover. */
    void saveAsPreset();
    void wireOnline();
    void showUpdateOffer();
    /** The file's name, else the preset title, else "Untitled". */
    QString documentName(const QString& path, const Scene& scene) const;
    bool saveFile(bool saveAs);
    /** For the current tab: true when it has no unsaved changes, or they were saved or discarded on request. */
    bool confirmDiscard();
    bool isPristine() const;
    QString dataDir() const;
    void saveSession();
    bool restoreSession();
    bool restoreLegacyAutosave();

    void newTab();
    /** Appends a tab for a parked scene and switches to it. */
    void addTab(std::unique_ptr<Project::Document> doc);
    void addCloseButton(int index);
    void switchTab(int index);
    void closeTab(int index);
    void updateTabs();

    Project* m_project;
    SpaceStationClient* m_client;
    OnlineUpdates* m_online;
    QToolButton* m_updateBtn = nullptr;
    QString m_updateVersion;
    QUrl m_updatePage;
    /** A check the user asked for reports "up to date" and failures; the daily one stays quiet about them. */
    bool m_manualOnline = false;
    HidDock* m_hid;
    bool m_devicesEnabled = true;
    bool m_clientStarted = false;

    DockView* m_dock;
    Timeline* m_timeline;
    EffectLibrary* m_library;
    Inspector* m_inspector;
    ShaderEditor* m_shaderEditor = nullptr;
    PresetBrowser* m_presetBrowser = nullptr;
    QToolButton* m_presetsBtn;

    QLabel* m_statusPill;
    QToolButton* m_newBtn;
    QToolButton* m_openBtn;
    QToolButton* m_saveBtn;
    QToolButton* m_undoBtn;
    QToolButton* m_redoBtn;
    QTabBar* m_tabBar;
    /** One per tab; the active tab's slot stays empty while its scene lives in m_project. */
    std::vector<std::unique_ptr<Project::Document>> m_tabs;
    int m_activeTab = 0;
    QLabel* m_brightnessLabel;
    SliderRow* m_brightness;
    QToolButton* m_settingsBtn;
    ProgressButton* m_sendBtn;

    QWidget* m_toolStrip;
    QToolButton* m_brushBtn;
    QToolButton* m_eraserBtn;
    QToolButton* m_fillBtn;
    QToolButton* m_pickerBtn;
    QToolButton* m_mirrorBtn;
    ColorButton* m_colorBtn;
    QLabel* m_sizeLabel;
    SliderRow* m_sizeSlider;
    QLabel* m_hardLabel;
    SliderRow* m_hardSlider;
    QWidget* m_paintTools;

    QToolButton* m_playBtn;
    QLabel* m_timeLabel;
    QLabel* m_loopLabel;
    QDoubleSpinBox* m_loopSpin;
    QToolButton* m_keyAddBtn;
    QToolButton* m_keyDelBtn;
    QComboBox* m_easing;
    QLabel* m_bakeInfo;
    QTimer m_estimateTimer;
    QElapsedTimer m_uploadClock;
    double m_uploadEstimateSeconds = 0;

    QTimer m_timer;
    QElapsedTimer m_clock;
    bool m_playing = true;
    double m_phase = 0;

    PaintTool m_tool = PaintTool::Brush;
    bool m_maskMode = false;
    QColor m_brushColor{0, 200, 255};
    int m_strokeKey = -1;
    bool m_strokeErase = false;
    int m_selectedKey = -1;
    std::vector<Rgba> m_strokeBase;
    std::vector<float> m_strokeMaskBase;
    std::vector<Rgba> m_keyClipboard;

    int m_maxFrames = 200;
    bool m_dockPreview = false;
    int m_uploadId = -1;
    UploadVia m_uploadVia = UploadVia::None;
};

} // namespace fg
