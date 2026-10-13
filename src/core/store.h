// Downloads and caches everything (in ~/.dq9_helper/ on desktop, the app's private storage on
// Android) so the sites are hit as little as possible.
#pragma once

#include "core/http.h"
#include "core/models.h"

#include <QDir>
#include <QHash>
#include <QJsonDocument>
#include <QMutex>

#include <memory>
#include <optional>

namespace dq9 {

class Store {
public:
    explicit Store(const QString &dir = defaultDir(), std::shared_ptr<Http> http = nullptr);

    static QString defaultDir();
    QString dir() const { return m_dir.path(); }
    void setHttp(std::shared_ptr<Http> http) { m_http = std::move(http); }

    // enemies
    QList<EnemyPtr> loadEnemies(bool force = false);

    // bookmarks, in the order they were added
    QList<Bookmark> loadBookmarks() const;
    void saveBookmarks(const QList<Bookmark> &marks) const;

    // material-list choices: item key -> break it down (true) or list it whole (false)
    QHash<QString, bool> loadListChoices() const;
    void saveListChoices(const QHash<QString, bool> &choices) const;

    // weapon list (the page's wiki source is what gets cached, so parser fixes need no re-download)
    QList<WeaponPtr> loadWeapons(bool force = false);
    QList<WeaponPtr> weapons;

    // recipes
    QPair<QList<Recipe>, QString> loadRecipes(bool force = false);   // note "robots" when blocked
    QList<Recipe> importRecipesFile(const QString &path);
    QList<Recipe> loadSheetRecipes() const;
    QList<Recipe> cachedRecipes() const;

    // icons
    QHash<QString, QString> loadSpriteIndex(bool force = false);
    void registerSprites(const QHash<QString, QString> &extra);
    bool hasSprite(const QString &name) const;
    QString spritePath(const QString &name) const;   // cached image file, "" if not downloaded yet
    QString fetchSprite(const QString &name);          // downloads if needed; "" if no icon
    QHash<QString, QString> spriteIndex() const;
    void setSpriteIndex(const QHash<QString, QString> &index);

    // item details (dqnine.wikidot.com). nullopt = not looked up yet; an empty Details = no page.
    std::optional<Details> cachedDetails(const QString &name) const;
    std::optional<Details> itemDetails(const QString &name);
    void clearDetails();

    // monster locations
    std::optional<QStringList> cachedLocations(const Enemy &enemy) const;
    QStringList locations(const Enemy &enemy);

    // shops: item key -> ["Town (Shop)", ...]; the user's shops.json adds to the bundled list
    QHash<QString, QStringList> shops() const;
    // every item the bundled shop list sells -> a category hint ("" if none)
    QList<QPair<QString, QString>> shopItems() const;

private:
    std::optional<QJsonDocument> readJson(const QString &name) const;
    void writeJson(const QString &name, const QJsonDocument &doc) const;
    std::optional<QString> readText(const QString &name) const;
    QString spriteKey(const QString &name) const;
    QString spriteFile(const QString &url) const;
    void indexSpriteNames();
    QHash<QString, QString> wikiIndex();
    QJsonObject bundledShops() const;

    QDir m_dir;
    std::shared_ptr<Http> m_http;

    mutable QMutex m_cacheLock;      // locations + details
    QHash<QString, QStringList> m_locCache;
    QHash<QString, Details> m_detailCache;

    QMutex m_indexLock;
    std::optional<QHash<QString, QString>> m_index;

    mutable QMutex m_spriteLock;
    QHash<QString, QString> m_spriteIndex;
    QHash<QString, QString> m_spriteNorm;
};

} // namespace dq9
