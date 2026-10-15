#include "app/mainwindow.h"

#include "app/materiallist.h"
#include "app/theme.h"
#include "app/treeview.h"
#include "core/parsers.h"
#include "core/text.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QMessageBox>
#include <QDialog>
#include <QSplitter>
#include <QScrollBar>
#include <QScroller>
#include <QShortcut>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

using namespace dq9;

namespace {

// A combo box whose list opens as the phone menu page instead of Qt's pop-up.
class PageCombo : public QComboBox {
public:
    std::function<void()> onPopup;
    void showPopup() override
    {
        if (onPopup)
            onPopup();
        else
            QComboBox::showPopup();
    }
};

constexpr int ROLE_KEY = Qt::UserRole;
constexpr int ROLE_MARK_KEY = Qt::UserRole + 1;
constexpr int ROLE_GLOSS = Qt::UserRole + 2;   // glossary id: the row opens the used-in page

// List modes that sit above the equipment types in the type list.
const QString SEARCH_CAT = QStringLiteral("\U0001F50D Search all items");
const QString GLOSSARY_CAT = QStringLiteral("\U0001F4D6 Glossary: what it's used in");
constexpr int SPECIAL_CATS = 3;   // bookmarks, search, glossary

const char *ROBOTS_MSG = QT_TRANSLATE_NOOP(
    "MainWindow",
    "No alchemy recipes are loaded yet.\n\n"
    "The recipe list that ships with the app couldn't be read, and woodus.com's robots.txt asks automated "
    "tools not to fetch its recipes page, so this app won't.\n\n"
    "To use that page instead: open it in a browser, save it as a web page (HTML only), then choose "
    "\"Import woodus page...\" from the menu and pick the saved file.\n\n"
    "Until then you can still browse monster drops and locations.");

QToolButton *toolButton(const QString &text, const QString &tip = QString())
{
    auto *b = new QToolButton;
    b->setText(text);
    b->setToolTip(tip);
    b->setAutoRaise(true);
    b->setToolButtonStyle(Qt::ToolButtonTextOnly);
    QFont f = b->font();
    f.setPixelSize(20);
    b->setFont(f);
    return b;
}

void enableKineticScroll(QAbstractScrollArea *area)
{
    QScroller::grabGesture(area->viewport(), QScroller::LeftMouseButtonGesture);
}

} // namespace

MainWindow::MainWindow(bool wide)
    : m_wide(wide)
{
    setWindowTitle(tr("DQ9 Alchemy Helper v%1").arg(QStringLiteral(APP_VERSION)));

    m_rerenderTimer = new QTimer(this);
    m_rerenderTimer->setSingleShot(true);
    m_rerenderTimer->setInterval(150);
    connect(m_rerenderTimer, &QTimer::timeout, this, &MainWindow::refreshInfo);
    m_redrawTimer = new QTimer(this);
    m_redrawTimer->setSingleShot(true);
    m_redrawTimer->setInterval(250);
    connect(m_redrawTimer, &QTimer::timeout, this, &MainWindow::draw);

    m_loc = new LocWorker(m_store, this);
    connect(m_loc, &QueueWorker::done, this, [this] { m_rerenderTimer->start(); });
    m_loc->start();
    m_details = new DetailWorker(m_store, this);
    connect(m_details, &QueueWorker::done, this, &MainWindow::detailsReady);
    m_details->start();
    m_sprites = new SpriteWorker(m_store, this);
    connect(m_sprites, &QueueWorker::done, this, &MainWindow::spriteReady);
    m_sprites->start();
    m_bookmarks = m_store.loadBookmarks();

    m_browsePage = buildBrowsePage();
    m_itemPage = buildItemPage();
    m_infoPage = buildInfoPage();
    m_usedPage = buildUsedInPage();
    if (m_wide) {   // desktop: list on the left, tree on the right, details in their own windows
        auto *split = new QSplitter(Qt::Horizontal);
        split->addWidget(m_browsePage);
        m_rightStack = new QStackedWidget;
        m_rightStack->addWidget(m_itemPage);
        m_rightStack->addWidget(m_usedPage);
        split->addWidget(m_rightStack);
        split->setStretchFactor(0, 1);
        split->setStretchFactor(1, 3);
        split->setSizes({300, 850});
        setCentralWidget(split);
        m_itemBack->hide();
        m_infoPage->hide();
        m_view->message(tr("Select an item on the left."));
        connect(m_items, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *it) { itemTapped(it); });
    } else {
        m_pages = new QStackedWidget;
        m_pages->addWidget(m_browsePage);
        m_pages->addWidget(m_itemPage);
        m_pages->addWidget(m_infoPage);
        m_pages->addWidget(m_usedPage);
        setCentralWidget(m_pages);
    }
    qApp->installEventFilter(this);

    new QShortcut(QKeySequence(QStringLiteral("Ctrl+D")), this, [this] {
        if (m_current && (m_wide || m_pages->currentWidget() != m_browsePage))
            toggleBookmark(*m_current);
    });
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+B")), this, [this] { showBookmarks(); });
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+F")), this, [this] { showMode(SEARCH_CAT); });
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+G")), this, [this] { showMode(GLOSSARY_CAT); });

    startLoad(false);
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    m_loc->shutdown();
    m_details->shutdown();
    m_sprites->shutdown();
    if (m_loader)
        m_loader->wait(5000);
}

// --------------------------------------------------------------------------- pages
QWidget *MainWindow::header(QWidget *left, QLabel *title, QList<QWidget *> right)
{
    auto *bar = new QWidget;
    bar->setObjectName(QStringLiteral("pageHeader"));
    bar->setAttribute(Qt::WA_StyledBackground);
    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(6, 4, 6, 4);
    if (left)
        lay->addWidget(left);
    title->setObjectName(QStringLiteral("pageTitle"));
    title->setMinimumWidth(40);
    lay->addWidget(title, 1);
    for (auto *w : right)
        lay->addWidget(w);
    return bar;
}

QWidget *MainWindow::buildBrowsePage()
{
    auto *page = new QWidget;
    auto *menuBtn = toolButton(QStringLiteral("⋮"), tr("Menu"));
    auto about = [this] {
        QMessageBox::about(this, tr("About"),
                           tr("<h3>DQ9 Alchemy Helper %1</h3>"
                              "<p>How to get any item in Dragon Quest IX: craft it, farm it or buy it.</p>"
                              "<p>Data: Dragon Quest Wiki (dragon-quest.org), dqnine.wikidot.com and a community recipe "
                              "spreadsheet. Fetched politely (robots.txt respected) and cached on this device.</p>"
                              "<p>GPL-3.0. Dragon Quest is a trademark of Square Enix; this is an unofficial fan tool.</p>")
                               .arg(QStringLiteral(APP_VERSION)));
    };
    connect(menuBtn, &QToolButton::clicked, this, [this, menuBtn, about] {
        showMenu(tr("Menu"),
                 {{QStringLiteral("\u2605 ") + tr("Bookmarks"), [this] { showBookmarks(); }},
                  {QStringLiteral("\U0001F50D ") + tr("Search all items"), [this] { showMode(SEARCH_CAT); }},
                  {QStringLiteral("\U0001F4D6 ") + tr("Glossary: what it's used in"), [this] { showMode(GLOSSARY_CAT); }},
                  {QString(), nullptr},
                  {tr("Refresh data"), [this] { startLoad(true); }},
                  {tr("Import spreadsheet..."), [this] { importSheet(); }},
                  {tr("Import woodus page..."), [this] { importRecipes(); }},
                  {QString(), nullptr},
                  {tr("About"), about}},
                 menuBtn->mapToGlobal(QPoint(0, menuBtn->height())));
    });
    auto *title = new QLabel(tr("DQ9 Alchemy Helper"));

    auto *category = new PageCombo;
    if (!m_wide)
        category->onPopup = [this] { chooseFromCombo(m_category, tr("Equipment type")); };
    m_category = category;
    connect(m_category, &QComboBox::currentIndexChanged, this, &MainWindow::fillItems);
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(tr("Filter items..."));
    m_filter->setClearButtonEnabled(true);
    connect(m_filter, &QLineEdit::textChanged, this, &MainWindow::fillItems);
    m_items = new QListWidget;
    m_items->setWordWrap(true);
    m_items->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_items->setContextMenuPolicy(Qt::CustomContextMenu);
    enableKineticScroll(m_items);
    connect(m_items, &QListWidget::itemClicked, this, &MainWindow::itemTapped);
    connect(m_items, &QListWidget::itemActivated, this, &MainWindow::itemTapped);
    connect(m_items, &QListWidget::customContextMenuRequested, this, &MainWindow::itemsMenu);
    m_status = new QLabel(tr("Starting..."));
    m_status->setObjectName(QStringLiteral("muted"));
    m_status->setWordWrap(true);

    auto *body = new QVBoxLayout;
    body->setContentsMargins(8, 8, 8, 4);
    body->addWidget(m_category);
    body->addWidget(m_filter);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(header(nullptr, title, {menuBtn}));
    lay->addLayout(body);
    lay->addWidget(m_items, 1);
    auto *statusRow = new QHBoxLayout;
    statusRow->setContentsMargins(10, 4, 10, 6);
    statusRow->addWidget(m_status);
    lay->addLayout(statusRow);
    return page;
}

QWidget *MainWindow::buildItemPage()
{
    auto *page = new QWidget;
    auto *back = toolButton(QStringLiteral("‹"), tr("Back"));
    connect(back, &QToolButton::clicked, this, &MainWindow::goBack);
    m_itemBack = back;
    m_itemTitle = new QLabel;
    m_bookmarkBtn = toolButton(QStringLiteral("☆"), tr("Bookmark this item (Ctrl+D)"));
    connect(m_bookmarkBtn, &QToolButton::clicked, this, [this] {
        if (m_current)
            toggleBookmark(*m_current);
    });
    auto *more = toolButton(QStringLiteral("⋮"), tr("More"));
    connect(more, &QToolButton::clicked, this, &MainWindow::itemPageMenu);

    // View: tree or list
    auto *treeBtn = new QToolButton;
    treeBtn->setText(tr("Tree"));
    treeBtn->setCheckable(true);
    treeBtn->setChecked(true);
    auto *listBtn = new QToolButton;
    listBtn->setText(tr("Material list"));
    listBtn->setCheckable(true);
    listBtn->setToolTip(tr("The total of everything you need to gather or buy"));
    m_modeGroup = new QButtonGroup(this);
    m_modeGroup->setExclusive(true);
    m_modeGroup->addButton(treeBtn, 0);
    m_modeGroup->addButton(listBtn, 1);
    connect(m_modeGroup, &QButtonGroup::idClicked, this, &MainWindow::modeChanged);

    auto *prefer = new PageCombo;
    if (!m_wide)
        prefer->onPopup = [this] { chooseFromCombo(m_prefer, tr("Show which route")); };
    m_prefer = prefer;
    m_prefer->addItem(tr("Prefer alchemy"), QStringLiteral("alchemy"));
    m_prefer->addItem(tr("Prefer drops"), QStringLiteral("drops"));
    m_prefer->setToolTip(tr("When an item can be crafted AND dropped by monsters, show only this one"));
    connect(m_prefer, &QComboBox::currentIndexChanged, this, &MainWindow::draw);

    m_perN = new QSpinBox;
    m_perN->setRange(1, 30);
    m_perN->setValue(1);
    m_perN->setPrefix(tr("Monsters: "));
    m_perN->setToolTip(tr("How many dropping monsters to show per item (highest drop rate first)"));
    connect(m_perN, &QSpinBox::valueChanged, this, &MainWindow::draw);

    auto *zoomOut = new QToolButton;
    zoomOut->setText(QStringLiteral("−"));
    auto *zoomIn = new QToolButton;
    zoomIn->setText(QStringLiteral("+"));
    auto *fit = new QToolButton;
    fit->setText(tr("Fit"));
    fit->setToolTip(tr("Rescale the tree to fit the screen"));

    auto *row1 = new QHBoxLayout;
    row1->addWidget(treeBtn);
    row1->addWidget(listBtn);
    row1->addWidget(m_prefer, 1);
    m_treeControls = new QWidget;
    auto *row2 = new QHBoxLayout(m_treeControls);
    row2->setContentsMargins(0, 0, 0, 0);
    row2->addWidget(m_perN, 1);
    row2->addWidget(zoomOut);
    row2->addWidget(zoomIn);
    row2->addWidget(fit);

    m_view = new TreeView([this](const QString &n) { return getSprite(n); });
    connect(m_view, &TreeView::nodeTapped, this, &MainWindow::openNode);
    connect(m_view, &TreeView::moreTapped, this, [this](const QString &key) { openItemInfo(key, QString()); });
    connect(fit, &QToolButton::clicked, m_view, &TreeView::fit);
    connect(zoomIn, &QToolButton::clicked, m_view, &TreeView::zoomIn);
    connect(zoomOut, &QToolButton::clicked, m_view, &TreeView::zoomOut);
    m_matlist = new MaterialListPanel(m_store, [this](const QString &n) { return getSprite(n); });
    connect(m_matlist, &MaterialListPanel::changed, this, &MainWindow::draw);
    connect(m_matlist, &MaterialListPanel::openItem, this, &MainWindow::openItemInfo);
    connect(m_matlist, &MaterialListPanel::copied, this, [this] { setStatus(tr("Material list copied"), 4000); });
    m_itemStack = new QStackedWidget;
    m_itemStack->addWidget(m_view);
    m_itemStack->addWidget(m_matlist);

    auto *controls = new QVBoxLayout;
    controls->setContentsMargins(8, 6, 8, 6);
    controls->addLayout(row1);
    controls->addWidget(m_treeControls);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(header(back, m_itemTitle, {m_bookmarkBtn, more}));
    lay->addLayout(controls);
    lay->addWidget(m_itemStack, 1);
    return page;
}

QWidget *MainWindow::buildInfoPage()
{
    auto *page = new QWidget;
    auto *back = toolButton(QStringLiteral("‹"), tr("Back"));
    connect(back, &QToolButton::clicked, this, &MainWindow::goBack);
    m_infoTitle = new QLabel;
    m_infoView = new QTextBrowser;
    m_infoView->setOpenLinks(false);
    m_infoView->setOpenExternalLinks(false);
    m_infoView->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    enableKineticScroll(m_infoView);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(header(back, m_infoTitle, {}));
    auto *body = new QVBoxLayout;
    body->setContentsMargins(8, 4, 8, 4);
    body->addWidget(m_infoView);
    lay->addLayout(body, 1);
    return page;
}

QWidget *MainWindow::buildUsedInPage()
{
    auto *page = new QWidget;
    m_usedBack = toolButton(QStringLiteral("\u2039"), tr("Back"));
    connect(m_usedBack, &QToolButton::clicked, this, &MainWindow::usedInBack);
    m_usedTitle = new QLabel;
    m_usedTrailLabel = new QLabel;
    m_usedTrailLabel->setObjectName(QStringLiteral("muted"));
    m_usedTrailLabel->setWordWrap(true);
    m_usedSummary = new QLabel;
    m_usedSummary->setWordWrap(true);
    m_usedHow = new QPushButton;
    m_usedHow->setMinimumHeight(m_wide ? 0 : 44);
    m_usedHow->setToolTip(tr("Open its recipe tree: how to craft, farm or buy it"));
    connect(m_usedHow, &QPushButton::clicked, this, [this] {
        if (!m_registry || m_usedTrail.isEmpty())
            return;
        if (const ItemPtr info = m_registry->glossaryEntry(m_usedTrail.last()).item)
            openItem(info);
    });
    m_usedList = new QListWidget;
    m_usedList->setWordWrap(true);
    m_usedList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_usedList->setContextMenuPolicy(Qt::CustomContextMenu);
    m_usedList->setToolTip(tr("Tap an item to see what IT is used in"));
    enableKineticScroll(m_usedList);
    auto tapped = [this](QListWidgetItem *it) {
        if (it && !it->data(ROLE_GLOSS).isNull())
            openUsedIn(it->data(ROLE_GLOSS).toString(), false);
    };
    connect(m_usedList, &QListWidget::itemClicked, this, tapped);
    connect(m_usedList, &QListWidget::itemActivated, this, tapped);
    connect(m_usedList, &QListWidget::customContextMenuRequested, this, &MainWindow::usedInMenu);

    auto *body = new QVBoxLayout;
    body->setContentsMargins(8, 6, 8, 6);
    body->addWidget(m_usedTrailLabel);
    body->addWidget(m_usedSummary);
    body->addWidget(m_usedHow);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(header(m_usedBack, m_usedTitle, {}));
    lay->addLayout(body);
    lay->addWidget(m_usedList, 1);
    return page;
}

QWidget *MainWindow::buildMenuPage()
{
    auto *page = new QWidget;
    auto *back = toolButton(QStringLiteral("\u2039"), tr("Back"));
    connect(back, &QToolButton::clicked, this, &MainWindow::goBack);
    m_menuTitle = new QLabel;
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_menuButtons = new QWidget;
    new QVBoxLayout(m_menuButtons);
    scroll->setWidget(m_menuButtons);
    enableKineticScroll(scroll);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(header(back, m_menuTitle, {}));
    lay->addWidget(scroll, 1);
    return page;
}

void MainWindow::showMenu(const QString &title, const MenuEntries &entries, const QPoint &globalPos)
{
    if (m_wide) {
        QMenu menu(this);
        for (const auto &[label, action] : entries) {
            if (label.isEmpty())
                menu.addSeparator();
            else
                menu.addAction(label, this, action);
        }
        menu.exec(globalPos);
        return;
    }
    if (!m_menuPage) {
        m_menuPage = buildMenuPage();
        m_pages->addWidget(m_menuPage);
    }
    m_menuTitle->setText(title);
    auto *lay = static_cast<QVBoxLayout *>(m_menuButtons->layout());
    while (QLayoutItem *old = lay->takeAt(0)) {
        delete old->widget();
        delete old;
    }
    lay->setContentsMargins(12, 12, 12, 12);
    lay->setSpacing(8);
    for (const auto &[label, action] : entries) {
        if (label.isEmpty()) {
            lay->addSpacing(12);
            continue;
        }
        auto *b = new QPushButton(label);
        b->setMinimumHeight(52);
        b->setStyleSheet(QStringLiteral("text-align:left; padding-left:16px; font-size:16px;"));
        connect(b, &QPushButton::clicked, this, [this, action] {
            goBack();   // leave the menu first, so the action's own page (if any) goes on top
            QTimer::singleShot(0, this, action);
        });
        lay->addWidget(b);
    }
    lay->addStretch(1);
    showPage(m_menuPage);
}

void MainWindow::chooseFromCombo(QComboBox *combo, const QString &title)
{
    MenuEntries entries;
    for (int i = 0; i < combo->count(); ++i) {
        const QString text = combo->itemText(i);
        entries.append({(i == combo->currentIndex() ? QStringLiteral("\u2713  ") : QStringLiteral("     ")) + text,
                        [combo, i] { combo->setCurrentIndex(i); }});
    }
    showMenu(title, entries, combo->mapToGlobal(QPoint(0, combo->height())));
}

void MainWindow::showPage(QWidget *page)
{
    if (m_wide || m_pages->currentWidget() == page)
        return;
    m_history << m_pages->currentWidget();
    m_pages->setCurrentWidget(page);
}

bool MainWindow::goBack()
{
    if (m_wide || m_history.isEmpty())
        return false;
    if (m_pages->currentWidget() == m_usedPage && m_usedTrail.size() > 1) {
        m_usedTrail.removeLast();   // back down one level of "used in"
        renderUsedIn();
        return true;
    }
    QWidget *prev = m_history.takeLast();
    if (m_pages->currentWidget() == m_infoPage)
        m_infoRender = nullptr;
    m_pages->setCurrentWidget(prev);
    if (prev == m_browsePage)
        fillItems();   // bookmarks may have changed
    return true;
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if ((ke->key() == Qt::Key_Back || ke->key() == Qt::Key_Escape) && !m_history.isEmpty()
            && QApplication::activeModalWidget() == nullptr && QApplication::activePopupWidget() == nullptr) {
            if (event->type() == QEvent::KeyRelease)
                goBack();
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::setStatus(const QString &text, int ms)
{
    m_status->setText(text);
    if (ms > 0) {
        QTimer::singleShot(ms, m_status, [this, text] {
            if (m_status->text() == text)
                m_status->setText(m_registry ? tr("%1 monsters, %2 recipes loaded")
                                                   .arg(m_registry->enemies().size())
                                                   .arg(m_registry->recipeCount())
                                             : QString());
        });
    }
}

// --------------------------------------------------------------------------- loading
void MainWindow::startLoad(bool force)
{
    if (m_loader && m_loader->isRunning())
        return;
    if (force) {
        m_loc->resetFailures();
        m_details->resetFailures();
    }
    if (m_loader)
        m_loader->deleteLater();
    m_loader = new LoadWorker(m_store, force, this);
    connect(m_loader, &LoadWorker::status, this, [this](const QString &t) { setStatus(t); });
    connect(m_loader, &LoadWorker::ready, this, &MainWindow::onReady);
    connect(m_loader, &LoadWorker::failed, this, &MainWindow::onFailed);
    m_loader->start();
}

void MainWindow::onFailed(const QString &msg)
{
    setStatus(tr("Load failed. Check your connection, then choose Refresh data from the menu."));
    m_items->clear();
    auto *it = new QListWidgetItem(tr("Couldn't load data:\n%1").arg(msg));
    it->setFlags(Qt::NoItemFlags);
    m_items->addItem(it);
}

void MainWindow::onReady(RegistryPtr registry, const QString &note)
{
    setRegistry(std::move(registry));
    if (note == QStringLiteral("robots")) {
        setStatus(tr("Recipes not loaded: import a saved copy of the recipes page"));
        if (!m_robotsNoticeShown) {
            m_robotsNoticeShown = true;
            QMessageBox::information(this, tr("Recipes need a saved copy"), tr(ROBOTS_MSG));
        }
    } else if (!note.isEmpty()) {
        setStatus(note);
    } else {
        setStatus(tr("%1 monsters, %2 recipes loaded").arg(m_registry->enemies().size()).arg(m_registry->recipeCount()));
    }
}

void MainWindow::setRegistry(RegistryPtr registry)
{
    m_registry = std::move(registry);
    const QString keep = m_category->currentText();
    m_category->blockSignals(true);
    m_category->clear();
    m_category->addItem(BOOKMARKS_CAT);
    m_category->addItem(SEARCH_CAT);
    m_category->addItem(GLOSSARY_CAT);
    m_category->addItems(m_registry->categories());
    if (refreshBookmarkKeys(*m_registry, m_bookmarks))
        m_store.saveBookmarks(m_bookmarks);
    int idx = m_category->findText(keep);
    if (idx < 0)   // first load: open on the bookmarks if there are any
        idx = m_bookmarks.isEmpty() ? SPECIAL_CATS : 0;
    m_category->setCurrentIndex(std::min(idx, m_category->count() - 1));
    m_category->blockSignals(false);
    m_category->update();   // Android sometimes keeps showing the empty box from before the load
    fillItems();
    if (m_current) {   // the item page follows its item into the new data, or closes
        m_current = m_registry->item(m_current->key);
        if (!m_current) {
            m_history.clear();
            if (m_wide)
                m_view->message(tr("Select an item on the left."));
            else
                m_pages->setCurrentWidget(m_browsePage);
        } else {
            m_matlist->setItem(m_current.get());
            draw();
            refreshInfo();
        }
    }
    if (!m_usedTrail.isEmpty()) {   // the used-in page too: drop items the new data no longer has
        m_usedTrail.removeIf([this](const QString &id) { return m_registry->glossaryEntry(id).name.isEmpty(); });
        if (!m_usedTrail.isEmpty()) {
            renderUsedIn();
        } else if (m_wide) {
            m_rightStack->setCurrentWidget(m_itemPage);
        } else if (m_pages->currentWidget() == m_usedPage) {
            m_history.clear();
            m_pages->setCurrentWidget(m_browsePage);
        }
    }
}

void MainWindow::importRecipes()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open the saved recipes page"), QString(),
                                                      tr("HTML files (*.html *.htm);;All files (*)"));
    if (path.isEmpty())
        return;
    QList<Recipe> recipes;
    try {
        recipes = m_store.importRecipesFile(path);
    } catch (const std::exception &e) {
        QMessageBox::warning(this, tr("Couldn't read that file"), errorText(e));
        return;
    }
    rebuildRegistry();
    setStatus(tr("Imported %1 recipes from the woodus page").arg(recipes.size()), 6000);
}

void MainWindow::importSheet()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open the recipes spreadsheet"), QString(),
                                                      tr("Excel files (*.xlsx *.xlsm);;All files (*)"));
    if (path.isEmpty())
        return;
    QList<Recipe> recipes;
    try {
        recipes = m_store.importSheetFile(path);
    } catch (const std::exception &e) {
        QMessageBox::warning(this, tr("Couldn't read that file"), errorText(e));
        return;
    }
    rebuildRegistry();
    setStatus(tr("Imported %1 recipes from the spreadsheet").arg(recipes.size()), 6000);
}

void MainWindow::rebuildRegistry()
{
    // Re-merge every recipe source we have (no network) after an import.
    if (m_registry)
        setRegistry(std::make_shared<const Registry>(m_registry->enemies(), m_store.cachedRecipes(),
                                                     m_store.shopItems(), m_store.weapons));
}

// --------------------------------------------------------------------------- browse
void MainWindow::fillItems()
{
    const QSignalBlocker block(m_items);
    m_items->clear();
    if (!m_registry)
        return;
    const QString mode = m_category->currentText();
    const bool marked = mode == BOOKMARKS_CAT;
    const bool searching = mode == SEARCH_CAT;
    const QString text = m_filter->text().trimmed().toLower();
    m_filter->setPlaceholderText(searching ? tr("Type an item's name...")
                                 : mode == GLOSSARY_CAT ? tr("Filter the glossary...")
                                                        : tr("Filter items..."));
    auto hint = [this](const QString &msg) {
        auto *h = new QListWidgetItem(msg);
        h->setFlags(Qt::NoItemFlags);
        m_items->addItem(h);
    };
    if (mode == GLOSSARY_CAT) {
        const QList<GlossaryEntry> entries = m_registry->glossary(m_filter->text());
        for (const GlossaryEntry &e : entries) {
            const QString uses = e.uses ? tr("used in %n", nullptr, e.uses) : tr("not used in recipes");
            const QString star = isBookmarked(m_bookmarks, e.item.get()) ? QStringLiteral("\u2605 ") : QString();
            auto *it = new QListWidgetItem(star + e.name + QStringLiteral("   (%1 \u00b7 %2)").arg(e.category, uses));
            it->setData(ROLE_GLOSS, e.id);
            if (e.item)
                it->setData(ROLE_KEY, e.item->key);
            it->setToolTip(tr("Tap to see what %1 is used to make").arg(e.name));
            m_items->addItem(it);
        }
        if (entries.isEmpty())
            hint(tr("Nothing in the glossary matches \u201c%1\u201d.").arg(m_filter->text().trimmed()));
        return;
    }
    if (searching && text.isEmpty()) {
        hint(tr("Type part of a name to search every weapon, armour piece, accessory and item.\n\n"
                "Tip: the \U0001F4D6 Glossary lists everything and shows what each one is used to make."));
        return;
    }
    QList<QPair<std::optional<Bookmark>, ItemPtr>> rows;
    if (searching) {
        for (const auto &info : m_registry->search(m_filter->text()))
            rows.append({std::nullopt, info});
    } else if (marked) {
        for (const auto &[m, info] : bookmarkItems(*m_registry, m_bookmarks))
            if ((info ? info->name : m.name).toLower().contains(text))
                rows.append({m, info});
    } else {
        for (const auto &info : m_registry->itemsIn(m_category->currentText(), text))
            rows.append({std::nullopt, info});
    }
    for (const auto &[m, info] : rows) {
        if (!info) {   // bookmarked, but no data source has it any more
            auto *it = new QListWidgetItem(QStringLiteral("★ %1   ").arg(m->name) + tr("(not found in the current data)"));
            it->setFlags(Qt::ItemIsEnabled);
            it->setData(ROLE_MARK_KEY, m->key);
            m_items->addItem(it);
            continue;
        }
        QStringList tags;
        if (marked || searching)
            tags << info->category;
        if (!info->recipes.isEmpty())
            tags << tr("alchemy");
        if (!info->droppers.isEmpty())
            tags << tr("drop");
        if (!info->found.isEmpty())
            tags << tr("found");
        const QString star = isBookmarked(m_bookmarks, info.get()) ? QStringLiteral("★ ") : QString();
        auto *it = new QListWidgetItem(star + info->name + (tags.isEmpty() ? QString() : QStringLiteral("   (%1)").arg(tags.join(QStringLiteral(", ")))));
        it->setData(ROLE_KEY, info->key);
        m_items->addItem(it);
        if (m_wide && m_current && info->key == m_current->key)
            m_items->setCurrentItem(it);
    }
    if (marked && rows.isEmpty())
        hint(m_bookmarks.isEmpty() ? tr("No bookmarks yet. Open an item and tap ☆ to bookmark it.")
                                   : tr("No bookmarks match the filter."));
    if (searching && rows.isEmpty())
        hint(tr("No item matches \u201c%1\u201d.").arg(m_filter->text().trimmed()));
}

void MainWindow::itemTapped(QListWidgetItem *it)
{
    if (!it || !m_registry)
        return;
    if (!it->data(ROLE_GLOSS).isNull()) {
        openUsedIn(it->data(ROLE_GLOSS).toString(), true);
        return;
    }
    const QString key = it->data(ROLE_KEY).toString();
    if (key.isEmpty()) {
        if (!it->data(ROLE_MARK_KEY).isNull())
            itemsMenu(m_items->visualItemRect(it).center());   // a bookmark whose item is gone: offer to remove it
        return;
    }
    if (const ItemPtr info = m_registry->item(key)) {
        if (m_wide && m_current == info && m_rightStack->currentWidget() == m_itemPage)
            return;
        openItem(info);
    }
}

void MainWindow::itemsMenu(const QPoint &pos)
{
    QListWidgetItem *it = m_items->itemAt(pos);
    if (!it || !m_registry)
        return;
    MenuEntries entries;
    const ItemPtr info = m_registry->item(it->data(ROLE_KEY).toString());
    if (!info) {
        const QString key = it->data(ROLE_MARK_KEY).toString();
        if (key.isEmpty())
            return;
        entries.append({tr("Remove bookmark"), [this, key] {
            m_bookmarks.removeIf([&](const Bookmark &m) { return m.key == key; });
            m_store.saveBookmarks(m_bookmarks);
            fillItems();
        }});
    } else {
        const bool on = isBookmarked(m_bookmarks, info.get());
        entries.append({tr("How to get it"), [this, info] { openItem(info); }});
        entries.append({tr("What it's used in"), [this, info] { openUsedIn(info->key, true); }});
        entries.append({on ? tr("Remove bookmark") : tr("Add bookmark"), [this, info] { toggleBookmark(*info); }});
        if (on && m_category->currentText() == BOOKMARKS_CAT && m_filter->text().trimmed().isEmpty()) {
            entries.append({QString(), nullptr});
            entries.append({tr("Move up"), [this, info] {
                moveBookmark(m_bookmarks, *info, -1);
                m_store.saveBookmarks(m_bookmarks);
                fillItems();
            }});
            entries.append({tr("Move down"), [this, info] {
                moveBookmark(m_bookmarks, *info, 1);
                m_store.saveBookmarks(m_bookmarks);
                fillItems();
            }});
        }
    }
    showMenu(it->text().section(QStringLiteral("   ("), 0, 0), entries, m_items->viewport()->mapToGlobal(pos));
}

void MainWindow::showMode(const QString &category)
{
    m_history.clear();
    m_infoRender = nullptr;
    if (!m_wide)
        m_pages->setCurrentWidget(m_browsePage);
    const int idx = m_category->findText(category);
    if (idx >= 0)
        m_category->setCurrentIndex(idx);
    fillItems();
    m_filter->setFocus();
    m_filter->selectAll();
}

void MainWindow::showBookmarks()
{
    m_history.clear();
    m_infoRender = nullptr;
    if (!m_wide)
        m_pages->setCurrentWidget(m_browsePage);
    m_filter->clear();
    m_category->setCurrentIndex(std::max(m_category->findText(BOOKMARKS_CAT), 0));
    fillItems();
}

// --------------------------------------------------------------------------- item page
void MainWindow::openItem(const ItemPtr &info)
{
    m_current = info;
    m_itemTitle->setText(info->name);
    m_itemTitle->setToolTip(info->name);
    updateBookmarkButton();
    m_matlist->setItem(info.get());
    if (m_wide)
        m_rightStack->setCurrentWidget(m_itemPage);
    showPage(m_itemPage);
    draw();
}

void MainWindow::draw()
{
    if (!m_registry || !m_current)
        return;
    m_foundWait.clear();
    const QString prefer = m_prefer->currentData().toString();
    const FoundFn found = [this](const QString &n) { return foundFor(n); };
    if (m_modeGroup->checkedId() == 1) {
        m_matlist->showList(materialList(*m_registry, *m_current, m_store.shops(), prefer, m_matlist->choices,
                                         m_matlist->count(), m_matlist->recipeIndex(), found));
        return;
    }
    m_view->showTree(buildTree(*m_registry, *m_current, m_perN->value(), m_store.shops(), prefer, found));
}

void MainWindow::modeChanged()
{
    const bool isList = m_modeGroup->checkedId() == 1;
    m_itemStack->setCurrentIndex(isList ? 1 : 0);
    m_treeControls->setVisible(!isList);
    draw();
}

void MainWindow::updateBookmarkButton()
{
    const bool on = isBookmarked(m_bookmarks, m_current.get());
    m_bookmarkBtn->setText(on ? QStringLiteral("★") : QStringLiteral("☆"));
    m_bookmarkBtn->setToolTip(on ? tr("Remove from bookmarks (Ctrl+D)") : tr("Bookmark this item (Ctrl+D)"));
}

void MainWindow::toggleBookmark(const ItemInfo &info)
{
    const bool on = dq9::toggleBookmark(m_bookmarks, info);
    m_store.saveBookmarks(m_bookmarks);
    setStatus((on ? tr("Bookmarked %1") : tr("Removed bookmark: %1")).arg(info.name), 4000);
    updateBookmarkButton();
    fillItems();   // updates the stars (and the list itself when showing bookmarks)
}

void MainWindow::itemPageMenu()
{
    MenuEntries entries;
    entries.append({tr("How to read the tree"), [this] {
        openInfo(tr("Legend"), [] { return Rendered{theme::legendHtml(), {}}; });
    }});
    if (m_current) {
        entries.append({tr("Item details"), [this] { openItemInfo(m_current->key, m_current->name); }});
        entries.append({tr("What it's used in"), [this] { openUsedIn(m_current->key, true); }});
        if (isBookmarked(m_bookmarks, m_current.get())) {
            entries.append({QString(), nullptr});
            entries.append({tr("Move bookmark up"), [this] {
                moveBookmark(m_bookmarks, *m_current, -1);
                m_store.saveBookmarks(m_bookmarks);
            }});
            entries.append({tr("Move bookmark down"), [this] {
                moveBookmark(m_bookmarks, *m_current, 1);
                m_store.saveBookmarks(m_bookmarks);
            }});
        }
    }
    showMenu(m_current ? m_current->name : tr("Menu"), entries, mapToGlobal(QPoint(std::max(0, width() - 240), 48)));
}

// --------------------------------------------------------------------------- used-in page
void MainWindow::openUsedIn(const QString &id, bool fresh)
{
    if (!m_registry || id.isEmpty())
        return;
    if (fresh) {
        m_usedTrail = {id};
    } else {
        const int at = m_usedTrail.indexOf(id);
        if (at >= 0)
            m_usedTrail = m_usedTrail.mid(0, at + 1);   // already on the trail: step back to it
        else
            m_usedTrail << id;
    }
    renderUsedIn();
    if (m_wide)
        m_rightStack->setCurrentWidget(m_usedPage);
    else
        showPage(m_usedPage);
}

void MainWindow::renderUsedIn()
{
    m_usedList->clear();
    if (!m_registry || m_usedTrail.isEmpty())
        return;
    const GlossaryEntry entry = m_registry->glossaryEntry(m_usedTrail.last());
    m_usedTitle->setText(tr("%1: used in").arg(entry.name));
    m_usedTitle->setToolTip(entry.name);
    m_usedBack->setVisible(!m_wide || m_usedTrail.size() > 1);

    QStringList trail;
    for (const QString &id : m_usedTrail)
        trail << m_registry->glossaryEntry(id).name;
    m_usedTrailLabel->setText(trail.join(QStringLiteral("  \u203a  ")));
    m_usedTrailLabel->setVisible(m_usedTrail.size() > 1);

    const QList<Use> uses = m_registry->usedInId(entry.id);
    m_usedSummary->setText(uses.isEmpty()
                               ? tr("%1 isn't an ingredient in any known recipe.").arg(entry.name)
                               : tr("%1 goes straight into %n recipe(s). Tap one to see what that is used in.", nullptr,
                                    int(uses.size()))
                                     .arg(entry.name));
    m_usedHow->setText(tr("How to get %1  \u203a").arg(entry.name));
    m_usedHow->setVisible(bool(entry.item));

    for (const Use &u : uses) {
        const Recipe &r = u.product->recipes[u.recipe];
        QStringList parts;
        for (const Ing &ing : r.ingredients)
            parts << QStringLiteral("%1\u00d7 %2").arg(ing.qty).arg(ing.name);
        const int further = int(m_registry->usedInId(u.product->key).size());
        const QString next = further ? tr("\u2192 used in %n more", nullptr, further) : tr("not used in anything else");
        QString text = u.product->name + QStringLiteral("   (%1)").arg(u.product->category);
        text += QStringLiteral("\n    ") + parts.join(QStringLiteral(" + "));
        if (u.product->recipes.size() > 1)
            text += QStringLiteral("   ") + tr("[recipe %1 of %2]").arg(u.recipe + 1).arg(u.product->recipes.size());
        text += QStringLiteral("\n    ") + next;
        auto *it = new QListWidgetItem(text);
        it->setData(ROLE_GLOSS, u.product->key);
        it->setData(ROLE_KEY, u.product->key);
        m_usedList->addItem(it);
    }
    m_usedList->scrollToTop();
}

void MainWindow::usedInBack()
{
    if (m_usedTrail.size() > 1) {
        m_usedTrail.removeLast();
        renderUsedIn();
    } else if (!m_wide) {
        goBack();
    }
}

void MainWindow::usedInMenu(const QPoint &pos)
{
    QListWidgetItem *it = m_usedList->itemAt(pos);
    if (!it || !m_registry)
        return;
    const ItemPtr info = m_registry->item(it->data(ROLE_KEY).toString());
    if (!info)
        return;
    showMenu(info->name,
             {{tr("What it's used in"), [this, info] { openUsedIn(info->key, false); }},
              {tr("How to get it"), [this, info] { openItem(info); }}},
             m_usedList->viewport()->mapToGlobal(pos));
}

// --------------------------------------------------------------------------- info page
void MainWindow::openNode(const NodePtr &node)
{
    if (!m_registry)
        return;
    if (node->enemy) {
        const EnemyPtr enemy = node->enemy;
        openInfo(enemy->name, [this, enemy] {
            return renderEnemyHtml(enemy, [this](const EnemyPtr &e) { return m_loc->state(e); });
        });
        return;
    }
    const ItemPtr info = node->key.isEmpty() ? nullptr : m_registry->item(node->key);
    if (!info) {
        const QString label = node->label;
        const QString text = esc(node->tip.isEmpty() ? tr("No monster drops this and no recipe makes it.") : node->tip)
                                 .replace(QLatin1Char('\n'), QStringLiteral("<br>"));
        openInfo(label, [label, text] { return Rendered{QStringLiteral("<h2>%1</h2><p>%2</p>").arg(esc(label), text), {}}; });
        return;
    }
    openItemInfo(info->key, info->name);
}

void MainWindow::openItemInfo(const QString &key, const QString &name)
{
    if (!m_registry)
        return;
    ItemPtr info = key.isEmpty() ? nullptr : m_registry->item(key);
    if (!info && !name.isEmpty())
        info = m_registry->resolve(name);
    if (!info) {
        openInfo(name, [name] {
            return Rendered{QStringLiteral("<h2>%1</h2><p>No monster drops this and no recipe makes it.</p>").arg(esc(name)), {}};
        });
        return;
    }
    const RegistryPtr reg = m_registry;
    openInfo(info->name, [this, reg, info] {
        return renderItemHtml(*reg, *info, std::max(m_perN->value(), 10),
                              [this](const EnemyPtr &e) { return m_loc->state(e); }, m_store.shops(),
                              [this](const QString &n) { return m_details->state(n); });
    });
}

void MainWindow::openInfo(const QString &title, std::function<Rendered()> render)
{
    if (m_wide) {
        auto *dlg = new QDialog(this);
        dlg->setWindowTitle(title);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        auto *view = new QTextBrowser;
        view->setOpenLinks(false);
        auto *lay = new QVBoxLayout(dlg);
        lay->addWidget(view);
        dlg->resize(560, 640);
        m_infoWindows.append({view, render});
        dlg->show();
        refreshInfoView(view, render);
        return;
    }
    m_infoTitle->setText(title);
    m_infoTitle->setToolTip(title);
    m_infoRender = std::move(render);
    m_infoView->clear();
    showPage(m_infoPage);
    refreshInfo();
    m_infoView->verticalScrollBar()->setValue(0);
}

void MainWindow::refreshInfo()
{
    if (m_wide) {
        m_infoWindows.removeIf([](const InfoWindow &w) { return w.view.isNull(); });
        for (const auto &w : m_infoWindows)
            refreshInfoView(w.view, w.render);
        return;
    }
    if (m_infoRender && m_pages->currentWidget() == m_infoPage)
        refreshInfoView(m_infoView, m_infoRender);
}

void MainWindow::refreshInfoView(QTextBrowser *view, const std::function<Rendered()> &render)
{
    QScrollBar *bar = view->verticalScrollBar();
    const int pos = bar->value();
    const Rendered r = render();
    view->setHtml(theme::css() + r.html);
    QTimer::singleShot(0, bar, [bar, pos] { bar->setValue(pos); });
    for (const auto &enemy : r.needed)
        m_loc->request(enemy);
}

// --------------------------------------------------------------------------- icons and look-ups
QPair<bool, QPixmap> MainWindow::getSprite(const QString &name)
{
    // (has an icon, pixmap or null). Starts the download if the picture isn't cached yet.
    if (!m_store.hasSprite(name))
        return {false, QPixmap()};
    const QString key = skey(name);
    auto it = m_pixmaps.find(key);
    if (it != m_pixmaps.end())
        return {true, it.value()};
    const QString path = m_store.spritePath(name);
    if (!path.isEmpty()) {
        const QPixmap px(path);
        m_pixmaps.insert(key, px);
        return {true, px};
    }
    m_sprites->request(name);
    return {true, QPixmap()};
}

void MainWindow::spriteReady(const QString &name)
{
    const QString path = m_store.spritePath(name);
    if (path.isEmpty())
        return;
    const QString key = skey(name);
    if (!m_pixmaps.contains(key))
        m_pixmaps.insert(key, QPixmap(path));
    const QPixmap px = m_pixmaps.value(key);
    m_view->setSprite(name, px);
    m_matlist->setSprite(name, px);
}

QStringList MainWindow::foundFor(const QString &name)
{
    // Where-to-find lines from an item's wiki page. Starts the look-up if it isn't cached; the tree
    // is redrawn when it arrives.
    const DetailState st = m_details->state(name);
    if (st.state == QStringLiteral("loading")) {
        m_foundWait.insert(dkey(name));
        return {};
    }
    return detailsFound(st.details);
}

void MainWindow::detailsReady(const QString &name)
{
    m_rerenderTimer->start();
    const QString key = dkey(name);
    if (m_foundWait.remove(key))
        m_redrawTimer->start();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_loc->shutdown();
    m_details->shutdown();
    m_sprites->shutdown();
    QMainWindow::closeEvent(event);
}
