#include "PresetLibrary.h"

#include "AppPaths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QSettings>
#include <algorithm>
#include <cstring>

namespace fg {

namespace {

struct Category {
    const char* folder;
    const char* name;   // English source in the "Presets" translation context
};

const Category kCategories[] = {
    {"mine", QT_TRANSLATE_NOOP("Presets", "My presets")},
    {"heroes", QT_TRANSLATE_NOOP("Presets", "Superheroes")},
    {"games", QT_TRANSLATE_NOOP("Presets", "Games")},
    {"retro", QT_TRANSLATE_NOOP("Presets", "Retro and arcade")},
    {"emoji", QT_TRANSLATE_NOOP("Presets", "Emoji")},
    {"memes", QT_TRANSLATE_NOOP("Presets", "Memes")},
    {"anime", QT_TRANSLATE_NOOP("Presets", "Anime")},
    {"animals", QT_TRANSLATE_NOOP("Presets", "Animals")},
    {"nature", QT_TRANSLATE_NOOP("Presets", "Nature")},
    {"space", QT_TRANSLATE_NOOP("Presets", "Space")},
    {"holidays", QT_TRANSLATE_NOOP("Presets", "Holidays")},
    {"abstract", QT_TRANSLATE_NOOP("Presets", "Patterns")},
    {"ambient", QT_TRANSLATE_NOOP("Presets", "Mood lighting")},
    {"minimal", QT_TRANSLATE_NOOP("Presets", "Minimal")},
    {"other", QT_TRANSLATE_NOOP("Presets", "Other")},
};

/** Known categories in table order, unknown ones after them (by name), "other" last of all. */
int categoryRank(const QString& folder)
{
    const int n = int(std::size(kCategories));
    if (folder == QLatin1String("other"))
        return n + 1;
    for (int i = 0; i < n; ++i)
        if (folder == QLatin1String(kCategories[i].folder))
            return i;
    return n;
}

QString prettyFileName(const QString& path)
{
    QString s = QFileInfo(path).completeBaseName().replace(QLatin1Char('-'), QLatin1Char(' '));
    if (!s.isEmpty())
        s[0] = s[0].toUpper();
    return s;
}

} // namespace

QString localizedText(const QJsonValue& texts)
{
    const QJsonObject o = texts.toObject();
    const QString lang = QLocale().name().left(2);
    QString s = o.value(lang).toString();
    if (s.isEmpty())
        s = o.value(QStringLiteral("en")).toString();
    if (s.isEmpty() && !o.isEmpty())
        s = o.begin().value().toString();
    if (s.isEmpty() && texts.isString())
        s = texts.toString();
    return s;
}

QString presetTitle(const QJsonObject& preset)
{
    return localizedText(preset.value(QStringLiteral("title")));
}

QString PresetLibrary::Preset::title() const
{
    const QString t = presetTitle(info);
    return t.isEmpty() ? prettyFileName(path) : t;
}

QString PresetLibrary::Preset::description() const
{
    return localizedText(info.value(QStringLiteral("description")));
}

PresetLibrary& PresetLibrary::instance()
{
    static PresetLibrary library;
    return library;
}

bool PresetLibrary::read(const QString& path, const QString& category, Source source, Preset& p)
{
    const QJsonObject scene = load(path);
    if (scene.value(QStringLiteral("format")).toString() != QLatin1String("flyglow"))
        return false;
    p.path = path;
    p.category = category;
    p.source = source;
    p.info = scene.value(QStringLiteral("preset")).toObject();
    p.group = localizedText(p.info.value(QStringLiteral("group")));
    p.loopSeconds = std::max(0.2, scene.value(QStringLiteral("loopSeconds")).toDouble(4.0));
    p.cover = std::clamp(p.info.value(QStringLiteral("cover")).toDouble(0.25), 0.0, 1.0);
    p.order = p.info.value(QStringLiteral("order")).toInt(1000);
    if (source != Source::BuiltIn)
        p.stamp = QFileInfo(path).lastModified().toMSecsSinceEpoch();
    QStringList words{prettyFileName(path)};
    for (const char* key : {"title", "description", "group"}) {
        const QJsonValue v = p.info.value(QLatin1String(key));
        if (v.isString())
            words << v.toString();
        for (const QJsonValue& t : v.toObject())
            words << t.toString();
    }
    for (const QJsonValue& v : p.info.value(QStringLiteral("tags")).toArray())
        words << v.toString();
    p.searchText = words.join(QLatin1Char(' ')).toLower();
    return true;
}

PresetLibrary::PresetLibrary()
{
    const QString root = QStringLiteral(":/presets");
    QDirIterator it(root, {QStringLiteral("*.flyglow")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString relative = path.mid(root.size() + 1);
        const QString category = relative.section(QLatin1Char('/'), 0, 0);
        // A file right under the root, or in a folder starting with "_", is not a preset.
        if (!relative.contains(QLatin1Char('/')) || category.startsWith(QLatin1Char('_')))
            continue;
        Preset p;
        if (read(path, category, Source::BuiltIn, p))
            m_builtIn.push_back(std::move(p));
    }
    refresh();
}

void PresetLibrary::refresh()
{
    std::vector<Preset> all;
    // Downloaded presets first, so they win over the built-in copy of the same <category>/<name>.
    std::vector<QString> ids;
    const QString online = onlineDir();
    QDirIterator it(online, {QStringLiteral("*.flyglow")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString relative = QDir(online).relativeFilePath(path);
        const QString category = relative.section(QLatin1Char('/'), 0, 0);
        if (!relative.contains(QLatin1Char('/')) || category.startsWith(QLatin1Char('_')))
            continue;
        Preset p;
        if (read(path, category, Source::Online, p)) {
            ids.push_back(relative);
            all.push_back(std::move(p));
        }
    }
    for (const Preset& b : m_builtIn) {
        const QString id = b.path.mid(int(std::strlen(":/presets/")));
        if (std::find(ids.begin(), ids.end(), id) == ids.end())
            all.push_back(b);
    }
    auto scanFolder = [&all](const QString& dir, const QString& category, Source source) {
        const QFileInfoList files = QDir(dir).entryInfoList({QStringLiteral("*.flyglow")}, QDir::Files, QDir::Name);
        for (const QFileInfo& f : files) {
            Preset p;
            if (read(f.absoluteFilePath(), category, source, p))
                all.push_back(std::move(p));
        }
    };
    scanFolder(myPresetsDir(), QStringLiteral("mine"), Source::Mine);
    for (const QString& dir : userFolders())
        scanFolder(dir, QStringLiteral("other"), Source::Folder);

    std::stable_sort(all.begin(), all.end(), [](const Preset& a, const Preset& b) {
        const int ra = categoryRank(a.category), rb = categoryRank(b.category);
        if (ra != rb)
            return ra < rb;
        if (a.category != b.category)
            return a.category < b.category;
        if (a.order != b.order)
            return a.order < b.order;
        return a.path < b.path;
    });
    m_presets = std::move(all);
}

QString PresetLibrary::myPresetsDir()
{
    const QString dir = appDataDir() + QStringLiteral("/presets/mine");
    QDir().mkpath(dir);
    return dir;
}

QString PresetLibrary::onlineDir()
{
    return appDataDir() + QStringLiteral("/presets/online");
}

QStringList PresetLibrary::userFolders()
{
    return QSettings().value(QStringLiteral("presets/folders")).toStringList();
}

void PresetLibrary::setUserFolders(const QStringList& folders)
{
    QSettings().setValue(QStringLiteral("presets/folders"), folders);
}

QStringList PresetLibrary::categories() const
{
    QStringList out;
    for (const Preset& p : m_presets)
        if (out.isEmpty() || out.last() != p.category)
            out << p.category;
    return out;
}

QString PresetLibrary::categoryName(const QString& category)
{
    for (const Category& c : kCategories)
        if (category == QLatin1String(c.folder))
            return QCoreApplication::translate("Presets", c.name);
    QString s = category;
    if (!s.isEmpty())
        s[0] = s[0].toUpper();
    return s;
}

QJsonObject PresetLibrary::load(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

} // namespace fg
