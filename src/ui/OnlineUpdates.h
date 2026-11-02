#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace fg {

/**
 * Two read-only checks against the GitHub repository, at most once a day each (docs/presets.md):
 * - the latest release, to offer a newer FlyGlow;
 * - the online preset library: examples/scenes/index.json on main lists every preset with its SHA-256 and the
 *   oldest FlyGlow that can load it. Presets that differ from the built-in copy are downloaded into
 *   PresetLibrary::onlineDir(), where the gallery picks them up; ones gone from the index are removed.
 * Nothing is sent but plain GET requests.
 */
class OnlineUpdates : public QObject {
    Q_OBJECT
public:
    explicit OnlineUpdates(QObject* parent = nullptr);

    /** Runs whichever checks are switched on and have not run today. */
    void checkDaily();
    /** manual: report "up to date" and failures too, and ignore a skipped version. */
    void checkApp(bool manual);
    void syncPresets(bool manual);
    bool syncing() const { return m_syncing; }

    static bool appCheckEnabled();
    static bool presetSyncEnabled();
    static void setAppCheckEnabled(bool on);
    static void setPresetSyncEnabled(bool on);
    /** "Skip this version": no more offers until a newer one is out. */
    static void skipVersion(const QString& version);

signals:
    void appUpdateAvailable(const QString& version, const QUrl& page);
    void appUpToDate();
    void appCheckFailed(const QString& message);
    void presetsSynced(int added, int updated, int removed);
    void presetSyncFailed(const QString& message);

private:
    QNetworkReply* get(const QUrl& url);
    void handleIndex(const QByteArray& json, bool manual);
    void downloadNext();
    void finishSync();

    QNetworkAccessManager* m_net;
    bool m_syncing = false;
    bool m_manualSync = false;
    struct Pending {
        QString path;     /**< <category>/<name>.flyglow */
        QByteArray sha256;
        bool replaces = false;
    };
    QList<Pending> m_pending;
    int m_added = 0, m_updated = 0, m_removed = 0;
    QStringList m_failed;
};

} // namespace fg
