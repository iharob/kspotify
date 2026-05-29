#include "mainwindow.h"

#include <QAction>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStandardPaths>
#include <QNetworkCookie>
#include <QProcess>
#include <QTimer>
#include <QWebEngineCookieStore>
#include <QWebEngineFullScreenRequest>
#include <QWebEngineNewWindowRequest>
#include <QWebEngineNotification>
#include <QWebEnginePage>
#include <QWebEnginePermission>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineView>

#include <KActionCollection>
#include <KLocalizedString>
#include <KNotification>
#include <KStandardAction>
#include <KStatusNotifierItem>

static const QString userAgent = QStringLiteral(
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/137.0.0.0 Safari/537.36");

class ChromeRequestInterceptor : public QWebEngineUrlRequestInterceptor
{
public:
    using QWebEngineUrlRequestInterceptor::QWebEngineUrlRequestInterceptor;

    void interceptRequest(QWebEngineUrlRequestInfo &info) override
    {
        info.setHttpHeader("Sec-CH-UA",
            R"("Chromium";v="137", "Google Chrome";v="137", "Not/A)Brand";v="24")");
        info.setHttpHeader("Sec-CH-UA-Mobile", "?0");
        info.setHttpHeader("Sec-CH-UA-Platform", "\"Linux\"");
    }
};

class SpotifyPage : public QWebEnginePage
{
    Q_OBJECT

public:
    using QWebEnginePage::QWebEnginePage;

protected:
    bool acceptNavigationRequest(const QUrl &url, NavigationType, bool isMainFrame) override
    {
        if (!isMainFrame)
            return true;

        const auto host = url.host();

        // Intercept OAuth providers — open in system browser
        if (host.endsWith(QStringLiteral("accounts.google.com"))
            || host.endsWith(QStringLiteral("appleid.apple.com"))) {
            QDesktopServices::openUrl(url);
            auto *w = qobject_cast<MainWindow *>(parent());
            if (w) {
                QTimer::singleShot(0, w, &MainWindow::showSignInPage);
            }
            return false;
        }

        // Handle "done" callback from the sign-in page
        if (url.scheme() == QStringLiteral("kspotify")
            && url.host() == QStringLiteral("auth-done")) {
            auto *w = qobject_cast<MainWindow *>(parent());
            if (w) {
                QTimer::singleShot(0, w, [w]() {
                    if (w->importFromFirefox() || w->importFromChrome())
                        return;
                    QMessageBox::warning(w, i18n("Import Failed"),
                        i18n("Could not find Spotify cookies in Firefox or Chrome.\n"
                             "Make sure you completed login in your browser."));
                });
            }
            return false;
        }

        return true;
    }
};

MainWindow::MainWindow(QWidget *parent)
    : KXmlGuiWindow(parent)
{
    auto *profile = new QWebEngineProfile(QStringLiteral("kspotify"), this);
    profile->setHttpUserAgent(userAgent);
    profile->setUrlRequestInterceptor(new ChromeRequestInterceptor(profile));
    profile->setNotificationPresenter([this](std::unique_ptr<QWebEngineNotification> n) {
        handleNotification(std::move(n));
    });

    auto *page = new SpotifyPage(profile, this);
    m_webView = new QWebEngineView(this);
    m_webView->setPage(page);
    m_webView->setContextMenuPolicy(Qt::NoContextMenu);

    auto *settings = m_webView->page()->settings();
    settings->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);
    settings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    settings->setAttribute(QWebEngineSettings::JavascriptCanAccessClipboard, true);

    QWebEngineScript cssScript;
    cssScript.setSourceCode(QStringLiteral(
        "(function() {"
        "  var style = document.createElement('style');"
        "  style.textContent = '"
        "    html, body { margin: 0 !important; padding: 0 !important; overflow: hidden; }"
        "    a[href=\"/download\"] { display: none !important; }"
        "    [data-testid=\"download-desktop-app-banner\"] { display: none !important; }"
        "  ';"
        "  document.documentElement.appendChild(style);"
        "})();"
    ));
    cssScript.setInjectionPoint(QWebEngineScript::DocumentReady);
    cssScript.setWorldId(QWebEngineScript::ApplicationWorld);
    cssScript.setRunsOnSubFrames(false);
    profile->scripts()->insert(cssScript);

    QWebEngineScript chromeScript;
    chromeScript.setSourceCode(QStringLiteral(
        "Object.defineProperty(navigator, 'webdriver', { get: () => undefined });"
        "window.chrome = { runtime: {}, csi: function() {}, loadTimes: function() {} };"
        "window.addEventListener('beforeinstallprompt', function(e) { e.preventDefault(); });"
    ));
    chromeScript.setInjectionPoint(QWebEngineScript::DocumentCreation);
    chromeScript.setWorldId(QWebEngineScript::MainWorld);
    chromeScript.setRunsOnSubFrames(true);
    profile->scripts()->insert(chromeScript);

    connect(m_webView->page(), &QWebEnginePage::permissionRequested,
            this, &MainWindow::handlePermission);
    connect(m_webView->page(), &QWebEnginePage::fullScreenRequested,
            this, &MainWindow::handleFullScreenRequest);
    connect(m_webView->page(), &QWebEnginePage::newWindowRequested,
            this, &MainWindow::handleNewWindowRequest);

    setCentralWidget(m_webView);
    m_webView->load(QUrl(QStringLiteral("https://open.spotify.com")));

    setupActions();
    setupTrayIcon();

    setupGUI(ToolBar | Keys | Save | Create, QStringLiteral("kspotifyui.rc"));

    menuBar()->hide();

    resize(1280, 800);
}

MainWindow::~MainWindow() = default;

void MainWindow::showSignInPage()
{
    m_webView->setHtml(QStringLiteral(
        "<html><body style='background:#121212;color:#fff;font-family:sans-serif;"
        "display:flex;justify-content:center;align-items:center;height:100vh;margin:0'>"
        "<div style='text-align:center;max-width:420px'>"
        "<h2 style='margin-bottom:24px'>Complete sign-in in your browser</h2>"
        "<p style='color:#b3b3b3;line-height:1.6;margin-bottom:32px'>"
        "A browser window has been opened for you to log in. "
        "Once you're done, click the button below.</p>"
        "<a href='kspotify://auth-done' style='"
        "display:inline-block;background:#1DB954;color:#000;font-weight:bold;"
        "padding:14px 48px;border-radius:500px;text-decoration:none;font-size:16px"
        "'>I've logged in</a>"
        "</div></body></html>"
    ));
}

void MainWindow::setupActions()
{
    KStandardAction::quit(qApp, &QCoreApplication::quit, actionCollection());

    auto *reload = new QAction(QIcon::fromTheme(QStringLiteral("view-refresh")),
                               i18n("&Reload"), this);
    actionCollection()->addAction(QStringLiteral("reload"), reload);
    actionCollection()->setDefaultShortcut(reload, Qt::Key_F5);
    connect(reload, &QAction::triggered, this, [this]() {
        m_webView->reload();
    });

    auto *home = new QAction(QIcon::fromTheme(QStringLiteral("go-home")),
                             i18n("&Home"), this);
    actionCollection()->addAction(QStringLiteral("home"), home);
    actionCollection()->setDefaultShortcut(home, QKeySequence(Qt::CTRL | Qt::Key_Home));
    connect(home, &QAction::triggered, this, [this]() {
        m_webView->load(QUrl(QStringLiteral("https://open.spotify.com")));
    });
}

void MainWindow::setupTrayIcon()
{
    m_trayIcon = new KStatusNotifierItem(this);
    m_trayIcon->setCategory(KStatusNotifierItem::ApplicationStatus);
    if (QIcon::hasThemeIcon(QStringLiteral("kspotify-tray")))
        m_trayIcon->setIconByName(QStringLiteral("kspotify-tray"));
    else
        m_trayIcon->setIconByName(QStringLiteral("kspotify"));
    m_trayIcon->setStandardActionsEnabled(true);

    auto *autostart = new QAction(i18n("Start automatically at login"), this);
    autostart->setCheckable(true);
    autostart->setChecked(isAutostartEnabled());
    connect(autostart, &QAction::toggled, this, &MainWindow::setAutostartEnabled);
    m_trayIcon->contextMenu()->addAction(autostart);

    m_trayIcon->setToolTipTitle(i18n("KSpotify"));
    m_trayIcon->setToolTipSubTitle(i18n("Spotify Web Player"));

    connect(m_trayIcon, &KStatusNotifierItem::activateRequested, this, [this]() {
        if (isVisible() && !isMinimized()) {
            hide();
        } else {
            show();
            raise();
            activateWindow();
        }
    });
}

static QString autostartDesktopPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/autostart/org.kde.kspotify.desktop");
}

bool MainWindow::isAutostartEnabled() const
{
    return QFile::exists(autostartDesktopPath());
}

void MainWindow::setAutostartEnabled(bool enabled)
{
    const QString path = autostartDesktopPath();
    QFile::remove(path);
    if (!enabled)
        return;

    QDir().mkpath(QFileInfo(path).absolutePath());

    const QString installed = QStandardPaths::locate(
        QStandardPaths::ApplicationsLocation, QStringLiteral("org.kde.kspotify.desktop"));
    if (!installed.isEmpty()) {
        QFile::copy(installed, path);
        return;
    }

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Name=KSpotify\n"
            "Exec=kspotify\n"
            "Icon=kspotify\n"
            "Terminal=false\n"
            "X-GNOME-Autostart-enabled=true\n");
    }
}

void MainWindow::handlePermission(QWebEnginePermission permission)
{
    const auto origin = permission.origin();
    if (origin.host().endsWith(QStringLiteral("spotify.com"))) {
        permission.grant();
        return;
    }
    permission.deny();
}

void MainWindow::handleNotification(std::unique_ptr<QWebEngineNotification> webNotification)
{
    auto *knotify = new KNotification(QStringLiteral("webNotification"),
                                       KNotification::CloseOnTimeout, this);
    knotify->setTitle(webNotification->title());
    knotify->setText(webNotification->message());

    const auto icon = webNotification->icon();
    if (!icon.isNull())
        knotify->setPixmap(QPixmap::fromImage(icon));

    auto *raw = webNotification.release();

    auto *defaultAction = knotify->addDefaultAction(i18n("Open"));
    connect(defaultAction, &KNotificationAction::activated, this, [this, raw]() {
        raw->click();
        show();
        raise();
        activateWindow();
    });
    connect(knotify, &KNotification::closed, raw, &QWebEngineNotification::close);
    connect(raw, &QWebEngineNotification::closed, knotify, &KNotification::close);

    knotify->sendEvent();
}

void MainWindow::handleFullScreenRequest(QWebEngineFullScreenRequest request)
{
    if (request.toggleOn())
        showFullScreen();
    else
        showNormal();
    request.accept();
}

void MainWindow::handleNewWindowRequest(QWebEngineNewWindowRequest &request)
{
    const QUrl url = request.requestedUrl();
    const auto host = url.host();

    if (host.endsWith(QStringLiteral("spotify.com"))
        || host.endsWith(QStringLiteral("facebook.com"))) {
        auto *popup = new QWebEngineView(m_webView->page()->profile());
        popup->setAttribute(Qt::WA_DeleteOnClose);
        popup->resize(500, 700);
        popup->setWindowTitle(QStringLiteral("KSpotify"));
        request.openIn(popup->page());
        connect(popup->page(), &QWebEnginePage::windowCloseRequested,
                popup, &QWidget::close);
        popup->show();
    } else {
        QDesktopServices::openUrl(url);
    }
}

bool MainWindow::importFromFirefox()
{
    QDir firefoxDir(QDir::homePath() + QStringLiteral("/.mozilla/firefox"));
    if (!firefoxDir.exists())
        return false;

    const auto profiles = firefoxDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto &profile : profiles) {
        auto cookiesDb = firefoxDir.filePath(profile + QStringLiteral("/cookies.sqlite"));
        if (!QFile::exists(cookiesDb))
            continue;

        auto tempDb = QDir::tempPath() + QStringLiteral("/kspotify-import.sqlite");
        QFile::remove(tempDb);
        if (!QFile::copy(cookiesDb, tempDb))
            continue;

        QProcess proc;
        proc.start(QStringLiteral("sqlite3"),
                   {QStringLiteral("-separator"), QStringLiteral("|"), tempDb,
                    QStringLiteral("SELECT name, value, host, path, expiry, isSecure, isHttpOnly "
                                   "FROM moz_cookies WHERE host LIKE '%.spotify.com'")});
        proc.waitForFinished(5000);
        QFile::remove(tempDb);

        auto output = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        if (output.isEmpty())
            continue;

        auto *store = m_webView->page()->profile()->cookieStore();
        bool found = false;

        const auto lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const auto &line : lines) {
            auto parts = line.split(QLatin1Char('|'));
            if (parts.size() < 7)
                continue;

            QNetworkCookie cookie;
            cookie.setName(parts[0].toUtf8());
            cookie.setValue(parts[1].toUtf8());
            cookie.setDomain(parts[2]);
            cookie.setPath(parts[3]);
            auto expiry = parts[4].toLongLong();
            if (expiry > 0)
                cookie.setExpirationDate(QDateTime::fromSecsSinceEpoch(expiry));
            cookie.setSecure(parts[5] == QLatin1String("1"));
            cookie.setHttpOnly(parts[6].trimmed() == QLatin1String("1"));

            store->setCookie(cookie, QUrl(QStringLiteral("https://open.spotify.com")));
            found = true;
        }

        if (found) {
            m_webView->load(QUrl(QStringLiteral("https://open.spotify.com")));
            return true;
        }
    }
    return false;
}

bool MainWindow::importFromChrome()
{
    // Try Chrome, then Chromium
    struct BrowserInfo {
        QString configDir;
        QString walletFolder;
    };
    const BrowserInfo browsers[] = {
        {QDir::homePath() + QStringLiteral("/.config/google-chrome"), QStringLiteral("Chrome Safe Storage")},
        {QDir::homePath() + QStringLiteral("/.config/chromium"), QStringLiteral("Chromium Safe Storage")},
    };

    for (const auto &browser : browsers) {
        auto cookiesDb = browser.configDir + QStringLiteral("/Default/Cookies");
        if (!QFile::exists(cookiesDb))
            continue;

        // Get encryption password from KWallet
        QProcess kwallet;
        kwallet.start(QStringLiteral("kwallet-query"),
                      {QStringLiteral("-f"), browser.walletFolder,
                       QStringLiteral("-r"), browser.walletFolder,
                       QStringLiteral("kdewallet")});
        kwallet.waitForFinished(5000);
        auto password = QString::fromUtf8(kwallet.readAllStandardOutput()).trimmed();
        if (password.isEmpty())
            continue;

        // Derive AES key via PBKDF2 and decrypt cookies using openssl
        // Key = PBKDF2(password, "saltysalt", 1 iteration, 16 bytes, SHA1)
        auto tempDb = QDir::tempPath() + QStringLiteral("/kspotify-chrome.sqlite");
        QFile::remove(tempDb);
        if (!QFile::copy(cookiesDb, tempDb))
            continue;

        // Get encrypted cookie values
        QProcess sqlite;
        sqlite.start(QStringLiteral("sqlite3"),
                     {QStringLiteral("-separator"), QStringLiteral("|"), tempDb,
                      QStringLiteral("SELECT name, hex(encrypted_value), host_key, path, "
                                     "expires_utc, is_secure, is_httponly "
                                     "FROM cookies WHERE host_key LIKE '%.spotify.com'")});
        sqlite.waitForFinished(5000);
        QFile::remove(tempDb);

        auto output = QString::fromUtf8(sqlite.readAllStandardOutput()).trimmed();
        if (output.isEmpty())
            continue;

        // Derive key: PBKDF2-SHA1(password, "saltysalt", 1, 16)
        QProcess deriveKey;
        deriveKey.start(QStringLiteral("openssl"),
                        {QStringLiteral("kdf"), QStringLiteral("-keylen"), QStringLiteral("16"),
                         QStringLiteral("-kdfopt"), QStringLiteral("digest:SHA1"),
                         QStringLiteral("-kdfopt"), QStringLiteral("pass:") + password,
                         QStringLiteral("-kdfopt"), QStringLiteral("salt:saltysalt"),
                         QStringLiteral("-kdfopt"), QStringLiteral("iter:1"),
                         QStringLiteral("PBKDF2")});
        deriveKey.waitForFinished(5000);
        auto keyHex = QString::fromUtf8(deriveKey.readAllStandardOutput()).trimmed()
                      .remove(QLatin1Char(':')).remove(QLatin1Char('\n'));
        if (keyHex.isEmpty())
            continue;

        auto *store = m_webView->page()->profile()->cookieStore();
        bool found = false;

        const auto lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const auto &line : lines) {
            auto parts = line.split(QLatin1Char('|'));
            if (parts.size() < 7)
                continue;

            auto encHex = parts[1];
            // v11 prefix = 763131 in hex (3 bytes = 6 hex chars)
            if (!encHex.startsWith(QStringLiteral("763131")))
                continue;
            auto cipherHex = encHex.mid(6);

            // Decrypt: AES-128-CBC, IV = 16 spaces (0x20)
            QProcess decrypt;
            decrypt.start(QStringLiteral("bash"),
                          {QStringLiteral("-c"),
                           QStringLiteral("echo '%1' | xxd -r -p | "
                                          "openssl enc -d -aes-128-cbc -K '%2' "
                                          "-iv '20202020202020202020202020202020'")
                           .arg(cipherHex, keyHex)});
            decrypt.waitForFinished(2000);
            auto value = QString::fromUtf8(decrypt.readAllStandardOutput());
            if (value.isEmpty())
                continue;

            QNetworkCookie cookie;
            cookie.setName(parts[0].toUtf8());
            cookie.setValue(value.toUtf8());
            cookie.setDomain(parts[2]);
            cookie.setPath(parts[3]);
            // Chrome stores expires_utc as microseconds since 1601-01-01
            auto expiresUtc = parts[4].toLongLong();
            if (expiresUtc > 0) {
                // Convert Chrome epoch (1601) to Unix epoch (1970): subtract 11644473600 seconds
                auto unixSecs = (expiresUtc / 1000000) - 11644473600LL;
                cookie.setExpirationDate(QDateTime::fromSecsSinceEpoch(unixSecs));
            }
            cookie.setSecure(parts[5] == QLatin1String("1"));
            cookie.setHttpOnly(parts[6].trimmed() == QLatin1String("1"));

            store->setCookie(cookie, QUrl(QStringLiteral("https://open.spotify.com")));
            found = true;
        }

        if (found) {
            m_webView->load(QUrl(QStringLiteral("https://open.spotify.com")));
            return true;
        }
    }
    return false;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_trayIcon) {
        hide();
        event->ignore();
        return;
    }
    KXmlGuiWindow::closeEvent(event);
}

#include "mainwindow.moc"
