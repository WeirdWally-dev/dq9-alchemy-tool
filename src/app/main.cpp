// DQ9 Alchemy Helper: pick an equipment type, pick an item, see how to get it.
//
// The first launch downloads the enemy list and the weapon list (one request each) and caches
// them. Monster locations and item details are looked up on demand (one polite request each,
// cached forever).
#include "app/mainwindow.h"
#include "app/theme.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("DQ9 Alchemy Helper"));
    QApplication::setOrganizationName(QStringLiteral("WeirdWally-dev"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    qRegisterMetaType<RegistryPtr>("RegistryPtr");
    theme::applyDark(app);

    MainWindow win;
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    win.showMaximized();
#else
    win.resize(430, 860);   // phone-shaped on desktop too, so the desktop build previews the mobile layout
    win.show();
#endif
    return app.exec();
}
