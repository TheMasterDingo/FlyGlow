#include "MainWindow.h"

#include "AppPaths.h"
#include "Controls.h"
#include "OnlineUpdates.h"
#include "EffectLibrary.h"
#include "Icons.h"
#include "Inspector.h"
#include "Language.h"
#include "MediaImportDialog.h"
#include "PresetBrowser.h"
#include "PresetLibrary.h"
#include "Project.h"
#include "ShaderEditor.h"
#include "Theme.h"
#include "Timeline.h"
#include "Translate.h"
#include "core/LedLayout.h"
#include "core/Scene.h"
#include "device/HidDock.h"
#include "device/SpaceStationClient.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDialog>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QGraphicsOpacityEffect>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <deque>

namespace fg {

namespace {

QToolButton* flatTool(QWidget* parent, icons::Id icon, bool checkable = false)
{
    auto* b = new QToolButton(parent);
    b->setIcon(icons::get(icon));
    b->setIconSize(QSize(18, 18));
    b->setObjectName("Flat");
    b->setCheckable(checkable);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

// Icon-only buttons have no text, so screen readers and UI Automation read the tooltip instead.
void setTip(QWidget* w, const QString& tip)
{
    w->setToolTip(tip);
    w->setAccessibleName(tip);
}

QString secondsText(double s)
{
    return QLocale().toString(s, 'f', 2);
}

/** What File > New makes; a tab still showing it can take the next opened scene without asking. */
QJsonObject defaultSceneJson()
{
    Project p;
    p.newDefault();
    return p.scene().toJson();
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    m_project = new Project(this);
    m_client = new SpaceStationClient(this);
    m_online = new OnlineUpdates(this);
    m_hid = new HidDock(this);
    m_devicesEnabled = qEnvironmentVariableIntValue("FLYGLOW_NO_DEVICE") == 0;

    QSettings settings;
    m_maxFrames = std::clamp(settings.value("output/maxFrames", 200).toInt(), 20, 255);
    m_dockPreview = settings.value("preview/asDock", false).toBool();
    m_brushColor = settings.value("paint/color", QColor(0, 200, 255)).value<QColor>();

    auto* root = new QWidget(this);
    root->setObjectName("Root");
    auto* rv = new QVBoxLayout(root);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(0);
    rv->addWidget(buildHeader());

    auto* split = new QSplitter(Qt::Horizontal, root);
    split->setChildrenCollapsible(false);
    m_library = new EffectLibrary(split);
    m_library->setMinimumWidth(220);

    auto* stage = new QWidget(split);
    stage->setObjectName("Stage");
    stage->setAttribute(Qt::WA_StyledBackground);
    auto* sv = new QVBoxLayout(stage);
    sv->setContentsMargins(0, 0, 0, 0);
    sv->setSpacing(0);
    m_toolStrip = buildToolStrip();
    sv->addWidget(m_toolStrip);
    m_dock = new DockView(stage);
    m_dock->setMirror(m_mirrorBtn->isChecked());
    sv->addWidget(m_dock, 1);
    sv->addWidget(buildTransport());
    m_timeline = new Timeline(stage);
    sv->addWidget(m_timeline);

    m_inspector = new Inspector(m_project, split);
    m_inspector->setMinimumWidth(320);

    split->addWidget(m_library);
    split->addWidget(stage);
    split->addWidget(m_inspector);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setStretchFactor(2, 0);
    split->setSizes({250, 820, 340});
    rv->addWidget(split, 1);
    setCentralWidget(root);

    m_bakeInfo = new QLabel(this);
    statusBar()->addPermanentWidget(m_bakeInfo);

    m_library->reload();
    wireProject();
    wireDevices();
    wireOnline();
    wireShortcuts();
    qApp->installEventFilter(this);

    retranslateUi();

    if (!restoreSession() && !restoreLegacyAutosave())
        m_project->newDefault();
    setAcceptDrops(true);

    m_dock->setBrushSize(1);
    m_dock->setBrushHardness(1.0);
    m_brushBtn->setChecked(true);
    setBrushColor(m_brushColor);
    updateToolState();
    updateKeyUi();
    updateBakeInfo();
    updateTitle();
    updateConnectionUi();

    connect(&m_timer, &QTimer::timeout, this, &MainWindow::tick);
    m_timer.start(16);
    m_clock.start();
    setPlaying(true);
    if (m_devicesEnabled)
        m_hid->start();

    resize(1400, 860);
    restoreGeometry(settings.value("window/geometry").toByteArray());
    // Once the window is up and settled; each check runs at most once a day.
    QTimer::singleShot(5000, m_online, &OnlineUpdates::checkDaily);
}

MainWindow::~MainWindow() = default;

void MainWindow::wireOnline()
{
    connect(m_online, &OnlineUpdates::appUpdateAvailable, this, [this](const QString& version, const QUrl& page) {
        m_updateVersion = version;
        m_updatePage = page;
        m_updateBtn->setText(tr("FlyGlow %1").arg(version));
        m_updateBtn->setVisible(true);
    });
    connect(m_online, &OnlineUpdates::presetsSynced, this, [this](int added, int updated, int removed) {
        if (added + updated + removed > 0) {
            statusBar()->showMessage(tr("Preset library updated: %1 new, %2 changed.").arg(added).arg(updated), 8000);
            if (m_presetBrowser && m_presetBrowser->isVisible())
                m_presetBrowser->reload();
        } else if (m_manualOnline) {
            statusBar()->showMessage(tr("The preset library is up to date."), 4000);
        }
        m_manualOnline = false;
    });
    connect(m_online, &OnlineUpdates::presetSyncFailed, this, [this](const QString& message) {
        if (m_manualOnline)
            QMessageBox::warning(m_presetBrowser && m_presetBrowser->isVisible() ? static_cast<QWidget*>(m_presetBrowser)
                                                                                 : this,
                                 tr("Preset library"), tr("Could not update the preset library: %1").arg(message));
        m_manualOnline = false;
    });
    connect(m_online, &OnlineUpdates::appUpToDate, this, [this] {
        statusBar()->showMessage(tr("FlyGlow %1 is the latest version.").arg(QCoreApplication::applicationVersion()),
                                 5000);
    });
    connect(m_online, &OnlineUpdates::appCheckFailed, this, [this](const QString& message) {
        statusBar()->showMessage(tr("Could not check for updates: %1").arg(message), 8000);
    });
    connect(m_updateBtn, &QToolButton::clicked, this, &MainWindow::showUpdateOffer);
}

void MainWindow::showUpdateOffer()
{
    QMessageBox box(QMessageBox::Information, tr("Update available"),
                    tr("FlyGlow %1 is out; this is %2.\n\nThe download page lists what is new and has the installer "
                       "and the portable zip.")
                        .arg(m_updateVersion, QCoreApplication::applicationVersion()),
                    QMessageBox::NoButton, this);
    QAbstractButton* download = box.addButton(tr("Open the download page"), QMessageBox::AcceptRole);
    QAbstractButton* skip = box.addButton(tr("Skip this version"), QMessageBox::DestructiveRole);
    box.addButton(tr("Later"), QMessageBox::RejectRole);
    box.setDefaultButton(qobject_cast<QPushButton*>(download));
    box.exec();
    if (box.clickedButton() == download) {
        QDesktopServices::openUrl(m_updatePage.isValid() ? m_updatePage
                                                         : QUrl("https://github.com/PsinaDev/FlyGlow/releases/latest"));
    } else if (box.clickedButton() == skip) {
        OnlineUpdates::skipVersion(m_updateVersion);
        m_updateBtn->setVisible(false);
    }
}

void MainWindow::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
    QMainWindow::changeEvent(e);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* e)
{
    // A click on anything that cannot take focus itself (labels, panels, the dock) drops focus from editors.
    if (e->type() == QEvent::MouseButtonPress) {
        auto* target = qobject_cast<QWidget*>(watched);
        QWidget* focused = QApplication::focusWidget();
        if (target && focused && target->window() == this && !(target->focusPolicy() & Qt::ClickFocus)
            && target != focused && !focused->isAncestorOf(target) && !target->isAncestorOf(focused))
            focused->clearFocus();
    } else if (e->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(e)->key();
        auto* editor = qobject_cast<QWidget*>(watched);
        if ((key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Escape) && editor
            && editor->window() == this
            && (qobject_cast<QLineEdit*>(editor) || qobject_cast<QAbstractSpinBox*>(editor)))
            QTimer::singleShot(0, editor, [editor] { editor->clearFocus(); });
    }
    return QMainWindow::eventFilter(watched, e);
}

void MainWindow::retranslateUi()
{
    setTip(m_newBtn, tr("New scene in a new tab (Ctrl+N)"));
    m_presetsBtn->setText(tr("Presets"));
    m_presetsBtn->setToolTip(tr("Ready-made scenes to start from (Ctrl+P)"));
    setTip(m_openBtn, tr("Open… (Ctrl+O)"));
    setTip(m_saveBtn, tr("Save (Ctrl+S)"));
    setTip(m_undoBtn, tr("Undo (Ctrl+Z)"));
    setTip(m_redoBtn, tr("Redo (Ctrl+Y)"));
    m_brightnessLabel->setText(tr("Brightness"));
    setTip(m_settingsBtn, tr("Output settings"));
    if (m_uploadId < 0)
        m_sendBtn->setText(tr("Send to dock"));

    setTip(m_brushBtn, tr("Brush (B)"));
    setTip(m_eraserBtn, tr("Eraser (E)"));
    setTip(m_fillBtn, tr("Fill similar neighbouring LEDs (G)"));
    setTip(m_pickerBtn, tr("Eyedropper (I). Alt+click picks a colour with any tool"));
    setTip(m_mirrorBtn, tr("Mirror: brush, eraser and fill also work on the other half of the dock (M)"));
    m_sizeLabel->setText(tr("Size"));
    m_sizeSlider->setToolTip(tr("1 — exactly one LED, 2 — the LED and its six neighbours, and so on"));
    m_hardLabel->setText(tr("Hardness"));

    setTip(m_playBtn, tr("Pause / play (Space)"));
    m_loopLabel->setText(tr("Loop"));
    m_loopSpin->setSuffix(QLatin1Char(' ') + tr("s"));
    m_loopSpin->setToolTip(tr("Length of the animation loop. The dock repeats it endlessly."));
    setTip(m_keyAddBtn, tr("Add a keyframe at the playhead (K)"));
    setTip(m_keyDelBtn, tr("Delete the selected keyframe (Del)"));
    const QStringList easings = {tr("Smooth"), tr("Linear"), tr("Step")};
    for (int i = 0; i < easings.size(); ++i)
        m_easing->setItemText(i, easings[i]);
    m_easing->setToolTip(tr("How colours flow from the selected keyframe to the next one"));

    updateToolState();
    updateBakeInfo();
    updateConnectionUi();
    updateTitle();
    renderPreview();
}

void MainWindow::switchLanguage(const QString& language)
{
    // Layers still named after their effect follow the language; names the user typed stay.
    std::vector<Layer*> defaultNamed;
    for (const auto& layer : m_project->scene().layers)
        if (layer->effect && (layer->name == layer->effect->name() || layer->name == trEffect(layer->effect->name())))
            defaultNamed.push_back(layer.get());
    QSettings().setValue("ui/language", language);
    installLanguage(language);
    for (Layer* layer : defaultNamed)
        layer->name = trEffect(layer->effect->name());
    m_project->refreshLayerNames();
}

void MainWindow::syncSettingsUi()
{
    const Scene& s = m_project->scene();
    {
        const QSignalBlocker b(m_loopSpin);
        m_loopSpin->setValue(s.loopSeconds);
    }
    m_brightness->setValue(s.brightness);
    m_timeline->setLoopSeconds(s.loopSeconds);
    updateBakeInfo();
    renderPreview();
}

QWidget* MainWindow::buildHeader()
{
    auto* bar = new QWidget(this);
    bar->setObjectName("HeaderBar");
    bar->setAttribute(Qt::WA_StyledBackground);
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(14, 8, 14, 8);
    h->setSpacing(8);

    auto* logo = new QLabel(bar);
    logo->setPixmap(icons::get(icons::Id::Dock, theme::kAccent).pixmap(22, 22));
    auto* title = new QLabel(QStringLiteral("FlyGlow"), bar);
    title->setObjectName("AppTitle");
    h->addWidget(logo);
    h->addWidget(title);
    h->addSpacing(10);

    m_newBtn = flatTool(bar, icons::Id::New);
    m_openBtn = flatTool(bar, icons::Id::Open);
    m_saveBtn = flatTool(bar, icons::Id::Save);
    m_undoBtn = flatTool(bar, icons::Id::Undo);
    m_redoBtn = flatTool(bar, icons::Id::Redo);
    for (QToolButton* b : {m_newBtn, m_openBtn, m_saveBtn})
        h->addWidget(b);
    m_presetsBtn = flatTool(bar, icons::Id::Presets);
    m_presetsBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    h->addWidget(m_presetsBtn);
    h->addSpacing(6);
    h->addWidget(m_undoBtn);
    h->addWidget(m_redoBtn);
    h->addSpacing(10);

    m_tabBar = new QTabBar(bar);
    m_tabBar->setObjectName("SceneTabs");
    m_tabBar->setDocumentMode(true);
    m_tabBar->setDrawBase(false);
    m_tabBar->setExpanding(false);
    m_tabBar->setMovable(true);
    m_tabBar->setElideMode(Qt::ElideRight);
    m_tabBar->setUsesScrollButtons(true);
    m_tabBar->setFocusPolicy(Qt::NoFocus);
    m_tabs.push_back(std::make_unique<Project::Document>());
    m_tabBar->addTab(QString());
    addCloseButton(0);
    h->addWidget(m_tabBar, 1);
    connect(m_tabBar, &QTabBar::currentChanged, this, &MainWindow::switchTab);
    connect(m_tabBar, &QTabBar::tabMoved, this, [this](int from, int to) {
        auto doc = std::move(m_tabs[size_t(from)]);
        m_tabs.erase(m_tabs.begin() + from);
        m_tabs.insert(m_tabs.begin() + to, std::move(doc));
        m_activeTab = m_tabBar->currentIndex();
    });

    m_updateBtn = new QToolButton(bar);
    m_updateBtn->setObjectName("Update");
    m_updateBtn->setCursor(Qt::PointingHandCursor);
    m_updateBtn->setVisible(false);
    h->addWidget(m_updateBtn);
    m_statusPill = new QLabel(bar);
    m_statusPill->setObjectName("StatusPill");
    h->addWidget(m_statusPill);
    h->addSpacing(10);

    m_brightnessLabel = new QLabel(bar);
    m_brightnessLabel->setObjectName("Dim");
    h->addWidget(m_brightnessLabel);
    m_brightness = new SliderRow(bar);
    m_brightness->setRange(1, 100, 1, "%");
    m_brightness->setFixedWidth(200);
    h->addWidget(m_brightness);

    m_settingsBtn = flatTool(bar, icons::Id::Settings);
    h->addWidget(m_settingsBtn);

    m_sendBtn = new ProgressButton(icons::get(icons::Id::Send, QColor(0x06, 0x10, 0x18)), QString(), bar);
    m_sendBtn->setObjectName("Primary");
    m_sendBtn->setCursor(Qt::PointingHandCursor);
    h->addWidget(m_sendBtn);

    connect(m_newBtn, &QToolButton::clicked, this, &MainWindow::newTab);
    connect(m_presetsBtn, &QToolButton::clicked, this, &MainWindow::openPresetBrowser);
    connect(m_openBtn, &QToolButton::clicked, this, &MainWindow::openFile);
    connect(m_saveBtn, &QToolButton::clicked, this, [this] { saveFile(false); });
    connect(m_undoBtn, &QToolButton::clicked, m_project, &Project::undo);
    connect(m_redoBtn, &QToolButton::clicked, m_project, &Project::redo);
    connect(m_settingsBtn, &QToolButton::clicked, this, &MainWindow::showOutputSettings);
    connect(m_sendBtn, &QPushButton::clicked, this, &MainWindow::sendToDock);
    connect(m_brightness, &SliderRow::editStarted, m_project, &Project::checkpoint);
    connect(m_brightness, &SliderRow::valueChanged, this, [this](double v) {
        m_project->scene().brightness = int(std::lround(v));
        m_project->notifySettingsChanged();
    });
    return bar;
}

QWidget* MainWindow::buildToolStrip()
{
    auto* strip = new QWidget(this);
    auto* outer = new QHBoxLayout(strip);
    outer->setContentsMargins(14, 10, 14, 4);
    m_paintTools = new QWidget(strip);
    m_paintTools->setGraphicsEffect(new QGraphicsOpacityEffect(m_paintTools));
    outer->addWidget(m_paintTools);
    outer->addStretch(1);
    auto* h = new QHBoxLayout(m_paintTools);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);

    m_brushBtn = flatTool(m_paintTools, icons::Id::Brush, true);
    m_eraserBtn = flatTool(m_paintTools, icons::Id::Eraser, true);
    m_fillBtn = flatTool(m_paintTools, icons::Id::Fill, true);
    m_pickerBtn = flatTool(m_paintTools, icons::Id::Picker, true);
    for (QToolButton* b : {m_brushBtn, m_eraserBtn, m_fillBtn, m_pickerBtn})
        h->addWidget(b);
    h->addSpacing(6);
    m_mirrorBtn = flatTool(m_paintTools, icons::Id::Mirror, true);
    m_mirrorBtn->setChecked(QSettings().value("paint/mirror", false).toBool());
    h->addWidget(m_mirrorBtn);
    h->addSpacing(6);
    m_colorBtn = new ColorButton(m_paintTools);
    m_colorBtn->setFixedSize(46, 28);
    h->addWidget(m_colorBtn);
    h->addSpacing(12);

    m_sizeLabel = new QLabel(m_paintTools);
    m_sizeLabel->setObjectName("Dim");
    h->addWidget(m_sizeLabel);
    m_sizeSlider = new SliderRow(m_paintTools);
    m_sizeSlider->setRange(1, 6, 1);
    m_sizeSlider->setValue(1);
    m_sizeSlider->setFixedWidth(170);
    h->addWidget(m_sizeSlider);
    m_hardLabel = new QLabel(m_paintTools);
    m_hardLabel->setObjectName("Dim");
    h->addWidget(m_hardLabel);
    m_hardSlider = new SliderRow(m_paintTools);
    m_hardSlider->setRange(0, 100, 5, "%");
    m_hardSlider->setValue(100);
    m_hardSlider->setFixedWidth(170);
    h->addWidget(m_hardSlider);

    connect(m_brushBtn, &QToolButton::clicked, this, [this] { selectTool(PaintTool::Brush); });
    connect(m_eraserBtn, &QToolButton::clicked, this, [this] { selectTool(PaintTool::Eraser); });
    connect(m_fillBtn, &QToolButton::clicked, this, [this] { selectTool(PaintTool::Fill); });
    connect(m_pickerBtn, &QToolButton::clicked, this, [this] { selectTool(PaintTool::Picker); });
    connect(m_mirrorBtn, &QToolButton::toggled, this, [this](bool on) {
        m_dock->setMirror(on);
        QSettings().setValue("paint/mirror", on);
    });
    connect(m_colorBtn, &ColorButton::colorChanged, this, &MainWindow::setBrushColor);
    connect(m_sizeSlider, &SliderRow::valueChanged, this, [this](double v) { m_dock->setBrushSize(int(std::lround(v))); });
    connect(m_hardSlider, &SliderRow::valueChanged, this, [this](double v) { m_dock->setBrushHardness(v / 100.0); });
    return strip;
}

QWidget* MainWindow::buildTransport()
{
    auto* row = new QWidget(this);
    row->setObjectName("Panel");
    row->setAttribute(Qt::WA_StyledBackground);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(14, 8, 14, 0);
    h->setSpacing(8);

    m_playBtn = flatTool(row, icons::Id::Pause);
    h->addWidget(m_playBtn);
    m_timeLabel = new QLabel(row);
    m_timeLabel->setMinimumWidth(110);
    h->addWidget(m_timeLabel);
    h->addSpacing(8);

    m_loopLabel = new QLabel(row);
    m_loopLabel->setObjectName("Dim");
    h->addWidget(m_loopLabel);
    m_loopSpin = new QDoubleSpinBox(row);
    m_loopSpin->setRange(0.5, 60);
    m_loopSpin->setSingleStep(0.5);
    m_loopSpin->setDecimals(1);
    m_loopSpin->setKeyboardTracking(false);
    m_loopSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_loopSpin->setFixedWidth(70);
    h->addWidget(m_loopSpin);
    h->addSpacing(16);

    m_keyAddBtn = flatTool(row, icons::Id::KeyAdd);
    m_keyDelBtn = flatTool(row, icons::Id::KeyRemove);
    h->addWidget(m_keyAddBtn);
    h->addWidget(m_keyDelBtn);
    m_easing = new QComboBox(row);
    m_easing->addItems({QString(), QString(), QString()});
    h->addWidget(m_easing);
    h->addStretch(1);

    connect(m_playBtn, &QToolButton::clicked, this, [this] { setPlaying(!m_playing); });
    connect(m_loopSpin, &QDoubleSpinBox::valueChanged, this, [this](double v) {
        if (std::abs(v - m_project->scene().loopSeconds) < 1e-9)
            return;
        m_project->checkpoint();
        m_project->scene().loopSeconds = v;
        m_project->notifySettingsChanged();
    });
    connect(m_keyAddBtn, &QToolButton::clicked, this, &MainWindow::addKeyAtPlayhead);
    connect(m_keyDelBtn, &QToolButton::clicked, this, &MainWindow::removeSelectedKey);
    connect(m_easing, &QComboBox::activated, this, [this](int idx) {
        KeyframeEffect* kf = currentKeys();
        if (!kf || m_selectedKey < 0)
            return;
        m_project->checkpoint();
        kf->setEasing(m_selectedKey, KeyframeEffect::Easing(idx));
        m_project->notifyLayerChanged(m_project->currentIndex());
    });
    return row;
}

void MainWindow::wireProject()
{
    connect(m_project, &Project::layersChanged, this, [this] {
        updateToolState();
        updateKeyUi();
        updateBakeInfo();
        renderPreview();
    });
    connect(m_project, &Project::layerChanged, this, [this] {
        updateKeyUi();
        updateBakeInfo();
        if (m_maskMode && m_project->currentLayer())
            m_dock->setMaskOverlay(&m_project->currentLayer()->mask);
        renderPreview();
    });
    connect(m_project, &Project::currentChanged, this, [this] {
        m_selectedKey = -1;
        updateToolState();
        updateKeyUi();
        renderPreview();
    });
    connect(m_project, &Project::settingsChanged, this, &MainWindow::syncSettingsUi);
    connect(m_project, &Project::modifiedChanged, this, &MainWindow::updateTitle);
    connect(m_project, &Project::undoStateChanged, this, [this] {
        m_undoBtn->setEnabled(m_project->canUndo());
        m_redoBtn->setEnabled(m_project->canRedo());
    });

    connect(m_library, &EffectLibrary::addRequested, this, [this](const QString& t) {
        m_project->checkpoint();
        m_project->addEffectLayer(t);
    });
    connect(m_library, &EffectLibrary::replaceRequested, this, &MainWindow::replaceCurrentEffect);
    connect(m_library, &EffectLibrary::importRequested, this, &MainWindow::importMedia);
    connect(m_library, &EffectLibrary::shaderRequested, this, &MainWindow::addShaderLayer);
    connect(m_inspector, &Inspector::shaderEditRequested, this, &MainWindow::openShaderEditor);

    connect(m_inspector, &Inspector::maskPaintToggled, this, [this](bool on) {
        m_maskMode = on;
        if (on && (m_tool == PaintTool::Fill || m_tool == PaintTool::Picker))
            m_tool = PaintTool::Brush;
        updateToolState();
    });

    connect(m_dock, &DockView::strokeBegan, this, &MainWindow::onStrokeBegan);
    connect(m_dock, &DockView::painted, this, &MainWindow::onPainted);
    connect(m_dock, &DockView::strokeEnded, this, [this] { m_strokeKey = -1; });
    connect(m_dock, &DockView::fillAt, this, &MainWindow::onFill);
    connect(m_dock, &DockView::pickAt, this, &MainWindow::onPick);

    connect(m_timeline, &Timeline::scrubStarted, this, [this] { setPlaying(false); });
    connect(m_timeline, &Timeline::phaseScrubbed, this, [this](double ph) { setPhase(ph); });
    connect(m_timeline, &Timeline::keySelected, this, [this](int i) {
        KeyframeEffect* kf = currentKeys();
        if (!kf || i < 0 || i >= kf->keyCount())
            return;
        m_selectedKey = i;
        setPlaying(false);
        setPhase(kf->key(i).time);
        updateKeyUi();
    });
    connect(m_timeline, &Timeline::keyDragStarted, m_project, &Project::checkpoint);
    connect(m_timeline, &Timeline::keyMoved, this, [this](int i, double t) {
        KeyframeEffect* kf = currentKeys();
        if (!kf)
            return;
        m_selectedKey = kf->moveKey(i, t);
        setPhase(kf->key(m_selectedKey).time);
        m_project->notifyLayerChanged(m_project->currentIndex());
    });
    connect(m_timeline, &Timeline::keyContextMenu, this, &MainWindow::showKeyMenu);
    connect(m_timeline, &Timeline::contextMenuRequested, this, &MainWindow::showTimelineMenu);
}

void MainWindow::wireShortcuts()
{
    auto shortcut = [this](const QKeySequence& seq, auto fn) {
        auto* s = new QShortcut(seq, this);
        s->setContext(Qt::WindowShortcut);
        connect(s, &QShortcut::activated, this, fn);
    };
    shortcut(QKeySequence::Undo, [this] { m_project->undo(); });
    shortcut(QKeySequence("Ctrl+Y"), [this] { m_project->redo(); });
    shortcut(QKeySequence("Ctrl+Shift+Z"), [this] { m_project->redo(); });
    shortcut(QKeySequence::Save, [this] { saveFile(false); });
    shortcut(QKeySequence("Ctrl+Shift+S"), [this] { saveFile(true); });
    shortcut(QKeySequence::Open, [this] { openFile(); });
    shortcut(QKeySequence("Ctrl+P"), [this] { openPresetBrowser(); });
    shortcut(QKeySequence("Ctrl+Shift+P"), [this] { saveAsPreset(); });
    shortcut(QKeySequence::New, [this] { newTab(); });
    shortcut(QKeySequence("Ctrl+T"), [this] { newTab(); });
    shortcut(QKeySequence("Ctrl+W"), [this] { closeTab(m_activeTab); });
    shortcut(QKeySequence("Ctrl+Tab"), [this] { switchTab((m_activeTab + 1) % int(m_tabs.size())); });
    shortcut(QKeySequence("Ctrl+Shift+Tab"),
             [this] { switchTab((m_activeTab + int(m_tabs.size()) - 1) % int(m_tabs.size())); });
    // Text fields claim plain keys through ShortcutOverride, so these stay out of the way while typing.
    shortcut(QKeySequence(Qt::Key_Space), [this] { setPlaying(!m_playing); });
    shortcut(QKeySequence(Qt::Key_B), [this] { selectTool(PaintTool::Brush); });
    shortcut(QKeySequence(Qt::Key_E), [this] { selectTool(PaintTool::Eraser); });
    shortcut(QKeySequence(Qt::Key_G), [this] { selectTool(PaintTool::Fill); });
    shortcut(QKeySequence(Qt::Key_I), [this] { selectTool(PaintTool::Picker); });
    shortcut(QKeySequence(Qt::Key_M), [this] { m_mirrorBtn->toggle(); });
    shortcut(QKeySequence(Qt::Key_K), [this] { addKeyAtPlayhead(); });
    shortcut(QKeySequence(Qt::Key_Delete), [this] { removeSelectedKey(); });
    shortcut(QKeySequence(Qt::Key_BracketLeft), [this] {
        m_sizeSlider->setValue(std::max(1.0, m_sizeSlider->value() - 1));
        m_dock->setBrushSize(int(m_sizeSlider->value()));
    });
    shortcut(QKeySequence(Qt::Key_BracketRight), [this] {
        m_sizeSlider->setValue(std::min(6.0, m_sizeSlider->value() + 1));
        m_dock->setBrushSize(int(m_sizeSlider->value()));
    });
    shortcut(QKeySequence(Qt::Key_Escape), [this] {
        if (m_maskMode) {
            m_maskMode = false;
            updateToolState();
        }
    });
}

void MainWindow::selectTool(PaintTool t)
{
    m_tool = t;
    updateToolState();
}

void MainWindow::wireDevices()
{
    connect(m_hid, &HidDock::presenceChanged, this, [this](bool present) {
        // The service pipe is only a fallback: polling it would compete with Space Station's own window.
        if (!present && !m_clientStarted && m_devicesEnabled) {
            m_clientStarted = true;
            m_client->start();
        }
        updateConnectionUi();
    });
    connect(m_hid, &HidDock::uploadProgress, this, [this](int id, double f) {
        if (m_uploadVia == UploadVia::Hid && id == m_uploadId)
            setUploadProgress(f);
    });
    connect(m_hid, &HidDock::uploadFinished, this, [this](int id, bool ok, const QString& message) {
        if (m_uploadVia == UploadVia::Hid && id == m_uploadId)
            finishUpload(ok, message);
    });

    connect(m_client, &SpaceStationClient::connectedChanged, this, &MainWindow::updateConnectionUi);
    connect(m_client, &SpaceStationClient::devicesChanged, this, &MainWindow::updateConnectionUi);
    connect(m_client, &SpaceStationClient::uploadProgress, this, [this](int id, double f) {
        if (m_uploadVia == UploadVia::Service && id == m_uploadId)
            setUploadProgress(f);
    });
    // The service reports nothing until it is done, so its fill follows the expected upload time.
    m_estimateTimer.setInterval(100);
    connect(&m_estimateTimer, &QTimer::timeout, this, [this] {
        if (m_uploadEstimateSeconds > 0)
            setUploadProgress(std::min(0.95, m_uploadClock.elapsed() / 1000.0 / m_uploadEstimateSeconds));
    });
    connect(m_client, &SpaceStationClient::uploadFinished, this, [this](int id, bool ok, const QString& message) {
        if (m_uploadVia == UploadVia::Service && id == m_uploadId)
            finishUpload(ok, message);
    });
    connect(m_client, &SpaceStationClient::logMessage, this, [](const QString& line) {
        qInfo("[ipc] %s", qUtf8Printable(line));
    });
}

void MainWindow::finishUpload(bool ok, const QString& message)
{
    m_uploadId = -1;
    m_uploadVia = UploadVia::None;
    m_estimateTimer.stop();
    m_uploadEstimateSeconds = 0;
    m_sendBtn->setProgress(-1);
    m_sendBtn->setText(tr("Send to dock"));
    updateConnectionUi();
    if (ok)
        statusBar()->showMessage(tr("Done: the animation is uploaded to the dock."), 8000);
    else
        QMessageBox::warning(this, tr("Could not send"), message);
}

void MainWindow::setUploadProgress(double fraction)
{
    m_sendBtn->setProgress(fraction);
    m_sendBtn->setText(tr("Sending… %1%").arg(int(std::lround(fraction * 100))));
}

void MainWindow::tick()
{
    const double dt = m_clock.restart() / 1000.0;
    if (m_playing) {
        const double loop = std::max(0.1, m_project->scene().loopSeconds);
        const double next = m_phase + dt / loop;
        if (m_timeline->hasSelection()) {
            // Playback loops inside the selection, to judge a cut before trimming to it.
            const double from = m_timeline->selectionFrom(), to = m_timeline->selectionTo();
            m_phase = next >= from && next < to ? next : from + std::fmod(std::max(0.0, next - from), to - from);
        } else {
            m_phase = std::fmod(next, 1.0);
        }
        m_timeline->setPhase(m_phase);
        renderPreview();
    }
}

void MainWindow::setPlaying(bool on)
{
    m_playing = on;
    m_playBtn->setIcon(icons::get(on ? icons::Id::Pause : icons::Id::Play));
    m_clock.restart();
    if (m_dockPreview)
        renderPreview();   // paused, the dock preview shows the playhead rather than the start of its frame
}

void MainWindow::setPhase(double phase)
{
    m_phase = phase - std::floor(phase);
    m_timeline->setPhase(m_phase);
    renderPreview();
}

void MainWindow::renderPreview()
{
    std::vector<Rgb> out;
    const Scene& scene = m_project->scene();
    if (m_dockPreview) {
        int frames = 1, period = 1;
        bakeTiming(scene, m_maxFrames, &frames, &period);
        // Each frame holds for its whole step. The epsilon keeps a playhead parked on a frame boundary
        // (0.29 * 100 = 28.999...) on that frame.
        const int frame = std::min(frames - 1, int(m_phase * frames + 1e-6));
        // Paused, the preview shows the playhead itself: a keyframe between two dock frames is what is being edited.
        scene.render(m_playing ? double(frame) / frames : m_phase, LedLayout::cd2(), out);
        std::vector<uchar> codes(out.size() * 3);
        encodeFrame(out, scene.outputGamma, codes.data(), scene.darkLift);
        auto light = [](uchar v) { return v <= kDockDarkValue ? 0.f : float(std::pow(v / 255.0, kDockGamma)); };
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = {light(codes[i * 3]), light(codes[i * 3 + 1]), light(codes[i * 3 + 2])};
    } else {
        scene.render(m_phase, LedLayout::cd2(), out);
    }
    const float k = float(scene.brightness) / 100.f;
    if (k < 0.999f)
        for (Rgb& c : out)
            c = {c.r * k, c.g * k, c.b * k};
    m_dock->setColors(out);
    m_timeLabel->setText(tr("%1 / %2 s").arg(secondsText(m_phase * scene.loopSeconds), secondsText(scene.loopSeconds)));
}

KeyframeEffect* MainWindow::currentKeys()
{
    Layer* l = m_project->currentLayer();
    return l ? dynamic_cast<KeyframeEffect*>(l->effect.get()) : nullptr;
}

void MainWindow::updateToolState()
{
    Layer* layer = m_project->currentLayer();
    if (!layer && m_maskMode)
        m_maskMode = false;
    m_inspector->setMaskPainting(m_maskMode);
    const bool keys = currentKeys() != nullptr;
    const bool canPaint = keys || m_maskMode;

    m_brushBtn->setChecked(m_tool == PaintTool::Brush);
    m_eraserBtn->setChecked(m_tool == PaintTool::Eraser);
    m_fillBtn->setChecked(m_tool == PaintTool::Fill);
    m_pickerBtn->setChecked(m_tool == PaintTool::Picker);
    m_paintTools->setEnabled(canPaint);
    static_cast<QGraphicsOpacityEffect*>(m_paintTools->graphicsEffect())->setOpacity(canPaint ? 1.0 : 0.3);
    m_fillBtn->setEnabled(!m_maskMode);
    m_pickerBtn->setEnabled(!m_maskMode);
    m_colorBtn->setEnabled(!m_maskMode);

    DockView::Tool t = DockView::Tool::None;
    if (m_maskMode) {
        t = m_tool == PaintTool::Eraser ? DockView::Tool::MaskErase : DockView::Tool::MaskPaint;
        m_dock->setHint(tr("Left click adds LEDs to the layer's area · right click removes them · Esc leaves area mode"));
    } else if (keys) {
        switch (m_tool) {
        case PaintTool::Brush: t = DockView::Tool::Brush; break;
        case PaintTool::Eraser: t = DockView::Tool::Eraser; break;
        case PaintTool::Fill: t = DockView::Tool::Fill; break;
        case PaintTool::Picker: t = DockView::Tool::Picker; break;
        }
        m_dock->setHint(tr("Left click paints · right click erases · Alt+click picks a colour · [ ] brush size"));
    } else {
        m_dock->setHint(QString());
    }
    m_dock->setTool(t);
    m_dock->setMaskOverlay(m_maskMode && layer ? &layer->mask : nullptr);
    m_library->setReplaceEnabled(layer != nullptr);
}

void MainWindow::setBrushColor(const QColor& c)
{
    m_brushColor = c;
    m_colorBtn->setColor(c);
    QSettings().setValue("paint/color", c);
}

void MainWindow::onStrokeBegan(bool erase)
{
    setPlaying(false);
    m_project->checkpoint();
    m_strokeKey = -1;
    m_strokeErase = erase || m_tool == PaintTool::Eraser;
    if (m_maskMode) {
        Layer* l = m_project->currentLayer();
        // An empty mask means the whole dock: painting starts from nothing, erasing from everything.
        if (l && l->mask.empty())
            l->mask.assign(size_t(LedLayout::cd2().count()), m_strokeErase ? 1.f : 0.f);
        m_strokeMaskBase = l ? l->mask : std::vector<float>{};
        return;
    }
    if (KeyframeEffect* kf = currentKeys()) {
        m_strokeKey = kf->addKey(m_phase);
        m_selectedKey = m_strokeKey;
        m_strokeBase = kf->key(m_strokeKey).colors;
        setPhase(kf->key(m_strokeKey).time);
        updateKeyUi();
    }
}

void MainWindow::onPainted(const QVector<int>& leds, const QVector<float>& weights)
{
    Layer* l = m_project->currentLayer();
    if (!l)
        return;
    if (m_maskMode) {
        if (l->mask.empty() || m_strokeMaskBase.size() != l->mask.size())
            return;
        for (int k = 0; k < leds.size(); ++k) {
            const size_t i = size_t(leds[k]);
            l->mask[i] = m_strokeErase ? std::min(m_strokeMaskBase[i], 1.f - weights[k])
                                       : std::max(m_strokeMaskBase[i], weights[k]);
        }
        m_project->notifyLayerChanged(m_project->currentIndex());
        return;
    }
    KeyframeEffect* kf = currentKeys();
    if (!kf || m_strokeKey < 0 || m_strokeBase.empty())
        return;
    const Rgba brush(color::fromQColor(m_brushColor), 1.f);
    for (int k = 0; k < leds.size(); ++k) {
        const Rgba base = m_strokeBase[size_t(leds[k])];
        const float w = weights[k];
        Rgba next;
        if (m_strokeErase)
            next = Rgba(base.r, base.g, base.b, base.a * (1.f - w));
        else
            next = w >= 0.999f ? brush : color::mix(base, brush, w);
        kf->setLed(m_strokeKey, leds[k], next);
    }
    m_project->notifyLayerChanged(m_project->currentIndex());
}

void MainWindow::onFill(int led)
{
    KeyframeEffect* kf = currentKeys();
    if (!kf || led < 0)
        return;
    setPlaying(false);
    m_project->checkpoint();
    const int key = kf->addKey(m_phase);
    m_selectedKey = key;
    const auto& colors = kf->key(key).colors;
    const LedLayout& L = LedLayout::cd2();
    std::vector<int> region;
    auto flood = [&](int seed) {
        const Rgba target = colors[size_t(seed)];
        auto similar = [&](const Rgba& c) {
            if (target.a < 0.05f && c.a < 0.05f)
                return true;
            return std::abs(c.r - target.r) + std::abs(c.g - target.g) + std::abs(c.b - target.b) < 0.03f
                   && std::abs(c.a - target.a) < 0.05f;
        };
        std::vector<char> seen(size_t(L.count()), 0);
        std::deque<int> queue{seed};
        seen[size_t(seed)] = 1;
        while (!queue.empty()) {
            const int i = queue.front();
            queue.pop_front();
            region.push_back(i);
            for (int n : L.neighbors(i))
                if (!seen[size_t(n)] && similar(colors[size_t(n)])) {
                    seen[size_t(n)] = 1;
                    queue.push_back(n);
                }
        }
    };
    // Both regions are found before anything is painted, so the mirrored seed still sees the original colours.
    flood(led);
    if (m_mirrorBtn->isChecked())
        flood(L.mirrorOf(led));
    const Rgba brush(color::fromQColor(m_brushColor), 1.f);
    for (int i : region)
        kf->setLed(key, i, brush);
    setPhase(kf->key(key).time);
    m_project->notifyLayerChanged(m_project->currentIndex());
}

void MainWindow::onPick(int led)
{
    if (led < 0 || size_t(led) >= m_dock->colors().size())
        return;
    setBrushColor(color::toQColor(m_dock->colors()[size_t(led)]));
    if (m_tool == PaintTool::Picker) {
        m_tool = PaintTool::Brush;
        updateToolState();
    }
}

void MainWindow::updateKeyUi()
{
    KeyframeEffect* kf = currentKeys();
    QVector<double> times;
    if (kf) {
        for (const auto& k : kf->keys())
            times.push_back(k.time);
        if (m_selectedKey >= kf->keyCount())
            m_selectedKey = kf->keyCount() - 1;
    } else {
        m_selectedKey = -1;
    }
    m_timeline->setKeys(times, m_selectedKey);
    m_timeline->setLoopSeconds(m_project->scene().loopSeconds);
    m_keyAddBtn->setVisible(kf != nullptr);
    m_keyDelBtn->setVisible(kf != nullptr);
    m_easing->setVisible(kf != nullptr);
    m_keyDelBtn->setEnabled(kf && m_selectedKey >= 0 && kf->keyCount() > 1);
    m_easing->setEnabled(kf && m_selectedKey >= 0);
    if (kf && m_selectedKey >= 0) {
        const QSignalBlocker b(m_easing);
        m_easing->setCurrentIndex(int(kf->key(m_selectedKey).easing));
    }
}

void MainWindow::addKeyAtPlayhead()
{
    KeyframeEffect* kf = currentKeys();
    if (!kf)
        return;
    m_project->checkpoint();
    m_selectedKey = kf->addKey(m_phase);
    setPlaying(false);
    m_project->notifyLayerChanged(m_project->currentIndex());
}

void MainWindow::removeSelectedKey()
{
    KeyframeEffect* kf = currentKeys();
    if (!kf || m_selectedKey < 0 || kf->keyCount() <= 1)
        return;
    m_project->checkpoint();
    kf->removeKey(m_selectedKey);
    m_selectedKey = -1;
    m_project->notifyLayerChanged(m_project->currentIndex());
}

void MainWindow::showKeyMenu(int index, const QPoint& globalPos)
{
    KeyframeEffect* kf = currentKeys();
    if (!kf || index < 0 || index >= kf->keyCount())
        return;
    m_selectedKey = index;
    updateKeyUi();
    QMenu menu(this);
    menu.addAction(tr("Copy keyframe"), this, [this, kf, index] { m_keyClipboard = kf->key(index).colors; });
    QAction* paste = menu.addAction(tr("Paste into this keyframe"), this, [this, kf, index] {
        m_project->checkpoint();
        for (size_t i = 0; i < m_keyClipboard.size(); ++i)
            kf->setLed(index, int(i), m_keyClipboard[i]);
        m_project->notifyLayerChanged(m_project->currentIndex());
    });
    paste->setEnabled(!m_keyClipboard.empty());
    menu.addAction(tr("Clear keyframe"), this, [this, kf, index] {
        m_project->checkpoint();
        kf->fill(index, Rgba(0, 0, 0, 0));
        m_project->notifyLayerChanged(m_project->currentIndex());
    });
    menu.addAction(tr("Fill keyframe with the brush colour"), this, [this, kf, index] {
        m_project->checkpoint();
        kf->fill(index, Rgba(color::fromQColor(m_brushColor), 1.f));
        m_project->notifyLayerChanged(m_project->currentIndex());
    });
    menu.addSeparator();
    QAction* del = menu.addAction(tr("Delete keyframe"), this, &MainWindow::removeSelectedKey);
    del->setEnabled(kf->keyCount() > 1);
    menu.exec(globalPos);
}

void MainWindow::showTimelineMenu(double phase, const QPoint& globalPos)
{
    QMenu menu(this);
    const bool selected = m_timeline->hasSelection();
    QAction* trim = menu.addAction(tr("Trim the loop to the selection"), this, &MainWindow::trimToSelection);
    trim->setEnabled(selected);
    QAction* clear = menu.addAction(tr("Clear selection"), m_timeline, &Timeline::clearSelection);
    clear->setEnabled(selected);
    if (!selected) {
        menu.addSeparator();
        QAction* hint = menu.addAction(tr("Drag below the ruler to select part of the loop"));
        hint->setEnabled(false);
    }
    Q_UNUSED(phase);
    menu.exec(globalPos);
}

void MainWindow::trimToSelection()
{
    if (!m_timeline->hasSelection())
        return;
    const double from = m_timeline->selectionFrom(), to = m_timeline->selectionTo();
    m_project->checkpoint();
    m_project->scene().trim(from, to);
    m_timeline->clearSelection();
    m_selectedKey = -1;
    m_project->notifyLayersChanged();
    m_project->notifySettingsChanged();
    setPhase(0);
    statusBar()->showMessage(tr("The loop now runs %1 s: only the selected part is left.")
                                 .arg(secondsText(m_project->scene().loopSeconds)),
                             5000);
}

void MainWindow::replaceCurrentEffect(const QString& typeId)
{
    Layer* l = m_project->currentLayer();
    if (!l)
        return;
    auto effect = EffectRegistry::instance().create(typeId);
    if (!effect)
        return;
    m_project->checkpoint();
    const bool defaultName = !l->effect || l->name.isEmpty() || l->name == trEffect(l->effect->name())
                             || l->name == l->effect->name();
    if (defaultName)
        l->name = trEffect(effect->name());
    l->effect = std::move(effect);
    m_project->notifyLayersChanged();
}

void MainWindow::importMedia()
{
    double loopSeconds = 0;
    std::unique_ptr<Layer> layer = MediaImportDialog::run(this, &loopSeconds);
    if (!layer)
        return;
    m_project->checkpoint();
    if (loopSeconds > 0) {
        m_project->scene().loopSeconds = std::clamp(loopSeconds, 0.5, 60.0);
        m_project->notifySettingsChanged();
    }
    m_project->addLayer(std::move(layer));
}

void MainWindow::addShaderLayer()
{
    m_project->checkpoint();
    if (m_project->addEffectLayer(QStringLiteral("shader")) >= 0)
        openShaderEditor();
}

void MainWindow::openShaderEditor()
{
    if (!m_shaderEditor)
        m_shaderEditor = new ShaderEditor(m_project, this);
    m_shaderEditor->show();
    m_shaderEditor->raise();
    m_shaderEditor->activateWindow();
}

void MainWindow::updateConnectionUi()
{
    const bool usb = m_hid->isPresent();
    const bool service = !usb && m_client->isConnected() && !m_client->docks().isEmpty();
    QString text;
    QColor dot;
    if (!m_devicesEnabled) {
        text = tr("No link to the dock");
        dot = theme::kTextDim;
    } else if (usb || service) {
        text = tr("Flydigi CD2 · connected");
        dot = theme::kOk;
    } else {
        text = tr("CD2 dock not found");
        dot = theme::kDanger;
    }
    m_statusPill->setText(QStringLiteral("<span style='color:%1'>●</span>&nbsp; %2").arg(dot.name(), text));
    m_statusPill->setToolTip(usb ? tr("Animations go to the dock directly over USB; Space Station can keep running alongside.")
                                 : service ? tr("Animations go through the Space Station service.")
                                           : tr("Connect the CD2 dock over USB."));
    const bool busy = m_uploadId >= 0;
    m_sendBtn->setEnabled((usb || service) && !busy);
    m_sendBtn->setToolTip(tr("Bake the animation and upload it to the dock (≈0.1 s per frame)"));
}

void MainWindow::sendToDock()
{
    if (m_uploadId >= 0)
        return;
    const BakeResult r = bake(m_project->scene(), LedLayout::cd2(), m_maxFrames);
    const int brightness = m_project->scene().brightness;
    if (m_hid->isPresent()) {
        m_uploadVia = UploadVia::Hid;
        m_uploadId = m_hid->upload(r.rgb, r.frameCount, r.period, brightness);
    } else if (!m_client->docks().isEmpty()) {
        m_uploadVia = UploadVia::Service;
        m_uploadId = m_client->uploadCustom(m_client->docks().front().uid, r.rgb, r.frameCount, r.period, brightness);
    }
    if (m_uploadId < 0) {
        m_uploadVia = UploadVia::None;
        QMessageBox::warning(this, tr("Could not send"), tr("CD2 dock not found."));
        return;
    }
    setUploadProgress(0);
    if (m_uploadVia == UploadVia::Service) {
        m_uploadEstimateSeconds = 0.5 + r.frameCount * 0.087;
        m_uploadClock.start();
        m_estimateTimer.start();
    }
    updateConnectionUi();
    statusBar()->showMessage(tr("Sending %1 frame(s), step %2 ms…").arg(r.frameCount).arg(r.period * 20));
}

void MainWindow::updateBakeInfo()
{
    int frames = 0, period = 1;
    bakeTiming(m_project->scene(), m_maxFrames, &frames, &period);
    if (frames <= 1) {
        m_bakeInfo->setText(tr("Still image · 1 frame"));
        return;
    }
    const double fps = 1000.0 / (period * 20.0);
    m_bakeInfo->setText(tr("To the dock: %1 frames · step %2 ms (%3 fps) · loop %4 s")
                            .arg(frames)
                            .arg(period * 20)
                            .arg(QLocale().toString(fps, 'f', fps < 10 ? 1 : 0))
                            .arg(secondsText(frames * period * 0.02)));
}

void MainWindow::showOutputSettings()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Output settings"));
    dlg.setMinimumWidth(460);
    auto* v = new QVBoxLayout(&dlg);
    v->setSpacing(6);
    v->setContentsMargins(18, 14, 18, 14);

    Scene& scene = m_project->scene();
    auto addNote = [&](const QString& text) {
        auto* l = new QLabel(text, &dlg);
        l->setObjectName("Dim");
        l->setWordWrap(true);
        v->addWidget(l);
    };

    auto* asDockRow = new QHBoxLayout();
    asDockRow->addWidget(new QLabel(tr("Preview as on the dock"), &dlg), 1);
    auto* asDock = new ToggleSwitch(&dlg);
    asDock->setChecked(m_dockPreview);
    asDockRow->addWidget(asDock);
    v->addLayout(asDockRow);
    addNote(tr("Shows what the dock will play: each frame held for the dock's step, the output gamma and the 8-bit "
               "levels. The LEDs themselves are more saturated than a monitor can show."));

    v->addSpacing(8);
    v->addWidget(new QLabel(tr("Output gamma"), &dlg));
    auto* gamma = new SliderRow(&dlg);
    gamma->setRange(0.6, 2.6, 0.05);
    gamma->setValue(scene.outputGamma);
    v->addWidget(gamma);
    addNote(tr("The dock turns values into light along its own curve, close to gamma 1.4, so 1.4 keeps the "
               "preview's light ratios on the LEDs. Higher values brighten dim colours and wash out mixed hues; lower "
               "values make dark colours darker."));

    v->addSpacing(8);
    v->addWidget(new QLabel(tr("Lift dark tones"), &dlg));
    auto* lift = new SliderRow(&dlg);
    lift->setRange(0, 100, 1, "%");
    lift->setValue(scene.darkLift * 100.0);
    v->addWidget(lift);
    addNote(tr("Raises the darkest tones the way gamma 2.2 does while the mids and highs keep the output gamma: slow "
               "dark fades step less and stay lit instead of dropping out at the bottom, and colours keep their hue. "
               "0% sends the plain output curve."));

    v->addSpacing(8);
    v->addWidget(new QLabel(tr("Maximum frames per animation"), &dlg));
    auto* frames = new SliderRow(&dlg);
    frames->setRange(20, 255, 1);
    frames->setValue(m_maxFrames);
    v->addWidget(frames);
    addNote(tr("More frames play smoother but upload longer (~0.1 s per frame). Space Station never sends more than "
               "200."));

    v->addSpacing(8);
    v->addWidget(new QLabel(tr("Opening a scene"), &dlg));
    auto* openIn = new QComboBox(&dlg);
    openIn->addItem(tr("Ask every time"), QStringLiteral("ask"));
    openIn->addItem(tr("In the current tab"), QStringLiteral("here"));
    openIn->addItem(tr("In a new tab"), QStringLiteral("tab"));
    openIn->setCurrentIndex(std::max(0, openIn->findData(QSettings().value("file/openIn", "ask").toString())));
    v->addWidget(openIn);

    v->addSpacing(8);
    auto* appRow = new QHBoxLayout();
    appRow->addWidget(new QLabel(tr("Check for FlyGlow updates"), &dlg), 1);
    auto* appCheck = new ToggleSwitch(&dlg);
    appCheck->setChecked(OnlineUpdates::appCheckEnabled());
    appRow->addWidget(appCheck);
    v->addLayout(appRow);
    auto* presetRow = new QHBoxLayout();
    presetRow->addWidget(new QLabel(tr("Download new presets"), &dlg), 1);
    auto* presetSync = new ToggleSwitch(&dlg);
    presetSync->setChecked(OnlineUpdates::presetSyncEnabled());
    presetRow->addWidget(presetSync);
    v->addLayout(presetRow);
    addNote(tr("Once a day FlyGlow asks GitHub for the latest release and for the list of presets in the online "
               "library, and downloads the presets it does not have yet. Nothing is sent."));
    auto* checkNow = new QPushButton(tr("Check now"), &dlg);
    connect(checkNow, &QPushButton::clicked, this, [this] {
        m_manualOnline = true;
        m_online->checkApp(true);
        m_online->syncPresets(true);
    });
    v->addWidget(checkNow, 0, Qt::AlignLeft);

    v->addSpacing(8);
    v->addWidget(new QLabel(tr("Language"), &dlg));
    auto* language = new QComboBox(&dlg);
    language->addItem(QStringLiteral("English"), QStringLiteral("en"));
    language->addItem(QStringLiteral("Русский"), QStringLiteral("ru"));
    const QString currentLanguage = QSettings().value("ui/language", "en").toString();
    language->setCurrentIndex(std::max(0, language->findData(currentLanguage)));
    if (m_uploadId >= 0) {
        language->setEnabled(false);
        language->setToolTip(tr("Available once the upload finishes."));
    }
    v->addWidget(language);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    box->button(QDialogButtonBox::Ok)->setObjectName("Primary");
    box->button(QDialogButtonBox::Ok)->setText(tr("Apply"));
    box->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    v->addSpacing(10);
    v->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return;

    m_project->checkpoint();
    scene.outputGamma = gamma->value();
    scene.darkLift = lift->value() / 100.0;
    m_maxFrames = int(std::lround(frames->value()));
    QSettings().setValue("output/maxFrames", m_maxFrames);
    m_dockPreview = asDock->isChecked();
    QSettings().setValue("preview/asDock", m_dockPreview);
    QSettings().setValue("file/openIn", openIn->currentData().toString());
    OnlineUpdates::setAppCheckEnabled(appCheck->isChecked());
    OnlineUpdates::setPresetSyncEnabled(presetSync->isChecked());
    m_project->notifySettingsChanged();
    const QString newLanguage = language->currentData().toString();
    if (newLanguage != currentLanguage)
        switchLanguage(newLanguage);
}

QString MainWindow::dataDir() const
{
    return appDataDir();
}

void MainWindow::saveSession()
{
    // Saved tabs without changes are reopened from their files; the rest keep their scene in the session.
    QJsonArray tabs;
    for (int i = 0; i < int(m_tabs.size()); ++i) {
        const bool active = i == m_activeTab;
        const Project::Document& parked = *m_tabs[size_t(i)];
        const QString path = active ? m_project->filePath() : parked.path;
        const bool modified = active ? m_project->isModified() : parked.modified;
        QJsonObject tab{{"path", path}};
        if (modified || path.isEmpty()) {
            tab.insert("modified", modified);
            tab.insert("scene", active ? m_project->scene().toJson() : parked.scene.toJson());
        }
        tabs.append(tab);
    }
    QSaveFile f(dataDir() + "/session.json");
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonObject{{"tabs", tabs}, {"active", m_activeTab}}).toJson(QJsonDocument::Compact));
        if (f.commit())
            QFile::remove(dataDir() + "/autosave.flyglow");
    }
}

bool MainWindow::restoreSession()
{
    QFile f(dataDir() + "/session.json");
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject session = QJsonDocument::fromJson(f.readAll()).object();
    std::vector<std::unique_ptr<Project::Document>> docs;
    int active = 0;
    const int savedActive = session.value("active").toInt();
    const QJsonArray tabs = session.value("tabs").toArray();
    for (int i = 0; i < tabs.size(); ++i) {
        const QJsonObject tab = tabs[i].toObject();
        const QString path = tab.value("path").toString();
        Project temp;
        const bool ok = tab.contains("scene") ? temp.loadJson(tab.value("scene").toObject())
                                              : !path.isEmpty() && temp.load(path);
        if (!ok)
            continue;
        if (i == savedActive)
            active = int(docs.size());
        auto doc = std::make_unique<Project::Document>();
        temp.swapDocument(*doc);
        if (tab.contains("scene")) {
            doc->path = path;
            doc->modified = tab.value("modified").toBool();
        }
        docs.push_back(std::move(doc));
    }
    if (docs.empty())
        return false;
    // The first scene goes into the tab that already exists, the others get tabs of their own.
    m_project->swapDocument(*docs.front());
    for (size_t i = 1; i < docs.size(); ++i) {
        m_tabs.push_back(std::move(docs[i]));
        const QSignalBlocker block(m_tabBar);
        m_tabBar->addTab(QString());
        addCloseButton(m_tabBar->count() - 1);
    }
    switchTab(active);
    updateTitle();
    return true;
}

bool MainWindow::restoreLegacyAutosave()
{
    // Before tabs, 0.2.1 and older kept one scene in autosave.flyglow.
    QFile autosave(dataDir() + "/autosave.flyglow");
    if (!autosave.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument json = QJsonDocument::fromJson(autosave.readAll());
    Project temp;
    if (!json.isObject() || !temp.loadJson(json.object()))
        return false;
    const QString lastPath = QSettings().value("file/last").toString();
    if (!lastPath.isEmpty() && QFileInfo::exists(lastPath)) {
        Project probe;
        if (probe.load(lastPath) && QJsonDocument(probe.scene().toJson()) == json)
            temp.load(lastPath);
    }
    Project::Document doc;
    temp.swapDocument(doc);
    // An untitled scene from the autosave is unsaved work, so closing its tab should offer to save it.
    doc.modified = doc.path.isEmpty() && doc.scene.toJson() != defaultSceneJson();
    m_project->swapDocument(doc);
    updateTitle();
    return true;
}

void MainWindow::openExternal(const QStringList& paths)
{
    if (isMinimized())
        showNormal();
    raise();
    activateWindow();
    for (const QString& path : paths)
        openScene(path);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
    for (const QUrl& url : e->mimeData()->urls())
        if (url.isLocalFile() && url.toLocalFile().endsWith(".flyglow", Qt::CaseInsensitive)) {
            e->acceptProposedAction();
            return;
        }
}

void MainWindow::dropEvent(QDropEvent* e)
{
    for (const QUrl& url : e->mimeData()->urls())
        if (url.isLocalFile() && url.toLocalFile().endsWith(".flyglow", Qt::CaseInsensitive))
            openScene(url.toLocalFile());
}

bool MainWindow::openFile()
{
    QSettings settings;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open scene"), settings.value("file/dir").toString(),
                                                      tr("FlyGlow scenes (*.flyglow)") + QStringLiteral(";;")
                                                          + tr("All files (*)"));
    return !path.isEmpty() && openScene(path);
}

bool MainWindow::openScene(const QString& path)
{
    const QString file = QFileInfo(path).absoluteFilePath();
    for (int i = 0; i < int(m_tabs.size()); ++i) {
        const QString open = i == m_activeTab ? m_project->filePath() : m_tabs[size_t(i)]->path;
        if (!open.isEmpty() && QFileInfo(open).absoluteFilePath().compare(file, Qt::CaseInsensitive) == 0) {
            switchTab(i);
            return true;
        }
    }
    if (isPristine())
        return openPath(file);
    switch (askOpenTarget(QFileInfo(file).completeBaseName())) {
    case OpenTarget::Here:
        return confirmDiscard() && openPath(file);
    case OpenTarget::NewTab:
        return openInNewTab(file);
    case OpenTarget::Cancel:
        break;
    }
    return false;
}

MainWindow::OpenTarget MainWindow::askOpenTarget(const QString& name)
{
    const QString remembered = QSettings().value("file/openIn", "ask").toString();
    if (remembered == "here")
        return OpenTarget::Here;
    if (remembered == "tab")
        return OpenTarget::NewTab;
    QMessageBox box(QMessageBox::Question, tr("Open scene"), tr("Open “%1” in this tab or in a new one?").arg(name),
                    QMessageBox::Cancel, this);
    QAbstractButton* here = box.addButton(tr("Open here"), QMessageBox::AcceptRole);
    QAbstractButton* tab = box.addButton(tr("Open in a new tab"), QMessageBox::AcceptRole);
    box.setDefaultButton(qobject_cast<QPushButton*>(tab));
    auto* remember = new QCheckBox(tr("Remember my choice"), &box);
    box.setCheckBox(remember);
    box.exec();
    const OpenTarget target = box.clickedButton() == here  ? OpenTarget::Here
                              : box.clickedButton() == tab ? OpenTarget::NewTab
                                                           : OpenTarget::Cancel;
    if (target != OpenTarget::Cancel && remember->isChecked())
        QSettings().setValue("file/openIn", target == OpenTarget::Here ? "here" : "tab");
    return target;
}

void MainWindow::openPresetBrowser()
{
    if (!m_presetBrowser) {
        m_presetBrowser = new PresetBrowser(this);
        connect(m_presetBrowser, &PresetBrowser::openRequested, this, &MainWindow::openPreset);
        connect(m_presetBrowser, &PresetBrowser::savePresetRequested, this, &MainWindow::saveAsPreset);
        connect(m_presetBrowser, &PresetBrowser::syncRequested, this, [this] {
            m_manualOnline = true;
            m_online->syncPresets(true);
        });
    }
    m_presetBrowser->show();
    m_presetBrowser->raise();
    m_presetBrowser->activateWindow();
}

bool MainWindow::openPreset(const QString& path, bool newTab)
{
    const QJsonObject json = PresetLibrary::load(path);
    Project temp;
    QString err;
    if (!temp.loadJson(json, &err)) {
        QMessageBox::warning(this, tr("Could not open"), err);
        return false;
    }
    OpenTarget target = OpenTarget::NewTab;
    if (!newTab)
        target = isPristine() ? OpenTarget::Here : askOpenTarget(presetTitle(temp.scene().preset));
    if (target == OpenTarget::Cancel || (target == OpenTarget::Here && !confirmDiscard()))
        return false;
    // A preset opens as a new, unsaved scene: Save asks where to put it.
    auto doc = std::make_unique<Project::Document>();
    temp.swapDocument(*doc);
    if (target == OpenTarget::NewTab) {
        addTab(std::move(doc));
    } else {
        m_project->swapDocument(*doc);
        m_timeline->clearSelection();
        updateTitle();
    }
    statusBar()->showMessage(tr("Preset opened: %1").arg(presetTitle(m_project->scene().preset)), 4000);
    return true;
}

void MainWindow::saveAsPreset()
{
    const QJsonObject current = m_project->scene().preset;
    QWidget* parent = m_presetBrowser && m_presetBrowser->isVisible() ? static_cast<QWidget*>(m_presetBrowser) : this;
    QDialog dlg(parent);
    dlg.setWindowTitle(tr("Save as preset"));
    dlg.setMinimumWidth(480);
    auto* v = new QVBoxLayout(&dlg);
    v->setContentsMargins(18, 14, 18, 14);
    v->setSpacing(6);
    v->addWidget(new QLabel(tr("Title"), &dlg));
    const QString oldTitle = documentName(m_project->filePath(), m_project->scene());
    auto* title = new QLineEdit(oldTitle, &dlg);
    v->addWidget(title);
    v->addSpacing(4);
    v->addWidget(new QLabel(tr("Description"), &dlg));
    const QString oldDescription = localizedText(current.value(QStringLiteral("description")));
    auto* description = new QPlainTextEdit(oldDescription, &dlg);
    description->setFixedHeight(76);
    v->addWidget(description);
    auto* note = new QLabel(tr("The frame under the playhead becomes the cover. The settings at the top of the "
                               "Inspector are saved with the preset: right-click a setting in the Inspector to add it "
                               "there."),
                            &dlg);
    note->setObjectName("Dim");
    note->setWordWrap(true);
    v->addWidget(note);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    box->button(QDialogButtonBox::Save)->setObjectName("Primary");
    v->addSpacing(8);
    v->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString name = title->text().trimmed().isEmpty() ? tr("Untitled") : title->text().trimmed();
    QString file = name;
    file.remove(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")));
    file = file.trimmed().isEmpty() ? QStringLiteral("preset") : file.trimmed();
    const QString path = PresetLibrary::myPresetsDir() + QLatin1Char('/') + file + QStringLiteral(".flyglow");
    if (QFileInfo::exists(path)
        && QMessageBox::question(parent, tr("Save as preset"), tr("My presets already has “%1”. Replace it?").arg(file))
               != QMessageBox::Yes)
        return;

    // Texts the user left as they were keep every language they came with.
    const QString lang = QLocale().name().left(2);
    QJsonObject preset = current;
    if (name != oldTitle || !preset.contains(QStringLiteral("title")))
        preset.insert(QStringLiteral("title"), QJsonObject{{lang, name}});
    const QString text = description->toPlainText().trimmed();
    if (text != oldDescription)
        preset.insert(QStringLiteral("description"), QJsonObject{{lang, text}});
    if (text.isEmpty())
        preset.remove(QStringLiteral("description"));
    preset.insert(QStringLiteral("cover"), std::round(m_phase * 1000.0) / 1000.0);
    preset.remove(QStringLiteral("order"));
    QJsonObject json = m_project->scene().toJson();
    json.insert(QStringLiteral("preset"), preset);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(json).toJson(QJsonDocument::Compact)) < 0
        || !f.commit()) {
        QMessageBox::warning(parent, tr("Could not save"), QDir::toNativeSeparators(path));
        return;
    }
    statusBar()->showMessage(tr("Saved to My presets: %1").arg(name), 4000);
    if (m_presetBrowser && m_presetBrowser->isVisible())
        m_presetBrowser->reveal(path);
}

QString MainWindow::documentName(const QString& path, const Scene& scene) const
{
    if (!path.isEmpty())
        return QFileInfo(path).completeBaseName();
    const QString preset = presetTitle(scene.preset);
    return preset.isEmpty() ? tr("Untitled") : preset;
}

bool MainWindow::openInNewTab(const QString& path)
{
    Project temp;
    QString err;
    if (!temp.load(path, &err)) {
        QMessageBox::warning(this, tr("Could not open"), QDir::toNativeSeparators(path) + "\n\n" + err);
        return false;
    }
    auto doc = std::make_unique<Project::Document>();
    temp.swapDocument(*doc);
    addTab(std::move(doc));
    QSettings settings;
    settings.setValue("file/dir", QFileInfo(path).absolutePath());
    settings.setValue("file/last", path);
    return true;
}

bool MainWindow::confirmDiscard()
{
    if (!m_project->isModified())
        return true;
    const QString name = documentName(m_project->filePath(), m_project->scene());
    const auto answer = QMessageBox::question(this, tr("Unsaved changes"), tr("Save changes to “%1”?").arg(name),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel)
        return false;
    return answer == QMessageBox::Discard || saveFile(false);
}

bool MainWindow::isPristine() const
{
    return m_project->filePath().isEmpty() && !m_project->canUndo()
        && m_project->scene().toJson() == defaultSceneJson();
}

void MainWindow::newTab()
{
    Project temp;
    temp.newDefault();
    auto doc = std::make_unique<Project::Document>();
    temp.swapDocument(*doc);
    addTab(std::move(doc));
}

void MainWindow::addTab(std::unique_ptr<Project::Document> doc)
{
    m_tabs.push_back(std::move(doc));
    {
        const QSignalBlocker block(m_tabBar);
        m_tabBar->addTab(QString());
    }
    addCloseButton(m_tabBar->count() - 1);
    switchTab(m_tabBar->count() - 1);
}

void MainWindow::addCloseButton(int index)
{
    // QTabBar puts a tab's widget flush against its right edge, so the margin lives in a container around it.
    auto* box = new QWidget(m_tabBar);
    auto* layout = new QHBoxLayout(box);
    layout->setContentsMargins(6, 0, 6, 0);
    auto* close = new QToolButton(box);
    close->setObjectName("TabClose");
    close->setIcon(QIcon(QStringLiteral(":/ui/close.svg")));
    close->setIconSize(QSize(16, 16));
    close->setFixedSize(18, 18);
    close->setCursor(Qt::PointingHandCursor);
    layout->addWidget(close);
    // Tabs move, so the button looks up where it is when clicked.
    connect(close, &QToolButton::clicked, this, [this, box] {
        for (int i = 0; i < m_tabBar->count(); ++i)
            if (m_tabBar->tabButton(i, QTabBar::RightSide) == box)
                closeTab(i);
    });
    m_tabBar->setTabButton(index, QTabBar::RightSide, box);
}

void MainWindow::switchTab(int index)
{
    if (index < 0 || index >= int(m_tabs.size()))
        return;
    if (index != m_activeTab) {
        m_selectedKey = -1;
        m_strokeKey = -1;
        m_timeline->clearSelection();
        m_project->swapDocument(*m_tabs[size_t(index)]);
        std::swap(m_tabs[size_t(m_activeTab)], m_tabs[size_t(index)]);
        m_activeTab = index;
        if (!m_project->filePath().isEmpty())
            QSettings().setValue("file/last", m_project->filePath());
    }
    {
        const QSignalBlocker block(m_tabBar);
        m_tabBar->setCurrentIndex(index);
    }
    updateTitle();
}

void MainWindow::closeTab(int index)
{
    if (index < 0 || index >= int(m_tabs.size()))
        return;
    switchTab(index);
    if (!confirmDiscard())
        return;
    if (m_tabs.size() == 1) {
        m_project->newDefault();
        QSettings().remove("file/last");
        updateTitle();
        return;
    }
    // Park the closing scene behind a neighbour, then drop it with its tab.
    switchTab(index + 1 < int(m_tabs.size()) ? index + 1 : index - 1);
    m_tabs.erase(m_tabs.begin() + index);
    if (m_activeTab > index)
        --m_activeTab;
    {
        const QSignalBlocker block(m_tabBar);
        m_tabBar->removeTab(index);
        m_tabBar->setCurrentIndex(m_activeTab);
    }
    updateTitle();
}

void MainWindow::updateTabs()
{
    for (int i = 0; i < m_tabBar->count() && i < int(m_tabs.size()); ++i) {
        const bool active = i == m_activeTab;
        const QString path = active ? m_project->filePath() : m_tabs[size_t(i)]->path;
        const bool modified = active ? m_project->isModified() : m_tabs[size_t(i)]->modified;
        const QString name = documentName(path, active ? m_project->scene() : m_tabs[size_t(i)]->scene);
        m_tabBar->setTabText(i, modified ? name + QStringLiteral(" •") : name);
        m_tabBar->setTabToolTip(i, path.isEmpty() ? name : QDir::toNativeSeparators(path));
        if (QWidget* close = m_tabBar->tabButton(i, QTabBar::RightSide))
            close->setToolTip(tr("Close tab (Ctrl+W)"));
    }
}

bool MainWindow::openPath(const QString& path)
{
    QString err;
    if (!m_project->load(path, &err)) {
        QMessageBox::warning(this, tr("Could not open"), QDir::toNativeSeparators(path) + "\n\n" + err);
        return false;
    }
    m_timeline->clearSelection();
    QSettings settings;
    settings.setValue("file/dir", QFileInfo(path).absolutePath());
    settings.setValue("file/last", path);
    updateTitle();
    return true;
}

bool MainWindow::saveFile(bool saveAs)
{
    QSettings settings;
    QString path = m_project->filePath();
    if (saveAs || path.isEmpty()) {
        // A scene opened from a preset is offered under the preset's title.
        QString suggested = presetTitle(m_project->scene().preset);
        suggested.remove(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")));
        path = QFileDialog::getSaveFileName(this, tr("Save scene"),
                                            settings.value("file/dir").toString() + '/'
                                                + (suggested.isEmpty() ? tr("My lighting") : suggested)
                                                + QStringLiteral(".flyglow"),
                                            tr("FlyGlow scenes (*.flyglow)"));
        if (path.isEmpty())
            return false;
        if (!path.endsWith(".flyglow", Qt::CaseInsensitive))
            path += ".flyglow";
    }
    QString err;
    if (!m_project->save(path, &err)) {
        QMessageBox::warning(this, tr("Could not save"), err);
        return false;
    }
    settings.setValue("file/dir", QFileInfo(path).absolutePath());
    settings.setValue("file/last", path);
    statusBar()->showMessage(tr("Saved: %1").arg(QDir::toNativeSeparators(path)), 4000);
    updateTitle();
    return true;
}

void MainWindow::updateTitle()
{
    const QString name = documentName(m_project->filePath(), m_project->scene());
    setWindowTitle(QStringLiteral("%1%2 — FlyGlow").arg(name, m_project->isModified() ? QStringLiteral(" •") : QString()));
    updateTabs();
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    saveSession();
    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    if (!m_project->filePath().isEmpty())
        settings.setValue("file/last", m_project->filePath());
    e->accept();
}

} // namespace fg
