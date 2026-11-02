#include "core/Effect.h"
#include "core/KeyframeEffect.h"
#include "core/MediaEffect.h"
#include "shader/ShaderEffect.h"
#include "shader/ShaderRenderer.h"
#include "ui/Language.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSettings>
#include <memory>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

/** One FlyGlow per user, and per folder for portable copies, which keep their own settings. */
QString instanceName()
{
    QString name = QStringLiteral("FlyGlow-") + qEnvironmentVariable("USERNAME");
    const QString appDir = QCoreApplication::applicationDirPath();
    if (QFileInfo::exists(appDir + "/portable.txt"))
        name += QLatin1Char('-')
              + QString::fromLatin1(
                    QCryptographicHash::hash(appDir.toLower().toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
    return name;
}

/** Sends the files to a FlyGlow that is already running; false when there is none. */
bool handOver(const QString& name, const QStringList& files)
{
    QLocalSocket socket;
    socket.connectToServer(name);
    if (!socket.waitForConnected(500))
        return false;
    // Lets the running window come to the front; Windows otherwise only flashes its taskbar button.
    AllowSetForegroundWindow(ASFW_ANY);
    socket.write(files.join(QLatin1Char('\n')).toUtf8());
    socket.waitForBytesWritten(1000);
    socket.disconnectFromServer();
    if (socket.state() != QLocalSocket::UnconnectedState)
        socket.waitForDisconnected(1000);
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("FlyGlow");
    QApplication::setOrganizationName("FlyGlow");
    QApplication::setApplicationVersion(FLYGLOW_VERSION);
    // The .ico has hand-tuned small sizes; the SVG blurs below 32 px.
    QApplication::setWindowIcon(QIcon(":/ui/flyglow.ico"));
    if (QFileInfo::exists(QCoreApplication::applicationDirPath() + "/portable.txt")) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QCoreApplication::applicationDirPath() + "/data");
    }

    QStringList files;
    for (const QString& arg : QApplication::arguments().mid(1))
        files << QFileInfo(arg).absoluteFilePath();
    const QString instance = instanceName();
    if (handOver(instance, files))
        return 0;
    // Listening this early makes a launch during start-up queue its files instead of opening a second window.
    QLocalServer server;
    server.listen(instance);

    fg::installLanguage(QSettings().value("ui/language", "en").toString());
    fg::registerBuiltinEffects();
    fg::EffectRegistry::instance().add<fg::KeyframeEffect>();
    fg::EffectRegistry::instance().add<fg::MediaEffect>();
    fg::EffectRegistry::instance().add<fg::ShaderEffect>();
    fg::theme::apply(app);

    // Its GL context has to go before the application object, and after every window that renders shaders.
    fg::ShaderRenderer shaderRenderer;
    fg::MainWindow w;
    w.show();
    if (!files.isEmpty())
        w.openExternal(files);

    // Later launches, such as opening a .flyglow file from Explorer, hand their files to this window.
    auto accept = [&server, &w] {
        while (QLocalSocket* socket = server.nextPendingConnection()) {
            auto received = std::make_shared<QByteArray>();
            auto finish = [socket, received, &w] {
                received->append(socket->readAll());
                socket->deleteLater();
                w.openExternal(QString::fromUtf8(*received).split(QLatin1Char('\n'), Qt::SkipEmptyParts));
            };
            QObject::connect(socket, &QLocalSocket::readyRead, socket,
                             [socket, received] { received->append(socket->readAll()); });
            if (socket->state() == QLocalSocket::UnconnectedState)
                finish();
            else
                QObject::connect(socket, &QLocalSocket::disconnected, &w, finish);
        }
    };
    QObject::connect(&server, &QLocalServer::newConnection, &w, accept);
    accept();
    return app.exec();
}
