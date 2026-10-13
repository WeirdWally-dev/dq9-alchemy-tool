#include "core/render.h"

#include "core/parsers.h"
#include "core/text.h"

#include <QSet>

#include <algorithm>

namespace dq9 {

namespace {

QString locsHtml(const LocState &s)
{
    if (s.state == QStringLiteral("ok")) {
        QStringList parts;
        for (const auto &l : s.locations)
            parts << esc(l);
        return parts.join(QStringLiteral("; "));
    }
    if (s.state == QStringLiteral("error"))
        return QStringLiteral("<i style='color:#c0392b'>couldn't load</i>");
    if (s.state == QStringLiteral("unknown"))
        return QStringLiteral("<i style='color:#888'>not listed on the wiki page</i>");
    return QStringLiteral("<i style='color:#888'>looking up&hellip;</i>");
}

QString monsterTable(const QList<Dropper> &rows, int perN, const LocFn &locFn, QList<EnemyPtr> &needed)
{
    QString out = QStringLiteral("<table border='1' cellspacing='0' cellpadding='4' width='100%'>"
                                 "<tr><th align='left'>Monster</th><th align='left'>Drop odds</th><th align='left'>Where</th></tr>");
    for (int i = 0; i < rows.size() && i < perN; ++i) {
        const auto &[enemy, drop] = rows[i];
        needed << enemy;
        out += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>")
                   .arg(esc(enemy->name), esc(drop.oddsText()), locsHtml(locFn(enemy)));
    }
    out += QStringLiteral("</table>");
    if (rows.size() > perN)
        out += QStringLiteral("<p><small>+%1 more monsters drop this. Raise &quot;Monsters shown&quot; to see them.</small></p>")
                   .arg(rows.size() - perN);
    return out;
}

QString joinEsc(const QStringList &l, const QString &sep)
{
    return esc(l.join(sep));
}

QString ingredientsLine(const Ings &ings)
{
    QStringList parts;
    for (const auto &i : ings)
        parts << QStringLiteral("%1 &times; %2").arg(i.qty).arg(esc(i.name));
    return parts.join(QStringLiteral(", "));
}

// One recipe's ingredient list, then where to get each ingredient: shop, monster drops, or
// (unfolded underneath) its own recipe. `path` holds the items already being unfolded, so a recipe
// that loops back on itself stops instead of recursing forever.
QString recipeHtml(const Registry &reg, const Recipe &r, int perN, const LocFn &locFn, const Shops &shops,
                   QList<EnemyPtr> &needed, const QSet<QString> &path, int depth, const DetailFn &detailFn)
{
    QString h = QStringLiteral("<p><b>Ingredients:</b> ") + ingredientsLine(r.ingredients) + QStringLiteral("</p>");
    if (depth == 0) {
        if (!r.extra.isEmpty()) {
            QStringList parts;
            for (const auto &kv : r.extra)
                parts << esc(kv.first) + QStringLiteral(": ") + esc(kv.second);
            h += QStringLiteral("<p>") + parts.join(QStringLiteral(" &nbsp;|&nbsp; ")) + QStringLiteral("</p>");
        }
        if (!r.source.isEmpty())
            h += QStringLiteral("<p><small style='color:#888'>Source: %1</small></p>").arg(esc(r.source));
    }
    for (const auto &[nm, q] : r.ingredients) {
        const ItemPtr ing = reg.resolve(nm);
        const QString key = ing ? ing->key : norm(nm);
        const QString title = QStringLiteral("%1 &times; %2").arg(q).arg(esc(nm));
        h += depth == 0 ? QStringLiteral("<h4>%1</h4>").arg(title) : QStringLiteral("<p><b>%1</b></p>").arg(title);
        QStringList ingShops = shops.value(norm(nm));
        if (ingShops.isEmpty() && ing)
            ingShops = shops.value(ing->key);
        if (!ingShops.isEmpty())
            h += QStringLiteral("<p><b>Buy:</b> ") + joinEsc(ingShops, QStringLiteral("; ")) + QStringLiteral("</p>");
        if (ing && !ing->found.isEmpty())
            h += QStringLiteral("<p><b>Found:</b> ") + joinEsc(ing->found, QStringLiteral("; ")) + QStringLiteral("</p>");
        if (ing && !ing->droppers.isEmpty())
            h += monsterTable(ing->droppers, perN, locFn, needed);
        if (ing && !ing->recipes.isEmpty()) {
            if (path.contains(key)) {
                h += QStringLiteral("<p><i>(Its recipe loops back to an item already shown above.)</i></p>");
            } else if (depth >= MAX_NEST) {
                h += QStringLiteral("<p><i>Can be made by alchemy. Select it in the list to see how.</i></p>");
            } else {
                const int n = int(ing->recipes.size());
                const QString note = n > 1
                    ? QStringLiteral(" (showing 1 of %1 recipes; select it in the list for the rest)").arg(n)
                    : QString();
                h += QStringLiteral("<p><i>Can be made by alchemy%1:</i></p><blockquote>").arg(note);
                QSet<QString> deeper = path;
                deeper.insert(key);
                h += recipeHtml(reg, ing->recipes.first(), perN, locFn, shops, needed, deeper, depth + 1, detailFn);
                h += QStringLiteral("</blockquote>");
            }
        } else if (ingShops.isEmpty() && !(ing && (!ing->droppers.isEmpty() || !ing->found.isEmpty()))) {
            // Nothing in the bundled sources: its item page usually says (chests, grottos, quests).
            const DetailState st = detailFn ? detailFn(ing ? ing->name : nm) : DetailState{QStringLiteral("none"), std::nullopt};
            const QStringList pageFound = detailsFound(st.details);
            if (!pageFound.isEmpty())
                h += QStringLiteral("<p><b>Found:</b> ") + joinEsc(pageFound, QStringLiteral("; ")) + QStringLiteral("</p>");
            else if (st.state == QStringLiteral("loading"))
                h += QStringLiteral("<p><small><i>Looking up where to find it...</i></small></p>");
            else
                h += QStringLiteral("<p><i>No monster on the enemy list drops this and no recipe makes it. "
                                    "It may be found in the world (chests, shiny spots, quests).</i></p>");
        }
    }
    return h;
}

QString detailsHtml(const DetailState &st)
{
    if (st.state == QStringLiteral("loading"))
        return QStringLiteral("<p><small><i>Loading item details...</i></small></p>");
    if (!st.details || st.details->isEmpty())
        return QString();
    QString h;
    if (!st.details->description.isEmpty())
        h += QStringLiteral("<p><i>%1</i></p>").arg(esc(st.details->description));
    QString body;
    for (const QString &k : DETAIL_SHOWN) {
        QString value;   // a label given twice keeps its last value
        for (const auto &f : st.details->fields)
            if (f.first == k && !f.second.isEmpty())
                value = f.second;
        if (!value.isEmpty())
            body += QStringLiteral("<tr><td><b>%1</b>&nbsp;&nbsp;</td><td>%2</td></tr>").arg(esc(k), esc(value));
    }
    if (!body.isEmpty())
        h += QStringLiteral("<table cellspacing='2'>%1</table>").arg(body);
    return h;
}

QString weaponHtml(const Weapon &w)
{
    auto price = [](const std::optional<int> &v) { return v ? thousands(*v) + QStringLiteral(" G") : QString(); };
    const QList<QPair<QString, QString>> rows = {
        {"Attack", w.attack}, {"Equip", w.equipNames().join(QStringLiteral(", "))}, {"Buy price", price(w.buy)},
        {"Sell price", price(w.sell)}, {"Notes", w.notes},
    };
    QString body;
    for (const auto &[k, v] : rows)
        if (!v.isEmpty())
            body += QStringLiteral("<tr><td><b>%1</b>&nbsp;&nbsp;</td><td>%2</td></tr>").arg(esc(k), esc(v));
    if (body.isEmpty())
        return QString();
    return QStringLiteral("<table cellspacing='2'>%1</table><p><small style='color:#888'>From the weapon list: %2</small></p>")
        .arg(body, esc(WEAPON_URL));
}

QString bulletList(const QStringList &items)
{
    QString h = QStringLiteral("<ul>");
    for (const auto &s : items)
        h += QStringLiteral("<li>%1</li>").arg(esc(s));
    return h + QStringLiteral("</ul>");
}

} // namespace

Rendered renderItemHtml(const Registry &reg, const ItemInfo &info, int perN, const LocFn &locFn,
                        const Shops &shops, const DetailFn &detailFn)
{
    Rendered out;
    QString &h = out.html;
    h = QStringLiteral("<h2>%1</h2><p><b>%2</b></p>").arg(esc(info.name), esc(info.category));
    QStringList pageFound;
    if (detailFn) {
        const DetailState st = detailFn(info.name);
        h += detailsHtml(st);
        pageFound = detailsFound(st.details);
    }
    if (info.weapon)
        h += weaponHtml(*info.weapon);

    const QStringList ownShops = shops.value(info.key);
    if (!ownShops.isEmpty())
        h += QStringLiteral("<h3>Buy it</h3>") + bulletList(ownShops);
    if (!info.found.isEmpty())
        h += QStringLiteral("<h3>Other ways to get it</h3>") + bulletList(info.found);

    if (!info.recipes.isEmpty()) {
        h += QStringLiteral("<h3>Alchemy</h3>");
        for (int n = 0; n < info.recipes.size(); ++n) {
            if (info.recipes.size() > 1)
                h += QStringLiteral("<h4>Recipe %1</h4>").arg(n + 1);
            h += recipeHtml(reg, info.recipes[n], perN, locFn, shops, out.needed, {info.key}, 0, detailFn);
        }
    }
    if (!info.droppers.isEmpty()) {
        h += QStringLiteral("<h3>Dropped by monsters</h3>");
        h += monsterTable(info.droppers, perN, locFn, out.needed);
    }
    if (info.recipes.isEmpty() && info.droppers.isEmpty() && ownShops.isEmpty() && info.found.isEmpty() && pageFound.isEmpty())
        h += QStringLiteral("<p><i>No alchemy recipe or monster drop found for this item.</i></p>");
    if (ownShops.isEmpty())
        h += QStringLiteral("<p><small>Not sold in any town shop on the shop list.</small></p>");
    return out;
}

Rendered renderEnemyHtml(const EnemyPtr &enemy, const LocFn &locFn)
{
    Rendered out;
    const LocState st = locFn(enemy);
    QString &h = out.html;
    h = QStringLiteral("<h2>%1</h2>").arg(esc(enemy->name));
    QStringList stats;
    if (!enemy->exp.isEmpty())
        stats << QStringLiteral("EXP: ") + esc(enemy->exp);
    if (!enemy->gold.isEmpty())
        stats << QStringLiteral("Gold: ") + esc(enemy->gold);
    if (!stats.isEmpty())
        h += QStringLiteral("<p>") + stats.join(QStringLiteral(" &nbsp;|&nbsp; ")) + QStringLiteral("</p>");
    h += QStringLiteral("<h3>Where to find it</h3><p>") + locsHtml(st) + QStringLiteral("</p>");
    if (!enemy->drops.isEmpty()) {
        h += QStringLiteral("<h3>Drops</h3><table border='1' cellspacing='0' cellpadding='4'>"
                            "<tr><th align='left'>Item</th><th align='left'>Type</th><th align='left'>Odds</th></tr>");
        QList<Drop> drops = enemy->drops;
        std::stable_sort(drops.begin(), drops.end(), [](const Drop &a, const Drop &b) { return a.prob() > b.prob(); });
        for (const auto &d : drops)
            h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td></tr>").arg(esc(d.item), esc(d.kind), esc(d.oddsText()));
        h += QStringLiteral("</table>");
    }
    if (!enemy->url.isEmpty())
        h += QStringLiteral("<p><small>%1</small></p>").arg(esc(enemy->url));
    out.needed << enemy;
    return out;
}

} // namespace dq9
