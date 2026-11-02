#include "PresetBrowser.h"

#include "FlowLayout.h"
#include "Icons.h"
#include "PresetLibrary.h"
#include "Theme.h"
#include "Thumbnail.h"
#include "core/LedLayout.h"
#include "core/Scene.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace fg {

namespace {

constexpr int kPresetRole = Qt::UserRole + 1;
constexpr int kCategoryRole = Qt::UserRole + 2;
const QSize kThumb(132, 118);
const QSize kPreview(300, 270);
constexpr size_t kSceneCache = 6;

} // namespace

PresetBrowser::PresetBrowser(QWidget* parent) : QDialog(parent)
{
    setMinimumSize(900, 560);
    resize(1180, 720);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(10);

    m_search = new QLineEdit(this);
    m_search->setClearButtonEnabled(true);
    root->addWidget(m_search);

    auto* body = new QHBoxLayout();
    body->setSpacing(12);
    auto* left = new QVBoxLayout();
    left->setSpacing(6);
    m_categories = new QListWidget(this);
    m_categories->setFocusPolicy(Qt::NoFocus);
    left->addWidget(m_categories, 1);
    m_savePreset = new QPushButton(icons::get(icons::Id::Save), QString(), this);
    m_savePreset->setCursor(Qt::PointingHandCursor);
    left->addWidget(m_savePreset);
    m_folders = new QPushButton(icons::get(icons::Id::Open), QString(), this);
    m_folders->setCursor(Qt::PointingHandCursor);
    left->addWidget(m_folders);
    m_sync = new QPushButton(icons::get(icons::Id::Redo), QString(), this);
    m_sync->setCursor(Qt::PointingHandCursor);
    left->addWidget(m_sync);
    auto* leftBox = new QWidget(this);
    leftBox->setFixedWidth(200);
    leftBox->setLayout(left);
    body->addWidget(leftBox);

    auto* middle = new QVBoxLayout();
    middle->setSpacing(6);
    m_groupBar = new QWidget(this);
    m_groupLayout = new FlowLayout(m_groupBar, 6);
    m_groupButtons = new QButtonGroup(this);
    m_groupButtons->setExclusive(true);
    middle->addWidget(m_groupBar);
    m_grid = new QListWidget(this);
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setIconSize(kThumb);
    m_grid->setGridSize(QSize(kThumb.width() + 22, kThumb.height() + 46));
    m_grid->setResizeMode(QListView::Adjust);
    m_grid->setMovement(QListView::Static);
    m_grid->setWrapping(true);
    m_grid->setWordWrap(true);
    m_grid->setUniformItemSizes(true);
    m_grid->setSpacing(4);
    m_grid->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_grid->setMouseTracking(true);
    m_grid->setContextMenuPolicy(Qt::CustomContextMenu);
    m_grid->viewport()->installEventFilter(this);
    middle->addWidget(m_grid, 1);
    m_empty = new QLabel(this);
    m_empty->setObjectName("Dim");
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_empty->setVisible(false);
    middle->addWidget(m_empty);
    body->addLayout(middle, 1);

    auto* side = new QVBoxLayout();
    side->setSpacing(6);
    // The side box is exactly as wide as the preview: the layout's default margins would push it past the right
    // edge and cut it off.
    side->setContentsMargins(0, 0, 0, 0);
    m_preview = new QLabel(this);
    m_preview->setFixedSize(kPreview);
    m_preview->setAlignment(Qt::AlignCenter);
    side->addWidget(m_preview);
    m_title = new QLabel(this);
    m_title->setObjectName("AppTitle");
    m_title->setWordWrap(true);
    side->addWidget(m_title);
    m_meta = new QLabel(this);
    m_meta->setObjectName("Dim");
    side->addWidget(m_meta);
    m_description = new QLabel(this);
    m_description->setWordWrap(true);
    m_description->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    side->addWidget(m_description);
    m_controls = new QLabel(this);
    m_controls->setObjectName("Dim");
    m_controls->setWordWrap(true);
    side->addWidget(m_controls);
    m_tags = new QLabel(this);
    m_tags->setWordWrap(true);
    m_tags->setTextFormat(Qt::RichText);
    m_tags->setCursor(Qt::PointingHandCursor);
    side->addWidget(m_tags);
    side->addStretch(1);
    m_open = new QPushButton(this);
    m_open->setObjectName("Primary");
    m_open->setCursor(Qt::PointingHandCursor);
    m_open->setDefault(true);
    m_openTab = new QPushButton(this);
    m_openTab->setCursor(Qt::PointingHandCursor);
    side->addWidget(m_open);
    side->addWidget(m_openTab);
    auto* sideBox = new QWidget(this);
    sideBox->setFixedWidth(kPreview.width());
    sideBox->setLayout(side);
    body->addWidget(sideBox);
    root->addLayout(body, 1);

    connect(m_search, &QLineEdit::textChanged, this, &PresetBrowser::applyFilter);
    connect(m_categories, &QListWidget::currentRowChanged, this, [this] {
        m_group.clear();
        fillGroups();
        applyFilter();
    });
    connect(m_grid, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        select(item ? item->data(kPresetRole).toInt() : -1);
    });
    connect(m_grid, &QListWidget::itemActivated, this, [this] { open(false); });
    connect(m_grid, &QListWidget::customContextMenuRequested, this, &PresetBrowser::showItemMenu);
    connect(m_open, &QPushButton::clicked, this, [this] { open(false); });
    connect(m_openTab, &QPushButton::clicked, this, [this] { open(true); });
    connect(m_savePreset, &QPushButton::clicked, this, &PresetBrowser::savePresetRequested);
    connect(m_folders, &QPushButton::clicked, this, &PresetBrowser::editFolders);
    connect(m_sync, &QPushButton::clicked, this, &PresetBrowser::syncRequested);
    connect(m_tags, &QLabel::linkActivated, this, [this](const QString& tag) { m_search->setText(tag); });

    m_thumbTimer.setInterval(0);
    connect(&m_thumbTimer, &QTimer::timeout, this, &PresetBrowser::renderQueued);
    m_animTimer.setInterval(40);
    connect(&m_animTimer, &QTimer::timeout, this, &PresetBrowser::animate);

    retranslateUi();
    reload();
    const QString last = QSettings().value("presets/category").toString();
    for (int row = 0; row < m_categories->count(); ++row)
        if (m_categories->item(row)->data(kCategoryRole).toString() == last)
            m_categories->setCurrentRow(row);
    if (m_categories->currentRow() < 0)
        m_categories->setCurrentRow(0);
    fillGroups();
    applyFilter();
}

void PresetBrowser::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(e);
}

void PresetBrowser::retranslateUi()
{
    setWindowTitle(tr("Presets"));
    m_search->setPlaceholderText(tr("Search presets: a name, a game, a colour…"));
    m_open->setText(tr("Open"));
    m_open->setToolTip(tr("Open the preset as a new scene; an empty tab takes it, otherwise you are asked where"));
    m_openTab->setText(tr("Open in a new tab"));
    m_savePreset->setText(tr("Save current scene…"));
    m_savePreset->setToolTip(tr("Save the scene in the active tab to My presets (Ctrl+Shift+P)"));
    m_folders->setText(tr("Folders…"));
    m_folders->setToolTip(tr("Show the scenes from other folders too, such as Downloads, under Other"));
    m_sync->setText(tr("Get new presets"));
    m_sync->setToolTip(tr("Download presets added to the online library since this version came out"));
    const auto& presets = PresetLibrary::instance().presets();
    for (int row = 0; row < m_grid->count(); ++row) {
        QListWidgetItem* item = m_grid->item(row);
        const auto& p = presets[size_t(item->data(kPresetRole).toInt())];
        item->setText(p.title());
        item->setToolTip(p.description());
    }
    fillCategories();
    fillGroups();
    applyFilter();
    select(m_selected);
}

QString PresetBrowser::currentCategory() const
{
    return m_categories->currentItem() ? m_categories->currentItem()->data(kCategoryRole).toString() : QString();
}

QString PresetBrowser::coverKey(int preset) const
{
    const auto& p = PresetLibrary::instance().presets()[size_t(preset)];
    return p.path + QLatin1Char('|') + QString::number(p.stamp);
}

void PresetBrowser::reload()
{
    const auto& library = PresetLibrary::instance();
    const QString selectedPath =
        m_selected >= 0 && m_selected < int(library.presets().size()) ? library.presets()[size_t(m_selected)].path
                                                                     : QString();
    PresetLibrary::instance().refresh();
    const auto& presets = library.presets();
    m_scenes.clear();
    m_queue.clear();
    m_hovered = -1;
    m_selected = -1;
    QListWidgetItem* keep = nullptr;
    {
        const QSignalBlocker block(m_grid);
        m_grid->clear();
        for (int i = 0; i < int(presets.size()); ++i) {
            auto* item = new QListWidgetItem(m_grid);
            item->setData(kPresetRole, i);
            item->setSizeHint(m_grid->gridSize());
            item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
            item->setText(presets[size_t(i)].title());
            item->setToolTip(presets[size_t(i)].description());
            const auto cover = m_covers.constFind(coverKey(i));
            if (cover != m_covers.cend())
                item->setIcon(QIcon(*cover));
            else
                m_queue.push_back(i);
            if (!selectedPath.isEmpty() && presets[size_t(i)].path == selectedPath)
                keep = item;
        }
    }
    fillCategories();
    fillGroups();
    if (keep)
        m_grid->setCurrentItem(keep);
    applyFilter();
    if (!m_queue.empty() && isVisible())
        m_thumbTimer.start();
}

void PresetBrowser::reveal(const QString& path)
{
    reload();
    const auto& presets = PresetLibrary::instance().presets();
    for (int i = 0; i < int(presets.size()); ++i) {
        if (QDir::cleanPath(presets[size_t(i)].path) != QDir::cleanPath(path))
            continue;
        m_search->clear();
        for (int row = 0; row < m_categories->count(); ++row)
            if (m_categories->item(row)->data(kCategoryRole).toString() == presets[size_t(i)].category)
                m_categories->setCurrentRow(row);
        m_grid->setCurrentItem(itemFor(i));
        return;
    }
}

void PresetBrowser::fillCategories()
{
    const QString current = currentCategory();
    const QSignalBlocker block(m_categories);
    m_categories->clear();
    const auto& library = PresetLibrary::instance();
    auto add = [this](const QString& name, int count, const QString& category) {
        auto* item = new QListWidgetItem(QStringLiteral("%1   %2").arg(name).arg(count), m_categories);
        item->setData(kCategoryRole, category);
    };
    add(tr("All presets"), int(library.presets().size()), QString());
    QStringList categories = library.categories();
    // My presets is always listed, so the place for saved scenes is easy to find.
    if (!categories.contains(QStringLiteral("mine")))
        categories.prepend(QStringLiteral("mine"));
    for (const QString& c : categories)
        add(PresetLibrary::categoryName(c),
            int(std::count_if(library.presets().begin(), library.presets().end(),
                              [&c](const PresetLibrary::Preset& p) { return p.category == c; })),
            c);
    for (int row = 0; row < m_categories->count(); ++row)
        if (m_categories->item(row)->data(kCategoryRole).toString() == current)
            m_categories->setCurrentRow(row);
}

void PresetBrowser::fillGroups()
{
    for (QAbstractButton* b : m_groupButtons->buttons()) {
        m_groupButtons->removeButton(b);
        b->deleteLater();
    }
    while (QLayoutItem* it = m_groupLayout->takeAt(0))
        delete it;
    const QString category = currentCategory();
    QStringList groups;
    if (!category.isEmpty())
        for (const auto& p : PresetLibrary::instance().presets())
            if (p.category == category && !p.group.isEmpty() && !groups.contains(p.group))
                groups << p.group;
    std::sort(groups.begin(), groups.end(), [](const QString& a, const QString& b) {
        return QString::localeAwareCompare(a, b) < 0;
    });
    m_groupBar->setVisible(groups.size() >= 2);
    if (!groups.contains(m_group))
        m_group.clear();
    if (groups.size() < 2)
        return;
    auto chip = [this](const QString& text, const QString& group) {
        auto* b = new QToolButton(m_groupBar);
        b->setObjectName("Chip");
        b->setText(text);
        b->setCheckable(true);
        b->setChecked(group == m_group);
        b->setCursor(Qt::PointingHandCursor);
        m_groupButtons->addButton(b);
        m_groupLayout->addWidget(b);
        connect(b, &QToolButton::clicked, this, [this, group] {
            m_group = group;
            applyFilter();
        });
    };
    chip(tr("All"), QString());
    for (const QString& g : groups)
        chip(g, g);
}

void PresetBrowser::applyFilter()
{
    const QString category = currentCategory();
    QSettings().setValue("presets/category", category);
    const QStringList words = m_search->text().toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const auto& presets = PresetLibrary::instance().presets();
    int shown = 0;
    QListWidgetItem* first = nullptr;
    bool selectedShown = false;
    for (int row = 0; row < m_grid->count(); ++row) {
        QListWidgetItem* item = m_grid->item(row);
        const int index = item->data(kPresetRole).toInt();
        const auto& p = presets[size_t(index)];
        // Searching looks through every category and group.
        bool show = words.isEmpty()
                        ? (category.isEmpty() || p.category == category) && (m_group.isEmpty() || p.group == m_group)
                        : true;
        for (const QString& w : words)
            show = show && p.searchText.contains(w);
        item->setHidden(!show);
        if (show) {
            ++shown;
            first = first ? first : item;
            selectedShown = selectedShown || index == m_selected;
        }
    }
    m_empty->setText(category == QLatin1String("mine") && words.isEmpty()
                         ? tr("No presets of your own yet. Open or make a scene, then click “Save current scene…” "
                              "on the left.")
                         : tr("Nothing found"));
    m_empty->setVisible(shown == 0);
    if (!selectedShown && first)
        m_grid->setCurrentItem(first);
    else if (!selectedShown)
        select(-1);
    if (QListWidgetItem* item = m_grid->currentItem(); item && !item->isHidden())
        m_grid->scrollToItem(item);
}

void PresetBrowser::select(int preset)
{
    m_selected = preset;
    const auto& presets = PresetLibrary::instance().presets();
    const bool valid = preset >= 0 && preset < int(presets.size());
    m_open->setEnabled(valid);
    m_openTab->setEnabled(valid);
    if (!valid) {
        m_title->clear();
        m_meta->clear();
        m_description->clear();
        m_controls->clear();
        m_tags->clear();
        m_preview->setPixmap(renderThumbnail({}, kPreview));
        return;
    }
    const auto& p = presets[size_t(preset)];
    m_title->setText(p.title());
    const QString where = p.group.isEmpty() ? PresetLibrary::categoryName(p.category)
                                            : PresetLibrary::categoryName(p.category) + QStringLiteral(" · ") + p.group;
    m_meta->setText(tr("%1 · loop %2 s").arg(where, QLocale().toString(p.loopSeconds, 'g', 3)));
    m_description->setText(p.description());
    QStringList controls;
    for (const QJsonValue& c : p.info.value(QStringLiteral("controls")).toArray())
        controls << localizedText(c.toObject().value(QStringLiteral("label")));
    m_controls->setText(controls.isEmpty() ? QString() : tr("Settings: %1").arg(controls.join(QStringLiteral(", "))));
    // Tags come in both languages; the ones in the UI's script are shown, each one searches for itself.
    const bool cyrillic = QLocale().name().startsWith(QLatin1String("ru"));
    static const QRegularExpression kCyrillic(QStringLiteral("[\\x{0400}-\\x{04FF}]"));
    QStringList links;
    for (const QJsonValue& v : p.info.value(QStringLiteral("tags")).toArray()) {
        const QString tag = v.toString();
        if (tag.contains(kCyrillic) == cyrillic)
            links << QStringLiteral("<a href=\"%1\" style=\"color:%2; text-decoration:none\">#%1</a>")
                         .arg(tag.toHtmlEscaped(), theme::kAccent.name());
    }
    m_tags->setText(links.join(QStringLiteral("&nbsp;&nbsp; ")));
    m_clock.restart();
    animate();
}

void PresetBrowser::open(bool newTab)
{
    const auto& presets = PresetLibrary::instance().presets();
    if (m_selected < 0 || m_selected >= int(presets.size()))
        return;
    const QString path = presets[size_t(m_selected)].path;
    hide();
    emit openRequested(path, newTab);
}

void PresetBrowser::showItemMenu(const QPoint& pos)
{
    QListWidgetItem* item = m_grid->itemAt(pos);
    if (!item)
        return;
    m_grid->setCurrentItem(item);
    const auto& p = PresetLibrary::instance().presets()[size_t(item->data(kPresetRole).toInt())];
    QMenu menu(this);
    menu.addAction(tr("Open"), this, [this] { open(false); });
    menu.addAction(tr("Open in a new tab"), this, [this] { open(true); });
    if (p.source == PresetLibrary::Source::Mine || p.source == PresetLibrary::Source::Folder) {
        const QString path = p.path;
        menu.addSeparator();
        menu.addAction(tr("Show in folder"), this, [path] {
            QProcess::startDetached(QStringLiteral("explorer.exe"),
                                    {QStringLiteral("/select,"), QDir::toNativeSeparators(path)});
        });
        if (p.source == PresetLibrary::Source::Mine) {
            const QString title = p.title();
            menu.addAction(tr("Move to the Recycle Bin"), this, [this, path, title] {
                if (QMessageBox::question(this, tr("Delete preset"), tr("Move “%1” to the Recycle Bin?").arg(title))
                    != QMessageBox::Yes)
                    return;
                QFile::moveToTrash(path);
                reload();
            });
        }
    }
    menu.exec(m_grid->viewport()->mapToGlobal(pos));
}

void PresetBrowser::editFolders()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Preset folders"));
    dlg.setMinimumWidth(520);
    auto* v = new QVBoxLayout(&dlg);
    v->setContentsMargins(18, 14, 18, 14);
    auto* note = new QLabel(tr("Scenes (.flyglow) in these folders appear in the gallery under Other. Subfolders are "
                               "not searched."),
                            &dlg);
    note->setObjectName("Dim");
    note->setWordWrap(true);
    v->addWidget(note);
    auto* list = new QListWidget(&dlg);
    for (const QString& f : PresetLibrary::userFolders())
        list->addItem(QDir::toNativeSeparators(f));
    v->addWidget(list, 1);
    auto* row = new QHBoxLayout();
    auto* add = new QPushButton(tr("Add folder…"), &dlg);
    auto* remove = new QPushButton(tr("Remove"), &dlg);
    row->addWidget(add);
    row->addWidget(remove);
    row->addStretch(1);
    v->addLayout(row);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    box->button(QDialogButtonBox::Ok)->setObjectName("Primary");
    v->addWidget(box);
    connect(add, &QPushButton::clicked, &dlg, [&dlg, list] {
        const QString dir = QFileDialog::getExistingDirectory(
            &dlg, tr("Add folder"), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
        if (!dir.isEmpty() && list->findItems(QDir::toNativeSeparators(dir), Qt::MatchExactly).isEmpty())
            list->addItem(QDir::toNativeSeparators(dir));
    });
    connect(remove, &QPushButton::clicked, &dlg, [list] { delete list->currentItem(); });
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QStringList folders;
    for (int i = 0; i < list->count(); ++i)
        folders << QDir::fromNativeSeparators(list->item(i)->text());
    PresetLibrary::setUserFolders(folders);
    reload();
}

void PresetBrowser::showEvent(QShowEvent* e)
{
    QDialog::showEvent(e);
    // Presets saved or dropped into a folder since the window was last open show up now.
    if (!e->spontaneous())
        reload();
    m_clock.restart();
    m_animTimer.start();
    if (!m_queue.empty())
        m_thumbTimer.start();
    m_search->setFocus();
    // The grid lays its items out only once it is visible, so scrolling to the selection has to wait for that.
    QTimer::singleShot(0, this, [this] {
        if (QListWidgetItem* item = m_grid->currentItem())
            m_grid->scrollToItem(item, QAbstractItemView::PositionAtCenter);
    });
}

void PresetBrowser::hideEvent(QHideEvent* e)
{
    m_animTimer.stop();
    m_thumbTimer.stop();
    QDialog::hideEvent(e);
}

bool PresetBrowser::eventFilter(QObject* watched, QEvent* e)
{
    if (watched == m_grid->viewport()) {
        int hovered = m_hovered;
        if (e->type() == QEvent::MouseMove) {
            QListWidgetItem* item = m_grid->itemAt(static_cast<QMouseEvent*>(e)->position().toPoint());
            hovered = item ? item->data(kPresetRole).toInt() : -1;
        } else if (e->type() == QEvent::Leave) {
            hovered = -1;
        }
        if (hovered != m_hovered) {
            // The tile that stops playing goes back to its cover frame.
            if (QListWidgetItem* old = itemFor(m_hovered)) {
                const auto cover = m_covers.constFind(coverKey(m_hovered));
                if (cover != m_covers.cend())
                    old->setIcon(QIcon(*cover));
            }
            m_hovered = hovered;
        }
    }
    return QDialog::eventFilter(watched, e);
}

QListWidgetItem* PresetBrowser::itemFor(int preset) const
{
    if (preset < 0)
        return nullptr;
    // Items are created in preset order and never reordered.
    return preset < m_grid->count() ? m_grid->item(preset) : nullptr;
}

const Scene* PresetBrowser::scene(int preset)
{
    auto it = m_scenes.find(preset);
    if (it != m_scenes.end())
        return it->second.get();
    const auto& presets = PresetLibrary::instance().presets();
    auto s = std::make_unique<Scene>();
    if (!s->fromJson(PresetLibrary::load(presets[size_t(preset)].path)))
        return nullptr;
    // Only the hovered and selected presets need to stay loaded.
    for (auto i = m_scenes.begin(); i != m_scenes.end() && m_scenes.size() >= kSceneCache;)
        i = i->first != m_selected && i->first != m_hovered ? m_scenes.erase(i) : std::next(i);
    return (m_scenes[preset] = std::move(s)).get();
}

QPixmap PresetBrowser::thumbnail(int preset, double phase, const QSize& size)
{
    std::vector<Rgb> out;
    if (const Scene* s = scene(preset))
        s->render(phase, LedLayout::cd2(), out);
    return renderThumbnail(out, size);
}

void PresetBrowser::renderQueued()
{
    const auto& presets = PresetLibrary::instance().presets();
    QElapsedTimer budget;
    budget.start();
    while (!m_queue.empty() && budget.elapsed() < 12) {
        const int i = m_queue.front();
        m_queue.pop_front();
        // A cover is rendered from a scene loaded just for it, so the animation cache is not churned.
        Scene s;
        std::vector<Rgb> out;
        if (s.fromJson(PresetLibrary::load(presets[size_t(i)].path)))
            s.render(presets[size_t(i)].cover, LedLayout::cd2(), out);
        const QPixmap cover = renderThumbnail(out, kThumb);
        m_covers.insert(coverKey(i), cover);
        if (QListWidgetItem* item = itemFor(i); item && i != m_hovered)
            item->setIcon(QIcon(cover));
    }
    if (m_queue.empty())
        m_thumbTimer.stop();
}

void PresetBrowser::animate()
{
    const auto& presets = PresetLibrary::instance().presets();
    const double seconds = m_clock.elapsed() / 1000.0;
    if (m_hovered >= 0 && m_hovered < int(presets.size())) {
        const double phase = std::fmod(seconds / presets[size_t(m_hovered)].loopSeconds, 1.0);
        if (QListWidgetItem* item = itemFor(m_hovered))
            item->setIcon(QIcon(thumbnail(m_hovered, phase, kThumb)));
    }
    if (m_selected >= 0 && m_selected < int(presets.size())) {
        const double phase = std::fmod(seconds / presets[size_t(m_selected)].loopSeconds, 1.0);
        m_preview->setPixmap(thumbnail(m_selected, phase, kPreview));
    }
}

} // namespace fg
