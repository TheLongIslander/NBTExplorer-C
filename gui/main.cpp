#include <QApplication>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QIcon>
#include <QTimer>
#include <QStyleFactory>

#include "MainWindow.h"

#ifndef CNBT_VERSION
#define CNBT_VERSION "development"
#endif

class CnbtApplication final : public QApplication {
public:
    using QApplication::QApplication;

    void setMainWindow(MainWindow* window) {
        window_ = window;
        if (window_ && !pendingPaths_.isEmpty()) {
            window_->openPaths(pendingPaths_);
            pendingPaths_.clear();
        }
    }

protected:
    bool event(QEvent* event) override {
        if (event->type() == QEvent::FileOpen) {
            auto* openEvent = static_cast<QFileOpenEvent*>(event);
            if (!openEvent->file().isEmpty()) {
                if (window_) window_->openPaths({openEvent->file()});
                else pendingPaths_ << openEvent->file();
                return true;
            }
        }
        return QApplication::event(event);
    }

private:
    MainWindow* window_ = nullptr;
    QStringList pendingPaths_;
};

int main(int argc, char* argv[]) {
    CnbtApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("C-NBT Explorer"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("io.github.cnbt-explorer"));
    QCoreApplication::setApplicationName(QStringLiteral("C-NBT Explorer"));
    QCoreApplication::setApplicationVersion(QStringLiteral(CNBT_VERSION));
    application.setWindowIcon(QIcon(QStringLiteral(":/icons/cnbt-explorer.svg")));

    MainWindow window;
    application.setMainWindow(&window);
    window.show();

    QStringList paths;
    const QStringList arguments = application.arguments();
    for (int i = 1; i < arguments.size(); ++i) {
        if (QFileInfo::exists(arguments[i])) paths << arguments[i];
    }
    if (!paths.isEmpty()) window.openPaths(paths);

    const QString screenshotPath = qEnvironmentVariable("CNBT_SCREENSHOT_PATH");
    if (!screenshotPath.isEmpty()) {
        QTimer::singleShot(500, &window, [&window, screenshotPath, &application] {
            window.grab().save(screenshotPath);
            application.quit();
        });
    }
    return application.exec();
}
