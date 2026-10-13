#include "core/registry.h"

#include "core/parsers.h"
#include "core/text.h"
#include "core/wikitext.h"

#include <QSet>

#include <algorithm>

namespace dq9 {

namespace {

// Insertion-ordered counter; mostCommon() breaks ties by first insertion, like collections.Counter.
struct Counter {
    QList<QPair<QString, int>> counts;
    void add(const QString &k)
    {
        for (auto &c : counts)
            if (c.first == k) {
                ++c.second;
                return;
            }
        counts.append({k, 1});
    }
    QString mostCommon() const
    {
        const QPair<QString, int> *best = nullptr;
        for (const auto &c : counts)
            if (!best || c.second > best->second)
                best = &c;
        return best ? best->first : QString();
    }
};

bool sameIngredients(const Ings &a, const Ings &b)
{
    auto keyed = [](const Ings &x) {
        QList<QPair<QString, int>> out;
        for (const auto &i : x)
            out.append({norm(i.name), i.qty});
        std::sort(out.begin(), out.end());
        return out;
    };
    return keyed(a) == keyed(b);
}

QString collapsedLower(const QString &s)
{
    static const QRegularExpression ws = rx(QStringLiteral("\\s+"));
    return sub(s.trimmed().toLower(), ws, QStringLiteral(" "));
}

} // namespace

Registry::Registry(QList<EnemyPtr> enemies, QList<Recipe> recipes, QList<QPair<QString, QString>> shopItems,
                   QList<WeaponPtr> weapons)
    : m_enemies(std::move(enemies)), m_recipes(std::move(recipes)), m_shopItems(std::move(shopItems)),
      m_weapons(std::move(weapons))
{
    build();
    m_built = true;   // lookups may now fall back to a plural/singular spelling
}

int Registry::recipeCount() const
{
    int n = 0;
    for (const auto &k : m_order)
        n += int(m_items.value(k)->recipes.size());
    return n;
}

void Registry::addAlias(const QString &name, const QString &key)
{
    const QString n = norm(name);
    if (!n.isEmpty() && !m_alias.contains(n))
        m_alias.insert(n, key);
}

ItemPtr Registry::setDefault(const QString &key, const QString &name, const QString &category)
{
    auto it = m_items.find(key);
    if (it != m_items.end())
        return it.value();
    auto info = std::make_shared<ItemInfo>();
    info->key = key;
    info->name = name;
    info->category = category;
    m_items.insert(key, info);
    m_order << key;
    return info;
}

ItemPtr Registry::resolve(const QString &name) const
{
    const QString n = norm(name);
    QString key = m_alias.value(n);
    if (key.isEmpty() && n.size() > 4 && m_built) {
        // Plural spelling in one source only: the spreadsheet's "Iron nails" is the enemy list's
        // "Iron nail". Only tried when the exact name is unknown.
        key = n.endsWith(QLatin1Char('s')) ? m_alias.value(n.left(n.size() - 1)) : m_alias.value(n + QLatin1Char('s'));
    }
    return key.isEmpty() ? nullptr : m_items.value(key);
}

ItemPtr Registry::itemFor(const Recipe &r)
{
    ItemPtr info = resolve(r.item);
    if (!info) {
        const QString key = norm(r.item);
        info = setDefault(key, r.item, r.category.isEmpty() ? ITEMS_CAT : r.category);
        addAlias(r.item, key);
        return info;
    }
    if (!r.category.isEmpty() && r.category != ITEMS_CAT && info->category == ITEMS_CAT && info->key == norm(r.item)
        && collapsedLower(r.item) != collapsedLower(info->name)) {
        // Same letters once spaces are ignored, but a different thing: the wiki's "Tortoiseshell"
        // (an ingredient) vs the spreadsheet's "Tortoise shell" (a torso armour). Keep them apart;
        // the name alias keeps pointing at the ingredient.
        return setDefault(info->key + QStringLiteral("~equip"), r.item, r.category);
    }
    return info;
}

void Registry::build()
{
    QHash<QString, Counter> kinds, names;
    for (const auto &enemy : m_enemies) {
        for (const auto &d : enemy->drops) {
            const QString key = norm(d.item);
            if (key.isEmpty())
                continue;
            ItemPtr info = setDefault(key, d.item, QString());
            info->droppers.append({enemy, d});
            kinds[key].add(canonCategory(d.kind));
            names[key].add(d.item);
            addAlias(d.item, key);
            if (!d.title.isEmpty() && similar(d.item, d.title))
                addAlias(d.title, key);
        }
    }
    for (const auto &key : m_order) {
        ItemPtr info = m_items.value(key);
        info->category = kinds.value(key).mostCommon();
        info->name = names.value(key).mostCommon();
        std::stable_sort(info->droppers.begin(), info->droppers.end(), [](const Dropper &a, const Dropper &b) {
            if (a.drop.prob() != b.drop.prob())
                return a.drop.prob() > b.drop.prob();
            return a.enemy->number < b.enemy->number;
        });
    }

    for (const Recipe &r : m_recipes) {
        ItemPtr info = itemFor(r);
        const QString src = r.source.isEmpty() ? QStringLiteral("woodus") : r.source;
        auto same = std::find_if(info->recipes.begin(), info->recipes.end(),
                                 [&](const Recipe &x) { return sameIngredients(x.ingredients, r.ingredients); });
        if (same != info->recipes.end()) {
            // the same recipe from another source: merge instead of listing it twice
            QStringList sources = same->source.split(QStringLiteral(", "));
            sources << src;
            sources.removeDuplicates();
            std::sort(sources.begin(), sources.end());
            same->source = sources.join(QStringLiteral(", "));
            if (same->category.isEmpty())
                same->category = r.category;
            Pairs merged = r.extra;
            for (const auto &kv : same->extra)
                pairsSet(merged, kv.first, kv.second);
            same->extra = merged;
        } else {
            if (info->recipes.isEmpty())
                info->name = r.item;   // recipe sources' spelling is the likelier in-game name
            Recipe copy = r;
            copy.source = src;
            info->recipes.append(copy);
        }
        // Category: wiki icon < spreadsheet's block-derived type < woodus' own type label.
        const int prio = src == QStringLiteral("spreadsheet") ? 1 : 2;
        if (!r.category.isEmpty() && prio >= m_catPrio.value(info->key, 0)) {
            info->category = r.category;
            m_catPrio.insert(info->key, prio);
        }
    }

    addWeapons();

    // Things only a shop sells (no drop, no recipe) still need a place in the lists.
    for (const auto &[name, hint] : m_shopItems) {
        if (name.contains(QLatin1Char('~')) || resolve(name))
            continue;
        const QString key = norm(name);
        if (key.isEmpty())
            continue;
        ItemPtr info = setDefault(key, name, QString());
        info->name = name;
        info->category = shopCategory(name, hint);
        addAlias(name, key);
    }
}

void Registry::addWeapons()
{
    // The weapon list's type is trusted over the guesses made for the other sources; its recipe is
    // only used when no other source has one; quest rewards, chests, bonus-alchemy chances and boss
    // drops become "found" entries. N/A cells add nothing.
    QHash<QString, EnemyPtr> byEnemy;
    for (const auto &e : m_enemies)
        byEnemy.insert(norm(e->name), e);
    for (const auto &w : m_weapons) {
        ItemPtr info = resolve(w->name);
        if (!info) {
            const QString key = norm(w->name);
            info = setDefault(key, w->name, w->category);
            addAlias(w->name, key);
        }
        info->weapon = w;
        if (!w->category.isEmpty() && w->category != QStringLiteral("Other weapons")) {
            info->category = w->category;
            m_catPrio.insert(info->key, 3);
        }
        if (!w->recipe.isEmpty() && info->recipes.isEmpty()) {
            Recipe r;
            r.item = w->name;
            r.category = info->category;
            r.ingredients = w->recipe;
            if (!w->recipeNote.isEmpty())
                r.extra.append({QStringLiteral("Note"), w->recipeNote});
            r.source = QStringLiteral("weapon list");
            info->recipes.append(r);
        }
        QStringList found = w->found;
        if (!w->priceNote.isEmpty())
            found << w->priceNote;
        if (!w->recipeNote.isEmpty() && w->recipe.isEmpty())
            found << w->recipeNote;   // "20% chance to alchemize when crafting a ..."
        QSet<const Enemy *> have;
        for (const auto &d : info->droppers)
            have.insert(d.enemy.get());
        for (const auto &mark : w->drops) {
            const EnemyPtr enemy = byEnemy.value(norm(mark.monster));
            if (!enemy) {
                found << QStringLiteral("Dropped by ") + mark.monster
                        + (mark.marker.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(dropMarker(mark.marker)));
            } else if (!have.contains(enemy.get())) {
                Drop d;
                d.item = info->name;
                d.kind = w->category;
                const QString marker = dropMarker(mark.marker);
                d.note = marker.isEmpty() ? QStringLiteral("listed on the weapon list") : marker;
                info->droppers.append({enemy, d});
                have.insert(enemy.get());
            }
        }
        QStringList merged;
        for (const QString &f : found)
            if (!f.isEmpty() && !info->found.contains(f))
                merged << f;
        info->found = merged + info->found;
    }
}

QStringList Registry::categories() const
{
    QSet<QString> present;
    for (const auto &k : m_order) {
        const QString c = m_items.value(k)->category;
        if (!c.isEmpty())
            present.insert(c);
    }
    QStringList out;
    for (const QString &c : EQUIP_ORDER)
        if (present.contains(c))
            out << c;
    QStringList rest;
    for (const QString &c : present)
        if (!EQUIP_ORDER.contains(c) && c != ITEMS_CAT)
            rest << c;
    std::sort(rest.begin(), rest.end());
    out += rest;
    if (present.contains(ITEMS_CAT))
        out << ITEMS_CAT;
    return out;
}

QList<ItemPtr> Registry::itemsIn(const QString &category, const QString &text) const
{
    const QString t = text.trimmed().toLower();
    QList<ItemPtr> res;
    for (const auto &k : m_order) {
        const ItemPtr &i = m_items[k];
        if (i->category == category && i->name.toLower().contains(t))
            res << i;
    }
    std::stable_sort(res.begin(), res.end(), [](const ItemPtr &a, const ItemPtr &b) { return a->name.toLower() < b->name.toLower(); });
    return res;
}

QList<Dropper> Registry::droppersOf(const QString &name) const
{
    const ItemPtr info = resolve(name);
    return info ? info->droppers : QList<Dropper>();
}

// --------------------------------------------------------------------------- bookmarks
QList<QPair<Bookmark, ItemPtr>> bookmarkItems(const Registry &reg, const QList<Bookmark> &marks)
{
    QList<QPair<Bookmark, ItemPtr>> out;
    for (const auto &m : marks) {
        ItemPtr info = reg.item(m.key);
        if (!info && !m.name.isEmpty())
            info = reg.resolve(m.name);
        out.append({m, info});
    }
    return out;
}

bool refreshBookmarkKeys(const Registry &reg, QList<Bookmark> &marks)
{
    bool changed = false;
    const auto found = bookmarkItems(reg, marks);
    for (int i = 0; i < marks.size(); ++i) {
        const ItemPtr &info = found[i].second;
        if (info && (marks[i].key != info->key || marks[i].name != info->name)) {
            marks[i] = {info->key, info->name};
            changed = true;
        }
    }
    return changed;
}

bool toggleBookmark(QList<Bookmark> &marks, const ItemInfo &info)
{
    for (int i = 0; i < marks.size(); ++i) {
        if (marks[i].key == info.key) {
            marks.removeAt(i);
            return false;
        }
    }
    marks.append({info.key, info.name});
    return true;
}

bool isBookmarked(const QList<Bookmark> &marks, const ItemInfo *info)
{
    return info && std::any_of(marks.begin(), marks.end(), [&](const Bookmark &m) { return m.key == info->key; });
}

void moveBookmark(QList<Bookmark> &marks, const ItemInfo &info, int step)
{
    for (int i = 0; i < marks.size(); ++i) {
        if (marks[i].key == info.key) {
            const int j = i + step;
            if (j >= 0 && j < marks.size())
                marks.swapItemsAt(i, j);
            return;
        }
    }
}

} // namespace dq9
