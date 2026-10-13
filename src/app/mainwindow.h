// Main window. Three parts:
//   Browse  - equipment type, filter, item list, bookmarks
//   Item    - the recipe tree or the material list for the chosen item
//   Info    - the full write-up for a tapped bubble (an item or a monster)
// On a phone they are one screen at a time and Back (Android's back gesture, or Esc) steps back.
// On a desktop, Browse and Item sit side by side and each Info opens in its own window.
#pragma once

#include "app/workers.h"
#include "core/store.h"
#include "core/tree.h"

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QPixmap>
#include <QSet>

#include <functional>

class MaterialListPanel;
class QButtonGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QSpinBox;
class QStackedWidget;
class QTextBrowser;
class QTimer;
class QToolButton;
class TreeView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(bool wide);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    QWidget *buildBrowsePage();
    QWidget *buildItemPage();
    QWidget *buildInfoPage();
    QWidget *header(QWidget *left, QLabel *title, QList<QWidget *> right);
    void showPage(QWidget *page);
    bool goBack();
    void setStatus(const QString &text, int ms = 0);

    // loading
    void startLoad(bool force);
    void onReady(RegistryPtr registry, const QString &note);
    void onFailed(const QString &msg);
    void setRegistry(RegistryPtr registry);
    void importRecipes();
    void importSheet();
    void rebuildRegistry();

    // browse
    void fillItems();
    void itemTapped(QListWidgetItem *it);
    void itemsMenu(const QPoint &pos);
    void showBookmarks();

    // item page
    void openItem(const dq9::ItemPtr &info);
    void draw();
    void modeChanged();
    void updateBookmarkButton();
    void toggleBookmark(const dq9::ItemInfo &info);
    void itemPageMenu();

    // info page
    void openNode(const dq9::NodePtr &node);
    void openItemInfo(const QString &key, const QString &name);
    void openInfo(const QString &title, std::function<dq9::Rendered()> render);
    void refreshInfo();
    void refreshInfoView(QTextBrowser *view, const std::function<dq9::Rendered()> &render);

    // icons and look-ups
    QPair<bool, QPixmap> getSprite(const QString &name);
    void spriteReady(const QString &name);
    QStringList foundFor(const QString &name);
    void detailsReady(const QString &name);

    dq9::Store m_store;
    RegistryPtr m_registry;
    dq9::ItemPtr m_current;
    QList<dq9::Bookmark> m_bookmarks;
    LoadWorker *m_loader = nullptr;
    LocWorker *m_loc;
    DetailWorker *m_details;
    SpriteWorker *m_sprites;
    QHash<QString, QPixmap> m_pixmaps;
    QSet<QString> m_foundWait;   // items the tree is waiting on an item page for
    QTimer *m_rerenderTimer;     // batches look-up results into one info-page refresh
    QTimer *m_redrawTimer;       // batches item-page results into one tree redraw
    bool m_robotsNoticeShown = false;

    bool m_wide;
    QStackedWidget *m_pages = nullptr;   // phone layout only
    QWidget *m_browsePage, *m_itemPage, *m_infoPage;
    QList<QWidget *> m_history;

    // browse page
    QComboBox *m_category;
    QLineEdit *m_filter;
    QListWidget *m_items;
    QLabel *m_status;

    // item page
    QLabel *m_itemTitle;
    QWidget *m_itemBack;
    QToolButton *m_bookmarkBtn;
    QButtonGroup *m_modeGroup;
    QComboBox *m_prefer;
    QSpinBox *m_perN;
    QWidget *m_treeControls;
    QStackedWidget *m_itemStack;
    TreeView *m_view;
    MaterialListPanel *m_matlist;

    // info page
    QLabel *m_infoTitle;
    QTextBrowser *m_infoView;
    std::function<dq9::Rendered()> m_infoRender;
    struct InfoWindow {   // desktop layout: one window per opened bubble
        QPointer<QTextBrowser> view;
        std::function<dq9::Rendered()> render;
    };
    QList<InfoWindow> m_infoWindows;
};
