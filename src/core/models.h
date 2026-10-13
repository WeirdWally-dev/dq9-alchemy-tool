// Plain data types shared by every part of the app.
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>
#include <stdexcept>

namespace dq9 {

inline const QString ITEMS_CAT = QStringLiteral("Ingredients & items");
inline const QString BOOKMARKS_CAT = QStringLiteral("★ Bookmarks");
inline const QStringList EQUIP_ORDER = {
    "Swords", "Spears", "Axes", "Staves", "Wands", "Whips", "Claws", "Knives", "Bows",
    "Hammers", "Fans", "Boomerangs", "Shields", "Head", "Torso", "Arms", "Legs", "Feet",
    "Accessories",
};

inline const QString ENEMY_URL = QStringLiteral("https://dragon-quest.org/wiki/List_of_enemies_in_Dragon_Quest_IX");
inline const QString RECIPE_URL = QStringLiteral("https://www.woodus.com/den/games/dq9ds/alchemy_recipes.php");
inline const QString SPRITE_URL = QStringLiteral("https://dragon-quest.org/w/index.php?title=List_of_alchemy_ingredients_in_Dragon_Quest_IX");
inline const QString WIKIDOT = QStringLiteral("https://dqnine.wikidot.com");
inline const QString WIKIDOT_INDEX = WIKIDOT + QStringLiteral("/alchemy");
inline const QString WEAPON_PAGE = QStringLiteral("List_of_weapons_in_Dragon_Quest_IX");
inline const QString WEAPON_URL = QStringLiteral("https://dragon-quest.org/wiki/") + WEAPON_PAGE;
inline const QString WEAPON_API = QStringLiteral("https://dragon-quest.org/w/api.php?action=parse&format=json&formatversion=2&prop=wikitext&page=") + WEAPON_PAGE;
inline const QString WEAPON_RAW = QStringLiteral("https://dragon-quest.org/w/index.php?title=") + WEAPON_PAGE + QStringLiteral("&action=raw");
inline const QString WIKI_IMAGES = QStringLiteral("https://dragon-quest.org/w/images");

class ParseError : public std::runtime_error {
public:
    explicit ParseError(const QString &msg) : std::runtime_error(msg.toStdString()) {}
};

class RobotsDisallowed : public std::runtime_error {
public:
    explicit RobotsDisallowed(const QString &url) : std::runtime_error(url.toStdString()) {}
};

class HttpError : public std::runtime_error {
public:
    HttpError(int status, const QString &msg) : std::runtime_error(msg.toStdString()), status(status) {}
    int status;   // 0 when the request never got an answer (offline, timeout, TLS)
};

QString errorText(const std::exception &e);

struct Ing {
    QString name;
    int qty = 1;
    bool operator==(const Ing &o) const { return name == o.name && qty == o.qty; }
};
using Ings = QList<Ing>;

// Ordered string map (Python dicts keep insertion order, and the app shows these in order).
using Pairs = QList<QPair<QString, QString>>;
QString pairsValue(const Pairs &p, const QString &key, const QString &def = QString());
void pairsSet(Pairs &p, const QString &key, const QString &value);

struct Drop {
    QString item;
    QString kind;      // icon label on the wiki: Item, Sword, Torso, ...
    int num = 0;
    int den = 0;
    QString title;     // wiki link title, sometimes a fuller item name
    QString note;      // odds text when the rate isn't known ("rare drop")

    double prob() const { return den ? double(num) / den : 0.0; }
    QString oddsText() const;
    QJsonObject toJson() const;
    static Drop fromJson(const QJsonObject &o);
};

struct Enemy {
    int number = 0;
    QString name;
    QString url;
    QString gold;
    QString exp;
    QList<Drop> drops;

    QJsonObject toJson() const;
    static Enemy fromJson(const QJsonObject &o);
};
using EnemyPtr = std::shared_ptr<const Enemy>;

struct Recipe {
    QString item;
    QString category;
    Ings ingredients;
    Pairs extra;
    QString source;    // "woodus" (or ""), "spreadsheet", "weapon list", or several once merged

    QJsonObject toJson() const;
    static Recipe fromJson(const QJsonObject &o);
    bool operator==(const Recipe &o) const;
};

struct MonsterMark {
    QString monster;
    QString marker;   // "R", "N", ...
    bool operator==(const MonsterMark &o) const { return monster == o.monster && marker == o.marker; }
};

// One row of the wiki's "List of weapons in Dragon Quest IX" table.
struct Weapon {
    QString name;
    QString category;
    QString attack;
    QStringList equip;              // vocation abbreviations (GL, RA, ...)
    std::optional<int> buy;
    std::optional<int> sell;
    QString priceNote;              // "32 Mini Medals", "Quest 363 reward"
    Ings recipe;
    QString recipeNote;             // "20% chance to alchemize when ..."
    QStringList towns;              // where it's sold (only when it has a buy price)
    QStringList found;              // other ways to get it: quest, chest, ...
    QList<MonsterMark> drops;
    QString notes;
    QString image;                  // wiki file name, "" if none

    QString spriteUrl() const;
    QStringList equipNames() const;
};
using WeaponPtr = std::shared_ptr<const Weapon>;

struct Dropper {
    EnemyPtr enemy;
    Drop drop;
};

struct ItemInfo {
    QString key;
    QString name;
    QString category;
    QList<Recipe> recipes;
    QList<Dropper> droppers;
    WeaponPtr weapon;               // its row on the wiki's weapon list
    QStringList found;              // other ways to get it (quest, chest, ...)
};
using ItemPtr = std::shared_ptr<ItemInfo>;

// An item's dqnine.wikidot.com page facts: {description, [[label, value], ...]}.
struct Details {
    QString description;
    QList<QPair<QString, QString>> fields;
    bool isEmpty() const { return description.isEmpty() && fields.isEmpty(); }
    QJsonObject toJson() const;
    static Details fromJson(const QJsonObject &o);
};

struct Bookmark {
    QString key;
    QString name;
    bool operator==(const Bookmark &o) const { return key == o.key && name == o.name; }
};

QString wikiImageUrl(const QString &fileName);

} // namespace dq9
