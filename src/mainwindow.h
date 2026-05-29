#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <KXmlGuiWindow>

#include <memory>

class QWebEngineFullScreenRequest;
class QWebEngineNewWindowRequest;
class QWebEngineNotification;
class QWebEngineView;
class QWebEnginePermission;
class KStatusNotifierItem;

class MainWindow : public KXmlGuiWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void showSignInPage();
    bool importFromFirefox();
    bool importFromChrome();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void setupActions();
    void setupTrayIcon();
    bool isAutostartEnabled() const;
    void setAutostartEnabled(bool enabled);
    void handlePermission(QWebEnginePermission permission);
    void handleNotification(std::unique_ptr<QWebEngineNotification> notification);
    void handleFullScreenRequest(QWebEngineFullScreenRequest request);
    void handleNewWindowRequest(QWebEngineNewWindowRequest &request);
    void signIn();

    QWebEngineView *m_webView = nullptr;
    KStatusNotifierItem *m_trayIcon = nullptr;
};

#endif
