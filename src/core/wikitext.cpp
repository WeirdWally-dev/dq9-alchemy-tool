#include "core/wikitext.h"

#include "core/text.h"

#include <QSet>

#include <algorithm>

namespace dq9 {

namespace {

const QSet<QString> &naWords()
{
    static const QSet<QString> na = {"", "n/a", "na", "-", "--", "none", "?"};
    return na;
}

bool isNa(const QString &s)
{
    return naWords().contains(stripChars(s, QStringLiteral(" .|")).toLower());
}

const QRegularExpression &fileRx()
{
    static const QRegularExpression re = rx(QStringLiteral("\\[\\[\\s*(?:File|Image)\\s*:\\s*([^|\\]]+)[^\\]]*\\]\\]"), true);
    return re;
}

const QRegularExpression &noteWords()
{
    static const QRegularExpression re = rx(QStringLiteral("\\d|reward|quest|medal|chest|grotto|map|treasure|alchem|drop|stop|"
                                                           "only|found|from|event|dlc|wi-?fi|post|bonus"), true);
    return re;
}

// 'style="x" | value' -> 'value'; a pipe inside [[..]] or {{..}} is not an attribute pipe.
QString splitAttr(const QString &cell)
{
    int depth = 0;
    qsizetype i = 0;
    while (i < cell.size()) {
        const QStringView two = QStringView(cell).mid(i, 2);
        if (two == u"[[" || two == u"{{") {
            ++depth;
            i += 2;
            continue;
        }
        if (two == u"]]" || two == u"}}") {
            depth = std::max(depth - 1, 0);
            i += 2;
            continue;
        }
        if (cell.at(i) == QLatin1Char('|') && depth == 0) {
            const QString pre = cell.left(i);
            if (pre.contains(QLatin1Char('=')) && !pre.contains(QStringLiteral("[[")))
                return cell.mid(i + 1);
            return cell;
        }
        ++i;
    }
    return cell;
}

struct Prices {
    std::optional<int> buy, sell;
    QStringList towns, notes;
};

// '1800, 900' / 'N/A, 2100' / '150g, 15g, [[Angel Falls]]' / 'Quest 363 reward, 3700g' / '7000,'
Prices parsePrices(const QString &raw)
{
    static const QRegularExpression num = rx(QStringLiteral("(\\d+)\\s*g?"), true);
    QList<std::optional<int>> priceSlots;
    Prices p;
    for (const QString &t : wtText(raw).split(QLatin1Char(','))) {
        const QString tok = t.trimmed();
        if (tok.isEmpty())
            continue;
        const auto m = fullMatch(num, QString(tok).remove(QLatin1Char(' ')));
        if (m.hasMatch())
            priceSlots.append(m.captured(1).toInt());
        else if (isNa(tok))
            priceSlots.append(std::nullopt);
        else if (contains(noteWords(), tok))
            p.notes << tok;
        else
            p.towns << tok;
    }
    if (priceSlots.size() >= 2) {
        p.buy = priceSlots[0];
        p.sell = priceSlots[1];
    } else if (!priceSlots.isEmpty()) {
        // Every row that has a buy price also gives the sell price, so a lone number is the sell price.
        p.sell = priceSlots[0];
    }
    return p;
}

// 'Iron axe x 1, [[Iron ore]] x 1' -> ingredients; free text -> ([], note). A missing quantity counts as 1.
QPair<Ings, QString> parseWeaponRecipe(const QString &raw)
{
    static const QRegularExpression paren = rx(QStringLiteral("\\s*\\(([^()]*)\\)\\s*$"));
    static const QRegularExpression digit = rx(QStringLiteral("\\d"));
    static const QRegularExpression ingRx = rx(QStringLiteral("^(.+?)\\s*[x×]\\s*(\\d+)$|^(.+?)\\s+[x×]$"), true);
    QString text = wtText(raw, false);
    if (isNa(text))
        return {};
    QString note;
    const auto m = paren.match(text);
    if (m.hasMatch() && !contains(digit, m.captured(1))) {
        note = m.captured(1).trimmed();
        text = text.left(m.capturedStart());
    }
    Ings ings;
    bool guessed = false;
    for (const QString &raw2 : text.split(QLatin1Char(','))) {
        const QString part = raw2.trimmed();
        if (part.isEmpty())
            continue;
        const auto im = ingRx.match(part);
        if (!im.hasMatch())
            return {Ings(), wtText(raw)};   // not an ingredient list: keep the whole cell as a note
        if (!im.captured(1).isEmpty()) {
            ings.append({im.captured(1).trimmed(), im.captured(2).toInt()});
        } else {
            ings.append({im.captured(3).trimmed(), 1});
            guessed = true;
        }
    }
    if (guessed) {
        QStringList parts;
        if (!note.isEmpty())
            parts << note;
        parts << QStringLiteral("the wiki leaves out a quantity, 1 is assumed");
        note = parts.join(QStringLiteral("; "));
    }
    return {ings, note};
}

// '[[Scarewolf]] (R), Claws (N)' -> [('Scarewolf', 'R'), ('Claws', 'N')]
QList<MonsterMark> parseDrops(const QString &raw)
{
    static const QRegularExpression re = rx(QStringLiteral("^(.*?)\\s*\\(([^()]*)\\)\\s*$"));
    QList<MonsterMark> out;
    for (const QString &p : wtText(raw).split(QLatin1Char(','))) {
        const QString part = p.trimmed();
        if (isNa(part))
            continue;
        const auto m = re.match(part);
        MonsterMark mm = m.hasMatch() ? MonsterMark{m.captured(1).trimmed(), m.captured(2).trimmed()}
                                      : MonsterMark{part, QString()};
        if (!mm.monster.isEmpty())
            out.append(mm);
    }
    return out;
}

} // namespace

QString wtText(const QString &input, bool keepLinkTrail)
{
    static const QRegularExpression comment = rx(QStringLiteral("<!--.*?-->"), false, true);
    static const QRegularExpression br = rx(QStringLiteral("<br\\s*/?>"), true);
    static const QRegularExpression ref = rx(QStringLiteral("<ref[^>]*/>|<ref[^>]*>.*?</ref>"), true, true);
    static const QRegularExpression tag = rx(QStringLiteral("<[^>]+>"));
    static const QRegularExpression tpl = rx(QStringLiteral("\\{\\{\\s*([^{}|]+?)\\s*(?:\\|[^{}]*)?\\}\\}"));
    static const QRegularExpression tplW = rx(QStringLiteral("\\s+W$"));
    static const QRegularExpression trail = rx(QStringLiteral("\\[\\[([^\\[\\]|]*)(?:\\|[^\\[\\]]*)?\\]\\][a-z]+"));
    static const QRegularExpression piped = rx(QStringLiteral("\\[\\[[^\\[\\]|]*\\|([^\\[\\]]*)\\]\\]"));
    static const QRegularExpression plain = rx(QStringLiteral("\\[\\[([^\\[\\]]*)\\]\\]"));
    static const QRegularExpression quotes = rx(QStringLiteral("'{2,}"));
    static const QRegularExpression ws = rx(QStringLiteral("\\s+"));

    QString s = sub(input, comment, QString());
    s = sub(s, br, QStringLiteral(", "));
    s = sub(s, ref, QString());
    s = sub(s, tag, QString());
    s = subFn(s, tpl, [](const QRegularExpressionMatch &m) { return sub(m.captured(1), tplW, QString()); });
    s = sub(s, fileRx(), QString());
    if (!keepLinkTrail)
        s = sub(s, trail, QStringLiteral("\\1"));
    s = sub(s, piped, QStringLiteral("\\1"));
    s = sub(s, plain, QStringLiteral("\\1"));
    s.remove(QStringLiteral("[[")).remove(QStringLiteral("]]"));   // the page has a few unclosed links
    s = sub(s, quotes, QString()).replace(QChar(0x00A0), QLatin1Char(' '));
    return stripChars(sub(s, ws, QStringLiteral(" ")), QStringLiteral(" |"));
}

QList<WikiTable> wtTables(const QString &text)
{
    static const QRegularExpression nl = rx(QStringLiteral("\\r\\n|\\r|\\n"));
    QList<WikiTable> tables;
    WikiTable cur;
    std::optional<QStringList> row;
    bool inside = false;
    for (const QString &line : text.split(nl)) {
        const QString s = line.trimmed();
        if (s.startsWith(QStringLiteral("{|"))) {
            inside = true;
            cur = WikiTable();
            row.reset();
            continue;
        }
        if (!inside)
            continue;
        if (s.startsWith(QStringLiteral("|}"))) {
            if (row)
                cur.rows.append(*row);
            tables.append(cur);
            inside = false;
            row.reset();
            continue;
        }
        if (s.startsWith(QStringLiteral("|-"))) {
            if (row)
                cur.rows.append(*row);
            row = QStringList();
            continue;
        }
        if (s.startsWith(QStringLiteral("|+")))
            continue;
        if (s.startsWith(QLatin1Char('!'))) {
            for (const QString &h : s.mid(1).split(QStringLiteral("!!")))
                cur.header << wtText(splitAttr(h)).toLower();
            continue;
        }
        if (s.startsWith(QLatin1Char('|'))) {
            if (!row)
                row = QStringList();
            for (const QString &c : s.mid(1).split(QStringLiteral("||")))
                row->append(splitAttr(c).trimmed());
            continue;
        }
        if (row && !row->isEmpty())   // a cell's text carried on to the next line
            row->last() = (row->last() + QLatin1Char('\n') + line).trimmed();
    }
    return tables;
}

QList<Weapon> parseWeaponWikitext(const QString &text)
{
    static const QRegularExpression equipRx = rx(QStringLiteral("^[A-Z]{2}(\\s*,\\s*[A-Z]{2})*$"));
    static const QRegularExpression digit = rx(QStringLiteral("\\d"));
    static const QRegularExpression equipSplit = rx(QStringLiteral("[\\s,/]+"));
    static const QSet<QString> placeholders = {"nopic.png", "nopic.jpg", "noimage.png"};
    static const QHash<QString, QString> spriteFixes = {
        {"longbow", "Long bow IX artwork.png"},   // the list shows Nopic.png; the artwork exists on the wiki
    };

    QList<Weapon> weapons;
    for (const WikiTable &t : wtTables(text)) {
        auto col = [&](const QStringList &keys) {
            for (int i = 0; i < t.header.size(); ++i)
                for (const QString &k : keys)
                    if (t.header[i].contains(k))
                        return i;
            return -1;
        };
        const int iName = col({"name"});
        if (iName < 0 || col({"recipe"}) < 0)
            continue;
        const QHash<QString, int> idx = {
            {"cat", col({"category", "type"})}, {"img", col({"image", "icon", "sprite"})}, {"atk", col({"attack"})},
            {"equip", col({"equip", "vocation"})}, {"price", col({"buy", "price"})}, {"recipe", col({"recipe"})},
            {"area", col({"area", "location", "where"})}, {"drop", col({"drop"})}, {"notes", col({"note"})},
        };
        for (const QStringList &r : t.rows) {
            auto cell = [&](const QString &k) {
                const int i = k == QStringLiteral("name") ? iName : idx.value(k, -1);
                return (i >= 0 && i < r.size()) ? r[i] : QString();
            };
            Weapon w;
            w.name = wtText(cell("name"));
            if (w.name.isEmpty())
                continue;
            QString equipRaw = wtText(cell("equip"));
            QString priceRaw = cell("price");
            if (contains(digit, equipRaw) && contains(equipRx, wtText(priceRaw))) {   // columns swapped
                const QString swapped = wtText(priceRaw);
                priceRaw = cell("equip");
                equipRaw = swapped;
            }
            const Prices pr = parsePrices(priceRaw);
            const auto [recipe, recipeNote] = parseWeaponRecipe(cell("recipe"));

            const QString area = wtText(cell("area"));
            QStringList towns = pr.towns, found;
            if (!isNa(area)) {
                QStringList parts;
                for (const QString &p : area.split(QLatin1Char(',')))
                    if (!isNa(p))
                        parts << p.trimmed();
                const bool anyNote = std::any_of(parts.begin(), parts.end(), [](const QString &p) { return contains(noteWords(), p); });
                if (pr.buy && !parts.isEmpty() && !anyNote) {
                    const QStringList before = towns;
                    for (const QString &p : parts)
                        if (!before.contains(p))
                            towns << p;
                } else {
                    found << area;
                }
            }
            if (!pr.buy && !towns.isEmpty()) {   // towns listed for something you can't buy: where it's found
                found += towns;
                towns.clear();
            }

            const auto m = fileRx().match(cell("img"));
            QString image = m.hasMatch() ? m.captured(1).trimmed() : QString();
            if (placeholders.contains(image.toLower().replace(QLatin1Char(' '), QLatin1Char('_'))))
                image.clear();
            image = spriteFixes.value(skey(w.name), image);

            const QString catCell = cell("cat");
            w.category = catCell.isEmpty() ? QStringLiteral("Other weapons") : canonCategory(wtText(catCell));
            w.attack = wtText(cell("atk"));
            for (const QString &v : equipRaw.split(equipSplit))
                if (!v.isEmpty() && !isNa(v))
                    w.equip << v;
            w.buy = pr.buy;
            w.sell = pr.sell;
            w.priceNote = pr.notes.join(QStringLiteral("; "));
            w.recipe = recipe;
            w.recipeNote = recipeNote;
            w.towns = towns;
            w.found = found;
            w.drops = parseDrops(cell("drop"));
            const QString notes = wtText(cell("notes"));
            w.notes = isNa(notes) ? QString() : notes;
            w.image = image;
            weapons.append(w);
        }
    }
    if (weapons.isEmpty())
        throw ParseError(QStringLiteral("No weapon table (with Name and Recipes columns) was found on the weapon list page."));
    return weapons;
}

QHash<QString, QString> weaponSprites(const QList<WeaponPtr> &weapons)
{
    QHash<QString, QString> out;
    for (const auto &w : weapons)
        if (!w->image.isEmpty())
            out.insert(skey(w->name), w->spriteUrl());
    return out;
}

QString dropMarker(const QString &marker)
{
    const QString up = marker.toUpper();
    if (up == QStringLiteral("R"))
        return QStringLiteral("rare drop");
    if (up == QStringLiteral("N"))
        return QStringLiteral("common drop");
    return marker;
}

} // namespace dq9
