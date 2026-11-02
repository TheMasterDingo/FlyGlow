#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <QHash>
#include <QPixmap>
#include <QTimer>
#include <deque>
#include <map>
#include <memory>
#include <vector>

class QButtonGroup;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QWidget;

namespace fg {

class FlowLayout;
class Scene;

/**
 * The preset gallery: categories, groups inside a category, a search, a grid of thumbnails that play while the
 * mouse is over them and a larger looping preview of the selected preset. The library is read again every time the
 * window opens, so presets saved or dropped into a folder meanwhile show up; covers are rendered a few at a time and
 * kept per file.
 */
class PresetBrowser : public QDialog {
    Q_OBJECT
public:
    explicit PresetBrowser(QWidget* parent = nullptr);
    /** Reads the library again and keeps the selection when the preset is still there. */
    void reload();
    /** Reads the library again and selects the preset at path in its own category. */
    void reveal(const QString& path);

signals:
    /** path is a resource or file path from PresetLibrary; newTab asks for a tab of its own. */
    void openRequested(const QString& path, bool newTab);
    /** "Save the current scene as a preset" was clicked. */
    void savePresetRequested();
    /** "Check for new presets" was clicked. */
    void syncRequested();

protected:
    void changeEvent(QEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* e) override;

private:
    void retranslateUi();
    void fillCategories();
    void fillGroups();
    void applyFilter();
    void select(int preset);
    void open(bool newTab);
    void renderQueued();
    void animate();
    void showItemMenu(const QPoint& pos);
    void editFolders();
    QString currentCategory() const;
    /** Loaded on first use and kept for the few presets being animated. */
    const Scene* scene(int preset);
    QPixmap thumbnail(int preset, double phase, const QSize& size);
    QListWidgetItem* itemFor(int preset) const;
    QString coverKey(int preset) const;

    QLineEdit* m_search;
    QListWidget* m_categories;
    QPushButton* m_savePreset;
    QPushButton* m_folders;
    QPushButton* m_sync;
    QWidget* m_groupBar;
    FlowLayout* m_groupLayout;
    QButtonGroup* m_groupButtons;
    QString m_group;
    QListWidget* m_grid;
    QLabel* m_empty;
    QLabel* m_preview;
    QLabel* m_title;
    QLabel* m_meta;
    QLabel* m_description;
    QLabel* m_controls;
    QLabel* m_tags;
    QPushButton* m_open;
    QPushButton* m_openTab;

    /** Covers by path and file time, so reopening the window does not render them again. */
    QHash<QString, QPixmap> m_covers;
    std::deque<int> m_queue;
    std::map<int, std::unique_ptr<Scene>> m_scenes;
    QTimer m_thumbTimer;
    QTimer m_animTimer;
    QElapsedTimer m_clock;
    int m_selected = -1;
    int m_hovered = -1;
};

} // namespace fg
