// Joins the enemy drop table with the recipe lists, the weapon list and the shop list, matching
// item names across sites.
#pragma once

#include "core/models.h"

#include <QHash>
#include <QList>

namespace dq9 {

// One recipe an item goes into: `product` is made from it (recipe number `recipe` of the
// product's recipes), needing `qty` of it.
struct Use {
    ItemPtr product;
    int recipe = 0;
    int qty = 1;
};

// A row of the glossary: every item, weapon and ingredient the data knows about. `item` is null
// for an ingredient that only appears inside recipes (no drop, shop or recipe of its own).
struct GlossaryEntry {
    QString id;        // stable look-up id for usedInId(): the registry key, or "?" + norm(name)
    QString name;
    QString category;
    ItemPtr item;
    int uses = 0;      // how many different items it goes into
};

class Registry {
public:
    Registry(QList<EnemyPtr> enemies, QList<Recipe> recipes,
             QList<QPair<QString, QString>> shopItems = {}, QList<WeaponPtr> weapons = {});

    const QList<EnemyPtr> &enemies() const { return m_enemies; }
    const QList<Recipe> &recipes() const { return m_recipes; }
    const QList<WeaponPtr> &weapons() const { return m_weapons; }
    const QList<QPair<QString, QString>> &shopItems() const { return m_shopItems; }

    int recipeCount() const;   // after de-duplicating the sources
    ItemPtr resolve(const QString &name) const;
    ItemPtr item(const QString &key) const { return m_items.value(key); }
    const QStringList &keys() const { return m_order; }

    QStringList categories() const;
    QList<ItemPtr> itemsIn(const QString &category, const QString &text = QString()) const;
    QList<Dropper> droppersOf(const QString &name) const;

    // Every item whose name contains `text` (ignoring case, accents, spaces and punctuation), in
    // every category. Best matches first: exact name, then names starting with it, then names with
    // a word starting with it, then the rest; alphabetical within each.
    QList<ItemPtr> search(const QString &text) const;

    // What an item is used to make, ONE level up only: the items with a recipe that takes it
    // directly. Each product is listed once (its first recipe that uses the item), sorted by name.
    QList<Use> usedIn(const QString &name) const;
    QList<Use> usedInId(const QString &id) const { return m_usedIn.value(id); }
    // The glossary id for a name (see GlossaryEntry::id); empty if no item or recipe mentions it.
    QString glossaryId(const QString &name) const;
    GlossaryEntry glossaryEntry(const QString &id) const;
    // All items plus recipe-only ingredients, filtered like search() but sorted by name.
    QList<GlossaryEntry> glossary(const QString &text = QString()) const;

private:
    void buildUsedIn();
    void addAlias(const QString &name, const QString &key);
    ItemPtr setDefault(const QString &key, const QString &name, const QString &category);
    ItemPtr itemFor(const Recipe &r);
    void build();
    void addWeapons();

    QList<EnemyPtr> m_enemies;
    QList<Recipe> m_recipes;
    QList<QPair<QString, QString>> m_shopItems;
    QList<WeaponPtr> m_weapons;
    QHash<QString, ItemPtr> m_items;
    QStringList m_order;
    QHash<QString, QString> m_alias;
    QHash<QString, int> m_catPrio;
    QHash<QString, QList<Use>> m_usedIn;   // glossary id -> what it is used to make
    QHash<QString, QString> m_loose;       // "?" + norm(name) -> name, for recipe-only ingredients
    bool m_built = false;
};

// ---- bookmarks ---------------------------------------------------------------------------
// Each bookmark with the item it points at now: by registry key, else by name (keys can change
// when a data source is refreshed or imported). nullptr if no source has the item any more.
QList<QPair<Bookmark, ItemPtr>> bookmarkItems(const Registry &reg, const QList<Bookmark> &marks);
// Point bookmarks at the items' current keys (in place). True if anything changed.
bool refreshBookmarkKeys(const Registry &reg, QList<Bookmark> &marks);
// Add or remove (in place). Returns true if it is bookmarked afterwards.
bool toggleBookmark(QList<Bookmark> &marks, const ItemInfo &info);
bool isBookmarked(const QList<Bookmark> &marks, const ItemInfo *info);
void moveBookmark(QList<Bookmark> &marks, const ItemInfo &info, int step);

} // namespace dq9
