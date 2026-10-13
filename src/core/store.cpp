#include "core/store.h"

#include "core/parsers.h"
#include "core/text.h"
#include "core/wikitext.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>

namespace dq9 {

static const QString BUNDLED_SHEET = QStringLiteral(":/data/sheet_recipes.json");
static const QString BUNDLED_SHOPS = QStringLiteral(":/data/shops_bundled.json");

QString Store::defaultDir()
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#else
    // Same folder as the Python version, so an existing cache carries over.
    return QDir::home().filePath(QStringLiteral(".dq9_helper"));
#endif
}

Store::Store(const QString &dir, std::shared_ptr<Http> http)
    : m_dir(dir), m_http(http ? std::move(http) : std::make_shared<Http>())
{
    QDir().mkpath(m_dir.path());
    if (const auto doc = readJson(QStringLiteral("locations.json")); doc && doc->isObject()) {
        const QJsonObject o = doc->object();
        for (auto it = o.begin(); it != o.end(); ++it) {
            QStringList locs;
            for (const auto &v : it.value().toArray())
                locs << v.toString();
            m_locCache.insert(it.key(), locs);
        }
    }
    if (const auto doc = readJson(QStringLiteral("details.json")); doc && doc->isObject()) {
        const QJsonObject o = doc->object();
        for (auto it = o.begin(); it != o.end(); ++it)
            m_detailCache.insert(it.key(), Details::fromJson(it.value().toObject()));
    }
}

std::optional<QJsonDocument> Store::readJson(const QString &name) const
{
    QFile f(m_dir.filePath(name));
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError)
        return std::nullopt;
    return doc;
}

void Store::writeJson(const QString &name, const QJsonDocument &doc) const
{
    QSaveFile f(m_dir.filePath(name));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(doc.toJson(QJsonDocument::Indented));
        f.commit();
    }
}

std::optional<QString> Store::readText(const QString &name) const
{
    QFile f(m_dir.filePath(name));
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    return QString::fromUtf8(f.readAll());
}

static QList<Recipe> recipesFromJson(const QJsonArray &arr)
{
    QList<Recipe> out;
    for (const auto &v : arr)
        out.append(Recipe::fromJson(v.toObject()));
    return out;
}

static QJsonArray recipesToJson(const QList<Recipe> &recipes)
{
    QJsonArray arr;
    for (const auto &r : recipes)
        arr.append(r.toJson());
    return arr;
}

// --------------------------------------------------------------------------- enemies
QList<EnemyPtr> Store::loadEnemies(bool force)
{
    QList<EnemyPtr> out;
    const auto cached = readJson(QStringLiteral("enemies.json"));
    if (cached && !cached->array().isEmpty() && !force) {
        for (const auto &v : cached->array())
            out.append(std::make_shared<Enemy>(Enemy::fromJson(v.toObject())));
        return out;
    }
    const QList<Enemy> enemies = parseEnemyList(m_http->get(ENEMY_URL));
    QJsonArray arr;
    for (const auto &e : enemies) {
        arr.append(e.toJson());
        out.append(std::make_shared<Enemy>(e));
    }
    writeJson(QStringLiteral("enemies.json"), QJsonDocument(arr));
    return out;
}

// --------------------------------------------------------------------------- bookmarks
QList<Bookmark> Store::loadBookmarks() const
{
    QList<Bookmark> out;
    const auto doc = readJson(QStringLiteral("bookmarks.json"));
    if (!doc || !doc->isArray())
        return out;
    for (const auto &v : doc->array()) {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        const QString key = o.value("key").toVariant().toString();
        const QString name = o.value("name").toVariant().toString();
        if (!key.isEmpty() || !name.isEmpty())
            out.append({key, name});
    }
    return out;
}

void Store::saveBookmarks(const QList<Bookmark> &marks) const
{
    QJsonArray arr;
    for (const auto &m : marks)
        arr.append(QJsonObject{{"key", m.key}, {"name", m.name}});
    writeJson(QStringLiteral("bookmarks.json"), QJsonDocument(arr));
}

// --------------------------------------------------------------------------- material-list choices
QHash<QString, bool> Store::loadListChoices() const
{
    QHash<QString, bool> out;
    const auto doc = readJson(QStringLiteral("list_choices.json"));
    if (!doc || !doc->isObject())
        return out;
    const QJsonObject o = doc->object();
    for (auto it = o.begin(); it != o.end(); ++it)
        out.insert(it.key(), it.value().toVariant().toBool());
    return out;
}

void Store::saveListChoices(const QHash<QString, bool> &choices) const
{
    QJsonObject o;
    for (auto it = choices.begin(); it != choices.end(); ++it)
        o.insert(it.key(), it.value());
    writeJson(QStringLiteral("list_choices.json"), QJsonDocument(o));
}

// --------------------------------------------------------------------------- weapon list
QList<WeaponPtr> Store::loadWeapons(bool force)
{
    std::optional<QString> text;
    if (!force)
        text = readText(QStringLiteral("weapons_wikitext.txt"));
    if (!text || text->isEmpty()) {
        try {
            const QJsonDocument doc = QJsonDocument::fromJson(m_http->get(WEAPON_API).toUtf8());
            const QJsonValue wt = doc.object().value("parse").toObject().value("wikitext");
            text = wt.isObject() ? wt.toObject().value("*").toString() : wt.toString();   // older API shape
            if (text->isEmpty())
                throw ParseError(QStringLiteral("empty API answer"));
        } catch (const std::exception &) {
            text = m_http->get(WEAPON_RAW);
        }
        QSaveFile f(m_dir.filePath(QStringLiteral("weapons_wikitext.txt")));
        if (f.open(QIODevice::WriteOnly)) {
            f.write(text->toUtf8());
            f.commit();
        }
    }
    weapons.clear();
    for (const auto &w : parseWeaponWikitext(*text))
        weapons.append(std::make_shared<Weapon>(w));
    return weapons;
}

// --------------------------------------------------------------------------- recipes
QPair<QList<Recipe>, QString> Store::loadRecipes(bool force)
{
    const auto cached = readJson(QStringLiteral("recipes.json"));
    const bool haveCache = cached && !cached->array().isEmpty();
    if (haveCache && !force)
        return {recipesFromJson(cached->array()), QString()};
    QList<Recipe> recipes;
    try {
        recipes = parseRecipes(m_http->get(RECIPE_URL));
    } catch (const RobotsDisallowed &) {
        if (haveCache)
            return {recipesFromJson(cached->array()), QStringLiteral("robots")};
        throw;
    }
    writeJson(QStringLiteral("recipes.json"), QJsonDocument(recipesToJson(recipes)));
    return {recipes, QString()};
}

QList<Recipe> Store::importRecipesFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw ParseError(QStringLiteral("Couldn't open %1: %2").arg(path, f.errorString()));
    const QList<Recipe> recipes = parseRecipes(QString::fromUtf8(f.readAll()));
    writeJson(QStringLiteral("recipes.json"), QJsonDocument(recipesToJson(recipes)));
    return recipes;
}

QList<Recipe> Store::loadSheetRecipes() const
{
    // Your imported copy if there is one, else the parsed copy shipped with the app.
    for (const QString &path : {m_dir.filePath(QStringLiteral("sheet_recipes.json")), BUNDLED_SHEET}) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error != QJsonParseError::NoError)
            continue;
        return recipesFromJson(doc.array());
    }
    return {};
}

QList<Recipe> Store::cachedRecipes() const
{
    QList<Recipe> out;
    if (const auto cached = readJson(QStringLiteral("recipes.json")))
        out = recipesFromJson(cached->array());
    return out + loadSheetRecipes();
}

// --------------------------------------------------------------------------- sprites
QHash<QString, QString> Store::loadSpriteIndex(bool force)
{
    QHash<QString, QString> index;
    const auto cached = readJson(QStringLiteral("sprites.json"));
    if (cached && !cached->object().isEmpty() && !force) {
        const QJsonObject o = cached->object();
        for (auto it = o.begin(); it != o.end(); ++it)
            index.insert(it.key(), it.value().toString());
    } else {
        index = parseSprites(m_http->get(SPRITE_URL));
        QJsonObject o;
        for (auto it = index.begin(); it != index.end(); ++it)
            o.insert(it.key(), it.value());
        writeJson(QStringLiteral("sprites.json"), QJsonDocument(o));
    }
    setSpriteIndex(index);
    return index;
}

void Store::setSpriteIndex(const QHash<QString, QString> &index)
{
    QMutexLocker lock(&m_spriteLock);
    m_spriteIndex = index;
    indexSpriteNames();
}

QHash<QString, QString> Store::spriteIndex() const
{
    QMutexLocker lock(&m_spriteLock);
    return m_spriteIndex;
}

void Store::registerSprites(const QHash<QString, QString> &extra)
{
    // Icons from another page (the weapon list). Icons already known are kept.
    QMutexLocker lock(&m_spriteLock);
    for (auto it = extra.begin(); it != extra.end(); ++it)
        if (!it.value().isEmpty() && !m_spriteIndex.contains(skey(it.key())))
            m_spriteIndex.insert(skey(it.key()), it.value());
    indexSpriteNames();
}

void Store::indexSpriteNames()
{
    m_spriteNorm.clear();
    QSet<QString> clash;
    for (auto it = m_spriteIndex.begin(); it != m_spriteIndex.end(); ++it) {
        const QString n = norm(it.key());
        if (m_spriteNorm.contains(n))
            clash.insert(n);
        m_spriteNorm.insert(n, it.key());
    }
    for (const QString &n : clash)
        m_spriteNorm.remove(n);   // ambiguous (e.g. Tortoiseshell / Tortoise shell): exact name only
}

QString Store::spriteKey(const QString &name) const
{
    const QString k = skey(name);
    if (m_spriteIndex.contains(k))
        return k;
    return m_spriteNorm.value(norm(name));
}

bool Store::hasSprite(const QString &name) const
{
    QMutexLocker lock(&m_spriteLock);
    return !spriteKey(name).isEmpty();
}

QString Store::spriteFile(const QString &url) const
{
    QString ext = QFileInfo(QUrl(url).path()).suffix().toLower();
    ext = ext.isEmpty() ? QStringLiteral(".png") : QLatin1Char('.') + ext;
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex()).left(16);
    return m_dir.filePath(QStringLiteral("sprites/") + hash + ext);
}

QString Store::spritePath(const QString &name) const
{
    QMutexLocker lock(&m_spriteLock);
    const QString k = spriteKey(name);
    if (k.isEmpty())
        return QString();
    const QString f = spriteFile(m_spriteIndex.value(k));
    return QFile::exists(f) ? f : QString();
}

QString Store::fetchSprite(const QString &name)
{
    QString url, file;
    {
        QMutexLocker lock(&m_spriteLock);
        const QString k = spriteKey(name);
        if (k.isEmpty())
            return QString();
        url = m_spriteIndex.value(k);
        file = spriteFile(url);
    }
    if (!QFile::exists(file)) {
        const QByteArray data = m_http->getBytes(url);
        QDir().mkpath(QFileInfo(file).path());
        QSaveFile f(file);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(data);
            f.commit();
        }
    }
    return file;
}

// --------------------------------------------------------------------------- item details
std::optional<Details> Store::cachedDetails(const QString &name) const
{
    QMutexLocker lock(&m_cacheLock);
    const auto it = m_detailCache.constFind(dkey(name));
    if (it == m_detailCache.constEnd())
        return std::nullopt;
    return it.value();
}

std::optional<Details> Store::itemDetails(const QString &name)
{
    if (const auto hit = cachedDetails(name))
        return hit->isEmpty() ? std::nullopt : hit;
    const QHash<QString, QString> index = wikiIndex();
    const QString url = index.value(dkey(name), WIKIDOT + QLatin1Char('/') + wikidotSlug(name));
    std::optional<Details> result;
    try {
        result = parseItemPage(m_http->get(url), name);
    } catch (const HttpError &e) {
        if (e.status != 404)
            throw;   // no such page (404): remember that, don't ask again
    }
    QMutexLocker lock(&m_cacheLock);
    m_detailCache.insert(dkey(name), result.value_or(Details()));
    QJsonObject o;
    for (auto it = m_detailCache.begin(); it != m_detailCache.end(); ++it)
        o.insert(it.key(), it.value().toJson());
    writeJson(QStringLiteral("details.json"), QJsonDocument(o));
    return result;
}

QHash<QString, QString> Store::wikiIndex()
{
    QMutexLocker lock(&m_indexLock);
    if (!m_index) {
        const auto cached = readJson(QStringLiteral("wiki_index.json"));
        QHash<QString, QString> idx;
        if (cached && !cached->object().isEmpty()) {
            const QJsonObject o = cached->object();
            for (auto it = o.begin(); it != o.end(); ++it)
                idx.insert(it.key(), it.value().toString());
        } else {
            idx = parseWikiIndex(m_http->get(WIKIDOT_INDEX));
            QJsonObject o;
            for (auto it = idx.begin(); it != idx.end(); ++it)
                o.insert(it.key(), it.value());
            writeJson(QStringLiteral("wiki_index.json"), QJsonDocument(o));
        }
        m_index = idx;
    }
    return *m_index;
}

void Store::clearDetails()
{
    {
        QMutexLocker lock(&m_cacheLock);
        m_detailCache.clear();
        writeJson(QStringLiteral("details.json"), QJsonDocument(QJsonObject()));
    }
    QMutexLocker lock(&m_indexLock);
    m_index.reset();
    QFile::remove(m_dir.filePath(QStringLiteral("wiki_index.json")));
}

// --------------------------------------------------------------------------- locations
std::optional<QStringList> Store::cachedLocations(const Enemy &enemy) const
{
    QMutexLocker lock(&m_cacheLock);
    const auto it = m_locCache.constFind(enemy.url);
    if (it == m_locCache.constEnd())
        return std::nullopt;
    return it.value();
}

QStringList Store::locations(const Enemy &enemy)
{
    if (const auto hit = cachedLocations(enemy))
        return *hit;
    const QString page = m_http->get(enemy.url);
    const QStringList locs = parseEnemyLocations(page);
    if (locs.isEmpty()) {   // keep the page so a parser problem can be diagnosed
        static const QRegularExpression unsafe = rx(QStringLiteral("[^A-Za-z0-9_-]"));
        QDir().mkpath(m_dir.filePath(QStringLiteral("debug")));
        QSaveFile f(m_dir.filePath(QStringLiteral("debug/") + sub(enemy.name, unsafe, QStringLiteral("_")) + QStringLiteral(".html")));
        if (f.open(QIODevice::WriteOnly)) {
            f.write(page.toUtf8());
            f.commit();
        }
        return {};
    }
    QMutexLocker lock(&m_cacheLock);
    m_locCache.insert(enemy.url, locs);
    QJsonObject o;
    for (auto it = m_locCache.begin(); it != m_locCache.end(); ++it)
        o.insert(it.key(), QJsonArray::fromStringList(it.value()));
    writeJson(QStringLiteral("locations.json"), QJsonDocument(o));
    return locs;
}

// --------------------------------------------------------------------------- shops
QJsonObject Store::bundledShops() const
{
    QFile f(BUNDLED_SHOPS);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

QHash<QString, QStringList> Store::shops() const
{
    auto key = [](const QString &k) { return k.contains(QLatin1Char('~')) ? k.toLower() : norm(k); };   // "~" keys are internal
    QHash<QString, QStringList> out;
    QJsonObject user;
    if (const auto doc = readJson(QStringLiteral("shops.json")); doc && doc->isObject())
        user = doc->object();
    for (const QJsonObject &raw : {bundledShops().value("shops").toObject(), user}) {
        for (auto it = raw.begin(); it != raw.end(); ++it) {
            QStringList labels;
            if (it.value().isString())
                labels << it.value().toString();
            else
                for (const auto &v : it.value().toArray())
                    labels << v.toString();
            QStringList &lst = out[key(it.key())];
            for (const QString &label : labels)
                if (!lst.contains(label))
                    lst << label;
        }
    }
    // Towns from the weapon list, for towns the shop list doesn't already give for that item.
    for (const auto &w : weapons) {
        if (!w->buy)
            continue;
        const QString k = norm(w->name);
        QStringList &lst = out[k];
        for (const QString &town : w->towns) {
            const bool have = std::any_of(lst.begin(), lst.end(), [&](const QString &x) {
                return norm(x.section(QStringLiteral(" ("), 0, 0)) == norm(town);
            });
            if (!have)
                lst << town + QStringLiteral(" (Weapon Shops)");
        }
        if (lst.isEmpty())
            out.remove(k);
    }
    return out;
}

QList<QPair<QString, QString>> Store::shopItems() const
{
    const QJsonObject b = bundledShops();
    const QJsonObject hints = b.value("hints").toObject();
    const QJsonObject shops = b.value("shops").toObject();
    QList<QPair<QString, QString>> out;
    for (auto it = shops.begin(); it != shops.end(); ++it)
        out.append({it.key(), hints.value(it.key()).toString()});
    return out;
}

} // namespace dq9
