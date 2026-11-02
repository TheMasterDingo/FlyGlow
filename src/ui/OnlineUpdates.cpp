#include "OnlineUpdates.h"

#include "PresetLibrary.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QVersionNumber>

namespace fg {

namespace {

const QUrl kLatestRelease(QStringLiteral("https://api.github.com/repos/PsinaDev/FlyGlow/releases/latest"));
const QString kFeed = QStringLiteral("https://raw.githubusercontent.com/PsinaDev/FlyGlow/main/examples/scenes/");
/** Presets are a few hundred KB at most; anything far bigger is not one. */
constexpr qint64 kMaxPresetBytes = 8 << 20;

/** FLYGLOW_PRESET_FEED points the library at another copy of examples/scenes/, such as a fork or a local server. */
QString feedBase()
{
    QString base = qEnvironmentVariable("FLYGLOW_PRESET_FEED");
    if (base.isEmpty())
        return kFeed;
    return base.endsWith(QLatin1Char('/')) ? base : base + QLatin1Char('/');
}

QVersionNumber appVersion()
{
    return QVersionNumber::fromString(QCoreApplication::applicationVersion());
}

bool ranToday(const QString& key)
{
    return QSettings().value(key).toDate() == QDate::currentDate();
}

/** <category>/<name>.flyglow and nothing else, so an index can never write outside the library folder. */
bool safePath(const QString& path)
{
    static const QRegularExpression re(QStringLiteral(R"(^[a-z0-9][a-z0-9_-]*/[A-Za-z0-9][A-Za-z0-9_.-]*\.flyglow$)"));
    return re.match(path).hasMatch() && !path.contains(QStringLiteral(".."));
}

QByteArray sha256(const QByteArray& data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
}

QByteArray fileSha256(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? sha256(f.readAll()) : QByteArray();
}

} // namespace

OnlineUpdates::OnlineUpdates(QObject* parent) : QObject(parent), m_net(new QNetworkAccessManager(this)) {}

bool OnlineUpdates::appCheckEnabled()
{
    return QSettings().value(QStringLiteral("updates/checkApp"), true).toBool();
}

bool OnlineUpdates::presetSyncEnabled()
{
    return QSettings().value(QStringLiteral("updates/syncPresets"), true).toBool();
}

void OnlineUpdates::setAppCheckEnabled(bool on)
{
    QSettings().setValue(QStringLiteral("updates/checkApp"), on);
}

void OnlineUpdates::setPresetSyncEnabled(bool on)
{
    QSettings().setValue(QStringLiteral("updates/syncPresets"), on);
}

void OnlineUpdates::skipVersion(const QString& version)
{
    QSettings().setValue(QStringLiteral("updates/skipped"), version);
}

void OnlineUpdates::checkDaily()
{
    if (appCheckEnabled() && !ranToday(QStringLiteral("updates/lastAppCheck")))
        checkApp(false);
    if (presetSyncEnabled() && !ranToday(QStringLiteral("updates/lastPresetSync")))
        syncPresets(false);
}

QNetworkReply* OnlineUpdates::get(const QUrl& url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("FlyGlow/%1").arg(QCoreApplication::applicationVersion()));
    request.setTransferTimeout(20000);
    return m_net->get(request);
}

void OnlineUpdates::checkApp(bool manual)
{
    QNetworkReply* reply = get(kLatestRelease);
    connect(reply, &QNetworkReply::finished, this, [this, reply, manual] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (manual)
                emit appCheckFailed(reply->errorString());
            return;
        }
        QSettings().setValue(QStringLiteral("updates/lastAppCheck"), QDate::currentDate());
        const QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
        QString tag = release.value(QStringLiteral("tag_name")).toString();
        if (tag.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
            tag.remove(0, 1);
        const QVersionNumber latest = QVersionNumber::fromString(tag);
        if (latest.isNull()) {
            if (manual)
                emit appCheckFailed(tr("GitHub did not name a release."));
            return;
        }
        const bool skipped = QSettings().value(QStringLiteral("updates/skipped")).toString() == latest.toString();
        if (QVersionNumber::compare(latest, appVersion()) > 0 && (manual || !skipped))
            emit appUpdateAvailable(latest.toString(), QUrl(release.value(QStringLiteral("html_url")).toString()));
        else if (manual)
            emit appUpToDate();
    });
}

void OnlineUpdates::syncPresets(bool manual)
{
    if (m_syncing)
        return;
    m_syncing = true;
    m_manualSync = manual;
    m_pending.clear();
    m_failed.clear();
    m_added = m_updated = m_removed = 0;
    QNetworkReply* reply = get(QUrl(feedBase() + QStringLiteral("index.json")));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            m_syncing = false;
            emit presetSyncFailed(reply->errorString());
            return;
        }
        handleIndex(reply->readAll(), m_manualSync);
    });
}

void OnlineUpdates::handleIndex(const QByteArray& json, bool manual)
{
    Q_UNUSED(manual);
    const QJsonObject index = QJsonDocument::fromJson(json).object();
    if (index.value(QStringLiteral("format")).toString() != QLatin1String("flyglow-presets")) {
        m_syncing = false;
        emit presetSyncFailed(tr("The preset index is not in a format this FlyGlow knows."));
        return;
    }
    const QString root = PresetLibrary::onlineDir();
    const QVersionNumber app = appVersion();
    QSet<QString> listed;
    for (const QJsonValue& v : index.value(QStringLiteral("presets")).toArray()) {
        const QJsonObject e = v.toObject();
        const QString path = e.value(QStringLiteral("path")).toString();
        const QByteArray hash = e.value(QStringLiteral("sha256")).toString().toLatin1().toLower();
        if (!safePath(path) || hash.size() != 64)
            continue;
        listed.insert(path);
        // Presets that need a newer FlyGlow wait for it.
        const QVersionNumber minApp = QVersionNumber::fromString(e.value(QStringLiteral("minApp")).toString());
        if (!minApp.isNull() && QVersionNumber::compare(minApp, app) > 0)
            continue;
        const QString local = root + QLatin1Char('/') + path;
        const QString builtIn = QStringLiteral(":/presets/") + path;
        if (QFile::exists(builtIn) && fileSha256(builtIn) == hash) {
            // The built-in copy is current again (an app update caught up): a downloaded one is not needed.
            if (QFile::exists(local) && QFile::remove(local))
                ++m_removed;
            continue;
        }
        if (QFile::exists(local) && fileSha256(local) == hash)
            continue;
        m_pending.append({path, hash, QFile::exists(local) || QFile::exists(builtIn)});
    }
    // Presets taken out of the library go away here too.
    QDirIterator it(root, {QStringLiteral("*.flyglow")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        if (!listed.contains(QDir(root).relativeFilePath(file)) && QFile::remove(file))
            ++m_removed;
    }
    downloadNext();
}

void OnlineUpdates::downloadNext()
{
    if (m_pending.isEmpty()) {
        finishSync();
        return;
    }
    const Pending p = m_pending.takeFirst();
    QNetworkReply* reply = get(QUrl(feedBase() + p.path));
    connect(reply, &QNetworkReply::finished, this, [this, reply, p] {
        reply->deleteLater();
        const QByteArray data = reply->error() == QNetworkReply::NoError ? reply->read(kMaxPresetBytes) : QByteArray();
        const bool scene = QJsonDocument::fromJson(data).object().value(QStringLiteral("format")).toString()
                        == QLatin1String("flyglow");
        if (data.isEmpty() || sha256(data) != p.sha256 || !scene) {
            m_failed << p.path;
        } else {
            const QString local = PresetLibrary::onlineDir() + QLatin1Char('/') + p.path;
            QDir().mkpath(QFileInfo(local).absolutePath());
            QSaveFile f(local);
            if (f.open(QIODevice::WriteOnly) && f.write(data) == data.size() && f.commit())
                ++(p.replaces ? m_updated : m_added);
            else
                m_failed << p.path;
        }
        downloadNext();
    });
}

void OnlineUpdates::finishSync()
{
    m_syncing = false;
    if (m_failed.isEmpty())
        QSettings().setValue(QStringLiteral("updates/lastPresetSync"), QDate::currentDate());
    if (!m_failed.isEmpty() && m_added + m_updated == 0)
        emit presetSyncFailed(tr("Could not download %n preset(s).", nullptr, int(m_failed.size())));
    else
        emit presetsSynced(m_added, m_updated, m_removed);
}

} // namespace fg
