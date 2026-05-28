#include <QApplication>
#include <QDir>
#include <QFile>
#include <QModelIndex>
#include <QStandardPaths>
#include <KAboutData>
#include <KColorSchemeManager>
#include <KCrash>
#include <KDBusService>
#include <KLocalizedString>

#include "mainwindow.h"

static void installColorScheme()
{
    auto dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
               + QStringLiteral("/color-schemes");
    QDir().mkpath(dir);
    auto dest = dir + QStringLiteral("/KSpotifyBlack.colors");
    if (QFile::exists(dest))
        return;

    // Try system-installed copy first
    auto src = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                      QStringLiteral("color-schemes/kspotify.colors"));
    if (!src.isEmpty()) {
        QFile::copy(src, dest);
        return;
    }

    // Generate at runtime for uninstalled dev builds
    QFile file(dest);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(
            "[ColorEffects:Disabled]\nColor=56,56,56\nColorAmount=0\nColorEffect=0\n"
            "ContrastAmount=0.65\nContrastEffect=1\nIntensityAmount=0.1\nIntensityEffect=2\n\n"
            "[ColorEffects:Inactive]\nChangeSelectionColor=true\nColor=112,111,110\n"
            "ColorAmount=0.025\nColorEffect=2\nContrastAmount=0.1\nContrastEffect=2\n"
            "Enable=false\nIntensityAmount=0\nIntensityEffect=0\n\n"
            "[Colors:Button]\nBackgroundNormal=30,30,30\nForegroundNormal=252,252,252\n\n"
            "[Colors:Header]\nBackgroundNormal=0,0,0\nForegroundNormal=252,252,252\n\n"
            "[Colors:Header][Inactive]\nBackgroundNormal=0,0,0\nForegroundNormal=252,252,252\n\n"
            "[Colors:Selection]\nBackgroundNormal=61,174,233\nForegroundNormal=252,252,252\n\n"
            "[Colors:View]\nBackgroundNormal=0,0,0\nForegroundNormal=252,252,252\n\n"
            "[Colors:Window]\nBackgroundNormal=0,0,0\nForegroundNormal=252,252,252\n\n"
            "[Colors:Complementary]\nBackgroundNormal=0,0,0\nForegroundNormal=252,252,252\n\n"
            "[Colors:Tooltip]\nBackgroundNormal=24,24,24\nForegroundNormal=252,252,252\n\n"
            "[General]\nColorScheme=KSpotifyBlack\nName=KSpotify Black\n\n"
            "[KDE]\ncontrast=4\n\n"
            "[WM]\nactiveBackground=0,0,0\nactiveBlend=252,252,252\n"
            "activeForeground=252,252,252\ninactiveBackground=0,0,0\n"
            "inactiveBlend=161,169,177\ninactiveForeground=161,169,177\n"
        );
    }
}

int main(int argc, char *argv[])
{
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--autoplay-policy=no-user-gesture-required");

    QApplication app(argc, argv);

    KAboutData aboutData(
        QStringLiteral("kspotify"),
        i18n("KSpotify"),
        QStringLiteral("1.0.1"), // x-release-please-version
        i18n("Spotify Web Player for KDE"),
        KAboutLicense::GPL_V3,
        i18n("© 2025–2026 KSpotify contributors"),
        QString(),
        QStringLiteral("https://github.com/iharob/kspotify")
    );
    aboutData.setDesktopFileName(QStringLiteral("org.kde.kspotify"));

    KAboutData::setApplicationData(aboutData);
    QApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("kspotify")));

    installColorScheme();
    auto *manager = KColorSchemeManager::instance();
    auto idx = manager->indexForScheme(QStringLiteral("KSpotify Black"));
    if (idx.isValid())
        manager->activateScheme(idx);

    KCrash::initialize();

    KDBusService service(KDBusService::Unique);

    auto *window = new MainWindow;
    window->show();

    QObject::connect(&service, &KDBusService::activateRequested, window, [window]() {
        window->show();
        window->raise();
        window->activateWindow();
    });

    return app.exec();
}
