// Joins the enemy drop table with the recipe lists, the weapon list and the shop list, matching
// item names across sites.
#pragma once

#include "core/models.h"

#include <QHash>
#include <QList>

namespace dq9 {

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

private:
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
