#include "core/tree.h"

#include "core/text.h"

#include <QSet>

#include <algorithm>

namespace dq9 {

NodePtr buildTree(const Registry &reg, const ItemInfo &info, int perN, const Shops &shops, const QString &prefer,
                  const FoundFn &foundFn)
{
    int count = 0;
    auto make = [&](const QString &kind, const QString &label, const QString &sub = QString()) {
        ++count;
        auto n = std::make_shared<Node>();
        n->kind = kind;
        n->label = label;
        n->sub = sub;
        return n;
    };
    auto extraFound = [&](const QString &name, const QStringList &known, bool need) {
        QStringList out = known;
        if (foundFn && need)
            for (const QString &f : foundFn(name))
                if (!known.contains(f))
                    out << f;
        return out;
    };
    auto foundLeaf = [&](const QStringList &found, const QString &key) {
        auto n = make(QStringLiteral("found"),
                      found.size() == 1 ? QStringLiteral("Found") : QStringLiteral("Found (%1 ways)").arg(found.size()),
                      found.first());
        n->edge = QStringLiteral("get");
        n->key = key;
        n->tip = found.join(QLatin1Char('\n'));
        return n;
    };
    auto sourceOf = [&](const ItemInfo &it) -> QString {
        if (!it.recipes.isEmpty() && (prefer != QStringLiteral("drops") || it.droppers.isEmpty()))
            return QStringLiteral("alchemy");
        return it.droppers.isEmpty() ? QString() : QStringLiteral("drops");
    };

    std::function<NodePtr(const QString &, int, const QSet<QString> &, int, const QString &)> ingredient;

    // Hang the one way (per `prefer`) to get `it` under `node`, plus its shops.
    std::function<void(const NodePtr &, const ItemInfo &, const QSet<QString> &, int)> itemSources =
        [&](const NodePtr &node, const ItemInfo &it, const QSet<QString> &path, int depth) {
            const QString src = sourceOf(it);
            if (src == QStringLiteral("alchemy")) {
                if (depth > MAX_NEST || count >= MAX_NODES) {
                    node->sub = QStringLiteral("(recipe not unfolded)");
                } else {
                    QSet<QString> deeper = path;
                    deeper.insert(it.key);
                    for (int n = 0; n < it.recipes.size(); ++n) {
                        const Recipe &r = it.recipes[n];
                        NodePtr parent = node;
                        if (it.recipes.size() > 1) {
                            parent = make(QStringLiteral("recipe"), QStringLiteral("Recipe %1").arg(n + 1), r.source);
                            parent->key = it.key;
                            node->children << parent;
                        }
                        for (const auto &ing : r.ingredients)
                            parent->children << ingredient(ing.name, ing.qty, deeper, depth + 1, QString());
                    }
                }
            } else if (src == QStringLiteral("drops")) {
                for (int i = 0; i < it.droppers.size() && i < perN; ++i) {
                    const auto &[enemy, drop] = it.droppers[i];
                    auto m = make(QStringLiteral("monster"), enemy->name, drop.oddsText());
                    m->edge = QStringLiteral("drop");
                    m->enemy = enemy;
                    m->tip = enemy->name + QStringLiteral(": ") + drop.oddsText();
                    node->children << m;
                }
                const int hidden = int(it.droppers.size()) - perN;
                if (hidden > 0 && !node->children.isEmpty()) {
                    NodePtr last = node->children.last();
                    last->more = hidden;
                    last->moreKey = it.key;
                    last->tip += QStringLiteral("\n+%1 more monster%2 also drop this (tap the + or raise 'Monsters shown')")
                                     .arg(hidden).arg(hidden > 1 ? QStringLiteral("s") : QString());
                }
            }
            const QStringList labels = shops.value(it.key);
            if (!labels.isEmpty()) {
                auto s = make(QStringLiteral("shop"), QStringLiteral("Shops (%1)").arg(labels.size()), labels.first());
                s->edge = QStringLiteral("buy");
                s->key = it.key;
                s->tip = labels.join(QLatin1Char('\n'));
                node->children << s;
            }
            // Quest rewards, chests, ... always for the chosen item; for an ingredient only when they
            // aren't already behind a recipe, so big trees stay readable.
            const QStringList found = extraFound(it.name, it.found, depth == 0 || node->children.isEmpty());
            if (!found.isEmpty() && (depth == 0 || src != QStringLiteral("alchemy") || node->children.isEmpty()))
                node->children << foundLeaf(found, it.key);
        };

    ingredient = [&](const QString &name, int qty, const QSet<QString> &path, int depth, const QString &edgeIn) -> NodePtr {
        const QString edge = qty > 1 ? QStringLiteral("x%1").arg(qty) : edgeIn;
        const ItemPtr it = reg.resolve(name);
        if (!it) {
            const QStringList lab = shops.value(norm(name));
            if (!lab.isEmpty()) {
                auto n = make(QStringLiteral("shop"), name, QStringLiteral("shop"));
                n->edge = edge;
                n->tip = lab.join(QLatin1Char('\n'));
                return n;
            }
            const QStringList found = extraFound(name, {}, true);
            if (!found.isEmpty()) {
                auto n = make(QStringLiteral("base"), name);
                n->edge = edge;
                n->sprite = name;
                n->children << foundLeaf(found, QString());
                return n;
            }
            auto n = make(QStringLiteral("unknown"), name, QStringLiteral("no known source"));
            n->edge = edge;
            n->tip = QStringLiteral("No monster drops it and no recipe makes it.");
            return n;
        }
        const QString kind = sourceOf(*it) == QStringLiteral("alchemy") ? QStringLiteral("craft") : QStringLiteral("base");
        auto node = make(kind, it->name, it->category);
        node->edge = edge;
        node->key = it->key;
        node->sprite = it->name;
        if (path.contains(it->key)) {
            node->sub = QStringLiteral("(loops back)");
            return node;
        }
        itemSources(node, *it, path, depth);
        if (node->children.isEmpty() && !node->sub.startsWith(QLatin1Char('('))) {
            node->kind = QStringLiteral("unknown");
            node->sub = QStringLiteral("no known source");
        }
        return node;
    };

    auto root = make(QStringLiteral("item"), info.name, info.category);
    root->key = info.key;
    root->sprite = info.name;
    itemSources(root, info, {}, 0);
    if (root->children.isEmpty())
        root->sub = info.category + QStringLiteral("  -  no known source");
    return root;
}

QPair<double, double> layout(const NodePtr &root)
{
    int row = 0;
    std::function<void(const NodePtr &, int)> rec = [&](const NodePtr &n, int depth) {
        n->x = depth * (NODE_W + GAP_X);
        if (n->children.isEmpty()) {
            n->y = row * (NODE_H + GAP_Y);
            ++row;
        } else {
            for (const auto &c : n->children)
                rec(c, depth + 1);
            n->y = (n->children.first()->y + n->children.last()->y) / 2;
        }
    };
    rec(root, 0);
    double width = 0;
    for (const auto &n : walk(root))
        width = std::max(width, n->x);
    return {width + NODE_W, std::max(row, 1) * (NODE_H + GAP_Y) - GAP_Y};
}

QList<NodePtr> walk(const NodePtr &root)
{
    QList<NodePtr> out;
    std::function<void(const NodePtr &)> rec = [&](const NodePtr &n) {
        out << n;
        for (const auto &c : n->children)
            rec(c);
    };
    rec(root);
    return out;
}

// --------------------------------------------------------------------------- material list
bool defaultBreakdown(const ItemInfo &it, const QString &prefer)
{
    return !it.recipes.isEmpty() && (prefer != QStringLiteral("drops") || it.droppers.isEmpty());
}

QString howToGet(const ItemInfo *it, const QString &name, const Shops &shops, const FoundFn &foundFn)
{
    QStringList parts;
    QStringList labels = shops.value(it ? it->key : norm(name));
    if (labels.isEmpty())
        labels = shops.value(norm(name));
    if (!labels.isEmpty())
        parts << QStringLiteral("Buy: ") + labels.first()
                + (labels.size() > 1 ? QStringLiteral(" (+%1 more)").arg(labels.size() - 1) : QString());
    if (it && !it->droppers.isEmpty()) {
        const auto &[enemy, drop] = it->droppers.first();
        const int more = int(it->droppers.size()) - 1;
        parts << QStringLiteral("Drop: %1 %2").arg(enemy->name, drop.oddsText())
                + (more ? QStringLiteral(" (+%1 more)").arg(more) : QString());
    }
    QStringList found = it ? it->found : QStringList();
    if (parts.isEmpty() && found.isEmpty() && foundFn && !(it && !it->recipes.isEmpty()))
        found = foundFn(it ? it->name : name);
    if (!found.isEmpty())
        parts << QStringLiteral("Found: ") + found.first()
                + (found.size() > 1 ? QStringLiteral(" (+%1 more)").arg(found.size() - 1) : QString());
    if (it && !it->recipes.isEmpty())
        parts << QStringLiteral("Craftable");
    return parts.isEmpty() ? QStringLiteral("no known source") : parts.join(QStringLiteral("  ·  "));
}

namespace {
struct Tally {
    QString name;
    int qty = 0;
    ItemPtr item;
    bool loop = false;
};
// Insertion-ordered map, with "move to the end" for the crafted list's made-last order.
struct OrderedTally {
    QStringList order;
    QHash<QString, Tally> rows;
    Tally &setDefault(const QString &key, const QString &name, const ItemPtr &item)
    {
        if (!rows.contains(key)) {
            order << key;
            rows.insert(key, Tally{name, 0, item, false});
        }
        return rows[key];
    }
    void moveToEnd(const QString &key)
    {
        order.removeAll(key);
        order << key;
    }
};
} // namespace

MaterialList materialList(const Registry &reg, const ItemInfo &info, const Shops &shops, const QString &prefer,
                          const QHash<QString, bool> &breakdown, int count, int recipeIndex, const FoundFn &foundFn)
{
    MaterialList out;
    out.target = info.name;
    out.count = count;
    OrderedTally mats, crafted;

    auto wants = [&](const ItemInfo &it) {
        const auto b = breakdown.constFind(it.key);
        return b == breakdown.constEnd() ? defaultBreakdown(it, prefer) : (b.value() && !it.recipes.isEmpty());
    };

    std::function<void(const QString &, int, const QSet<QString> &, int)> add =
        [&](const QString &name, int qty, const QSet<QString> &path, int depth) {
            const ItemPtr it = reg.resolve(name);
            const QString key = it ? it->key : QLatin1Char('?') + norm(name);
            if (it && !it->recipes.isEmpty() && wants(*it) && !path.contains(key) && depth <= 50) {
                QSet<QString> deeper = path;
                deeper.insert(key);
                for (const auto &ing : it->recipes.first().ingredients)
                    add(ing.name, ing.qty * qty, deeper, depth + 1);
                crafted.setDefault(key, it->name, it).qty += qty;
                crafted.moveToEnd(key);   // keep "made last" order: ingredients before what uses them
                return;
            }
            Tally &row = mats.setDefault(key, it ? it->name : name, it);
            row.qty += qty;
            if (it && !it->recipes.isEmpty() && wants(*it) && path.contains(key))
                row.loop = true;
        };

    if (info.recipes.isEmpty()) {
        out.notes << QStringLiteral("%1 has no alchemy recipe, so there is nothing to break down.").arg(info.name);
        add(info.name, count, {}, 0);
    } else {
        const Recipe &r = info.recipes[std::clamp(recipeIndex, 0, int(info.recipes.size()) - 1)];
        for (const auto &ing : r.ingredients)
            add(ing.name, ing.qty * count, {info.key}, 1);
    }

    for (const QString &key : mats.order) {
        const Tally &t = mats.rows[key];
        ListRow row;
        row.name = t.name;
        row.key = t.item ? t.item->key : QString();
        row.qty = t.qty;
        row.craftable = t.item && !t.item->recipes.isEmpty();
        row.loop = t.loop;
        row.how = howToGet(t.item.get(), t.name, shops, foundFn);
        out.materials << row;
    }
    for (const QString &key : crafted.order) {
        const Tally &t = crafted.rows[key];
        ListRow row;
        row.name = t.name;
        row.key = t.item->key;
        row.qty = t.qty;
        row.craftable = true;
        row.expanded = true;
        if (t.item->recipes.size() > 1)
            row.how = QStringLiteral("%1 recipes, using the first").arg(t.item->recipes.size());
        out.crafted << row;
    }
    if (std::any_of(out.materials.begin(), out.materials.end(), [](const ListRow &r) { return r.loop; }))
        out.notes << QStringLiteral("Some items can't be broken down here because their recipe needs an item "
                                    "further up the list.");
    return out;
}

QString materialListText(const MaterialList &ml)
{
    auto qty = [](int q) { return QStringLiteral("%1").arg(q, 3); };
    QStringList lines = {QStringLiteral("To make %1 × %2:").arg(ml.count).arg(ml.target), QString(),
                         QStringLiteral("Gather / buy:")};
    for (const auto &r : ml.materials)
        lines << QStringLiteral("  %1 × %2    (%3)").arg(qty(r.qty), r.name, r.how);
    if (!ml.crafted.isEmpty()) {
        lines << QString() << QStringLiteral("Craft along the way (in this order):");
        for (const auto &r : ml.crafted)
            lines << QStringLiteral("  %1 × %2").arg(qty(r.qty), r.name);
        lines << QStringLiteral("  %1 × %2   (the item itself)").arg(qty(ml.count), ml.target);
    }
    if (!ml.notes.isEmpty())
        lines << QString() << ml.notes;
    return lines.join(QLatin1Char('\n'));
}

} // namespace dq9
