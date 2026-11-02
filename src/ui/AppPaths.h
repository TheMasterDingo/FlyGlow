#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace fg {

/** data\ next to a portable exe (one with portable.txt beside it), else the user's AppData folder; created on use. */
inline QString appDataDir()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString dir = QFileInfo::exists(appDir + "/portable.txt")
                            ? appDir + "/data"
                            : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir;
}

} // namespace fg
