#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <vector>

namespace fg {

/** Text in the UI language from {"en": ..., "ru": ...}; English when there is none for it, else any. */
QString localizedText(const QJsonValue& texts);

/** A scene's preset title in the UI language, or an empty string for a scene that is not a preset. */
QString presetTitle(const QJsonObject& preset);

/**
 * Every preset the gallery shows (docs/presets.md):
 * - built in: each .flyglow under examples/scenes, compiled in as :/presets/<category>/<name>.flyglow;
 * - downloaded from the online library, laid out the same way under onlineDir(); one with the same
 *   <category>/<name> replaces the built-in copy;
 * - the user's own, saved with "Save as preset" into myPresetsDir() (category "mine");
 * - any .flyglow in the folders the user added (category "other").
 * Only the preset blocks are kept; scenes are read when asked for.
 */
class PresetLibrary {
public:
    enum class Source { BuiltIn, Online, Mine, Folder };

    struct Preset {
        QString path;          /**< resource or file path */
        QString category;      /**< folder name, "mine" or "other" */
        QString group;         /**< franchise or theme inside the category, e.g. "Minecraft"; may be empty */
        Source source = Source::BuiltIn;
        QJsonObject info;      /**< the scene's "preset" block */
        double loopSeconds = 4;
        double cover = 0.25;
        int order = 1000;
        qint64 stamp = 0;      /**< file modification time, 0 for built-ins: a changed file needs a new cover */
        QString title() const;
        QString description() const;
        /** Title, description, group and tags in every language, lower-case, for the search. */
        QString searchText;
    };

    static PresetLibrary& instance();

    const std::vector<Preset>& presets() const { return m_presets; }
    /** Reads the downloaded presets, My presets and the user's folders again; built-ins are read once. */
    void refresh();
    /** Category folders in display order: My presets, the known ones, the rest by name, then Other. */
    QStringList categories() const;
    static QString categoryName(const QString& category);

    /** The whole scene file. */
    static QJsonObject load(const QString& path);

    /** Where "Save as preset" writes; created on use. */
    static QString myPresetsDir();
    /** Presets downloaded from the online library, as <category>/<name>.flyglow. */
    static QString onlineDir();
    /** Folders the user added to the gallery. */
    static QStringList userFolders();
    static void setUserFolders(const QStringList& folders);

private:
    PresetLibrary();
    /** Reads one scene file into a preset; false when it is not a FlyGlow scene. */
    static bool read(const QString& path, const QString& category, Source source, Preset& out);
    std::vector<Preset> m_builtIn;
    std::vector<Preset> m_presets;
};

} // namespace fg
