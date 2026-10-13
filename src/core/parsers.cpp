#include "core/parsers.h"

#include "core/html.h"
#include "core/text.h"

#include <QSet>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace dq9 {

using html::Document;
using html::Node;

static const QSet<QString> NA_WORDS = {"", "n/a", "na", "-", "--", "none", "?"};

// --------------------------------------------------------------------------- links
QString wikiUrl(const QString &href)
{
    const QUrl full = QUrl(ENEMY_URL).resolved(QUrl(href));
    if (full.path().endsWith(QStringLiteral("index.php"))) {
        const QUrlQuery q(full);
        QString title = q.queryItemValue(QStringLiteral("title"), QUrl::FullyEncoded);
        title = pyUnquote(title.replace(QLatin1Char('+'), QLatin1Char(' ')));
        if (!title.isEmpty())
            return QStringLiteral("https://dragon-quest.org/wiki/")
                + pyQuote(title.replace(QLatin1Char(' '), QLatin1Char('_')), QStringLiteral("_():,'!-"));
    }
    return full.adjusted(QUrl::RemoveFragment).toString(QUrl::FullyEncoded);
}

// --------------------------------------------------------------------------- enemy list
static QList<Drop> parseLoot(const Node *td)
{
    QList<Drop> drops;
    std::optional<QString> kind;
    std::optional<Drop> pending;

    auto flush = [&](std::optional<QPair<int, int>> rate = std::nullopt) {
        if (pending) {
            if (rate) {
                pending->num = rate->first;
                pending->den = rate->second;
            }
            drops.append(*pending);
            pending.reset();
        }
    };

    for (Node *node : td->descendants()) {
        if (node->is(QStringLiteral("img"))) {
            const QString alt = node->attr(QStringLiteral("alt"));
            kind = (alt.isEmpty() ? QStringLiteral("Item") : alt).trimmed();
        } else if (node->is(QStringLiteral("a"))) {
            const QString txt = node->getText(QStringLiteral(" "), true);
            if (!txt.isEmpty()) {
                flush();
                Drop d;
                d.item = txt;
                d.title = node->attr(QStringLiteral("title"));
                d.kind = (kind && !kind->isEmpty()) ? *kind : QStringLiteral("Item");
                pending = d;
                kind.reset();
            }
        } else if (node->isText() && !(node->parent && node->parent->is(QStringLiteral("a")))) {
            if (pending && !node->text.trimmed().isEmpty()) {
                if (const auto rate = parseRate(node->text))
                    flush(rate);
            }
        }
    }
    flush();
    return drops;
}

QList<Enemy> parseEnemyList(const QString &htmlText)
{
    Document doc(htmlText);
    static const QRegularExpression numRx = rx(QStringLiteral("#\\s*(\\d+)"));
    QList<Enemy> enemies;
    for (Node *table : doc.root()->findAll({QStringLiteral("table")})) {
        Node *head = table->find({QStringLiteral("tr")});
        const QString headText = head ? head->getText(QStringLiteral(" ")).toLower() : QString();
        if (!headText.contains(QStringLiteral("enemy")) || !headText.contains(QStringLiteral("loot")))
            continue;
        for (Node *tr : table->findAll({QStringLiteral("tr")})) {
            const auto tds = tr->childElements({QStringLiteral("td")});
            if (tds.size() < 4)
                continue;
            const auto m = numRx.match(tds[0]->getText(QStringLiteral(" ")));
            Node *a = tds[0]->find({QStringLiteral("a")});
            if (!m.hasMatch() || !a)
                continue;
            Enemy e;
            e.number = m.captured(1).toInt();
            e.name = a->getText(QStringLiteral(" "), true);
            e.url = wikiUrl(a->attr(QStringLiteral("href")));
            e.gold = tds[1]->getText(QStringLiteral(" "), true);
            e.exp = tds[2]->getText(QStringLiteral(" "), true);
            e.drops = parseLoot(tds[3]);
            enemies.append(e);
        }
    }
    if (enemies.isEmpty())
        throw ParseError(QStringLiteral("No enemy table with 'Enemy' and 'Loot' columns was found on the page."));
    return enemies;
}

// --------------------------------------------------------------------------- enemy page: locations
static int headingLevel(const Node *el)
{
    if (!el || !el->isElement() || el->tag.size() != 2 || el->tag.at(0) != QLatin1Char('h'))
        return 0;
    const int lvl = el->tag.at(1).digitValue();
    return (lvl >= 1 && lvl <= 6) ? lvl : 0;
}

static std::vector<Node *> dq9SectionNodes(const Node *root)
{
    Node *heading = nullptr;
    for (Node *h : root->findAll([](const Node *n) { const int l = headingLevel(n); return l >= 2 && l <= 4; })) {
        const QString t = h->getText(QStringLiteral(" "));
        if (t.contains(QStringLiteral("Dragon Quest IX")) && t.contains(QStringLiteral("Sentinels"))) {
            heading = h;
            break;
        }
    }
    if (!heading)
        return {};
    const int level = headingLevel(heading);
    const Node *start = heading;
    if (heading->parent && heading->parent->is(QStringLiteral("div")) && heading->parent->hasClass(QStringLiteral("mw-heading")))
        start = heading->parent;

    auto isStop = [&](const Node *el) {
        if (!el->isElement())
            return false;
        int l = headingLevel(el);
        if (!l && el->hasClass(QStringLiteral("mw-heading"))) {
            const Node *h = el->findAll([](const Node *n) { return headingLevel(n) > 0; }).empty()
                ? nullptr
                : el->findAll([](const Node *n) { return headingLevel(n) > 0; }).front();
            l = headingLevel(h);
        }
        return l > 0 && l <= level;
    };

    std::vector<Node *> nodes;
    for (Node *sib : start->nextSiblings()) {
        if (isStop(sib))
            break;
        if (sib->isElement())
            nodes.push_back(sib);
    }
    return nodes;
}

static QStringList followingLines(const Node *anchor)
{
    static const QRegularExpression stopRx = rx(
        QStringLiteral("^\\s*(items?\\b.*\\b(dropped|drops?)\\b|dropped items?|item drops?|stolen items?|"
                       "\\w[\\w /-]*resistance|family|experience|spells?\\b|skills?\\b|in-game description)"),
        true);
    QString text;
    for (const Node *s : anchor->nextSiblings())
        text += html::flatten(s);
    QStringList lines;
    for (const QString &line : html::cleanLines(text)) {
        if (contains(stopRx, line)) {
            if (lines.isEmpty())
                return {};
            break;
        }
        lines << line;
    }
    return lines;
}

QStringList parseEnemyLocations(const QString &htmlText)
{
    Document doc(htmlText);
    static const QRegularExpression labelRx = rx(QStringLiteral("^\\s*Locations?\\s*(\\(s\\))?\\s*:?\\s*$"), true);
    QStringList results;
    for (Node *node : dq9SectionNodes(doc.root())) {
        for (Node *label : node->findAll([](const Node *n) { return n->isText() && contains(labelRx, n->text); })) {
            const Node *anchor = label;
            QStringList lines;
            for (int i = 0; i < 4; ++i) {
                lines = followingLines(anchor);
                if (!lines.isEmpty())
                    break;
                anchor = anchor->parent;
                if (!anchor || anchor->type == Node::Document || anchor->is(QStringLiteral("body"))
                    || anchor->is(QStringLiteral("html")))
                    break;
            }
            for (const QString &line : lines) {
                const QString low = line.toLower();
                if (low != QStringLiteral("n/a") && low != QStringLiteral("-") && low != QStringLiteral("none")
                    && !results.contains(line))
                    results << line;
            }
        }
    }
    return results;
}

// --------------------------------------------------------------------------- recipes (woodus)
Ings parseIngredients(const QString &text)
{
    static const QRegularExpression splitter = rx(QStringLiteral("\\n|,|\\+|;|&"));
    static const QRegularExpression lead = rx(QStringLiteral("^(\\d+)\\s*[x×]?\\s+(.+)$"), true);
    static const QRegularExpression trail = rx(QStringLiteral("^(.+?)\\s*[x×]\\s*(\\d+)$"), true);
    static const QRegularExpression ws = rx(QStringLiteral("\\s+"));
    Ings out;
    for (const QString &raw : text.split(splitter)) {
        const QString part = stripChars(sub(raw, ws, QStringLiteral(" ")), QStringLiteral(" ."));
        if (part.isEmpty())
            continue;
        const auto m = lead.match(part);
        const auto m2 = trail.match(part);
        if (m.hasMatch())
            out.append({m.captured(2).trimmed(), m.captured(1).toInt()});
        else if (m2.hasMatch())
            out.append({m2.captured(1).trimmed(), m2.captured(2).toInt()});
        else
            out.append({part, 1});
    }
    return out;
}

QList<Recipe> parseRecipes(const QString &htmlText)
{
    static const QStringList ingKeys = {"ingredient", "material", "component", "needed", "required", "requires"};
    static const QStringList itemKeys = {"item", "name", "result", "product", "creates"};
    static const QStringList typeKeys = {"type", "category"};

    Document doc(htmlText);
    QList<Recipe> recipes;
    QStringList seenHeaders;
    for (Node *table : doc.root()->findAll({QStringLiteral("table")})) {
        const auto rows = table->findAll({QStringLiteral("tr")});
        if (rows.size() < 2)
            continue;
        QStringList header;
        for (Node *c : rows[0]->findAll({QStringLiteral("th"), QStringLiteral("td")}))
            header << c->getText(QStringLiteral(" "), true).toLower();
        seenHeaders << header.join(QStringLiteral(" | ")).left(120);

        auto has = [](const QString &h, const QStringList &keys) {
            return std::any_of(keys.begin(), keys.end(), [&](const QString &k) { return h.contains(k); });
        };
        auto findCol = [&](const QStringList &keys, int exclude) {
            for (int i = 0; i < header.size(); ++i)
                if (i != exclude && has(header[i], keys))
                    return i;
            return -1;
        };

        int iType = findCol(typeKeys, -1);
        int iItem = findCol(itemKeys, iType);
        QList<int> ingCols;
        for (int i = 0; i < header.size(); ++i)
            if (has(header[i], ingKeys))
                ingCols << i;
        size_t firstData = 1;
        if (iItem < 0 && ingCols.isEmpty()) {
            // no recognisable header: assume item | type | ingredients...
            iItem = 0;
            iType = 1;
            ingCols = {2};
            firstData = 0;
        } else {
            if (iItem < 0)
                iItem = 0;
            if (ingCols.isEmpty()) {
                for (int i = 0; i < header.size(); ++i)
                    if (i != iItem && i != iType) {
                        ingCols << i;
                        break;
                    }
            }
        }
        const int maxCol = std::max(iItem, ingCols.isEmpty() ? 0 : *std::max_element(ingCols.begin(), ingCols.end()));
        for (size_t r = firstData; r < rows.size(); ++r) {
            const auto cells = rows[r]->childElements({QStringLiteral("td"), QStringLiteral("th")});
            if (int(cells.size()) <= maxCol)
                continue;
            const QString item = cells[iItem]->getText(QStringLiteral(" "), true);
            Ings ingredients;
            for (int c : ingCols)
                ingredients += parseIngredients(html::flatten(cells[c]));
            if (item.isEmpty() || ingredients.isEmpty())
                continue;
            const QString rawType = (iType >= 0 && iType < int(cells.size()))
                ? cells[iType]->getText(QStringLiteral(" "), true) : QString();
            Pairs extra;
            for (int i = 0; i < int(cells.size()); ++i) {
                if (i == iItem || i == iType || ingCols.contains(i) || i >= header.size() || header[i].isEmpty())
                    continue;
                const QString v = cells[i]->getText(QStringLiteral(" "), true);
                if (!v.isEmpty())
                    pairsSet(extra, titleCase(header[i]), v);
            }
            Recipe rec;
            rec.item = item;
            rec.category = rawType.isEmpty() ? QString() : canonCategory(rawType);
            rec.ingredients = ingredients;
            rec.extra = extra;
            recipes.append(rec);
        }
    }
    if (recipes.isEmpty()) {
        QString hint = seenHeaders.mid(0, 5).join(QStringLiteral("; "));
        if (hint.isEmpty())
            hint = QStringLiteral("no tables at all");
        throw ParseError(QStringLiteral("No recipes could be read from that page. Table headers seen: ") + hint
                         + QStringLiteral(". If the recipes are loaded by JavaScript or revealed on click, a saved copy won't contain them."));
    }
    return recipes;
}

// --------------------------------------------------------------------------- recipe spreadsheet
// The spreadsheet has four column pairs (Weapon / Armor / Accessory / Item name + materials) but
// never says WHICH weapon or armour type an entry is. It does list each type as its own
// alphabetical run (all swords A-Z, then all spears A-Z, ...), so a drop backwards in the
// alphabet marks the start of a new type. Each run is then named by a vote over the keywords in
// its item names.
const PatternList &subtypePatterns(const QString &group)
{
    static const PatternList weapon = {
        {"Swords", "sword|blade\\b|foil\\b|rapier"},
        {"Spears", "spear|lance|trident|poker|halberd|partisan"},
        {"Knives", "dagger|knife|dirk|needle|\\bfang\\b|\\btang\\b"},
        {"Wands", "wand|staff"},
        {"Whips", "whip|sidewinder"},
        {"Staves", "stick|\\brod\\b|\\bbar\\b|pillar|cosh|cudgel"},
        {"Claws", "claw|handrill"},
        {"Fans", "\\bfan\\b"},
        {"Axes", "\\baxe\\b|climaxe"},
        {"Hammers", "hammer|maul\\b"},
        {"Boomerangs", "boomerang|rang\\b|wing\\b"},
        {"Bows", "\\bbow\\b|longbow"},
    };
    static const PatternList armor = {
        {"Shields", "shield|platter"},
        {"Head", "\\bhat\\b|helm|\\bcap\\b|crown|mask|turban|bandana|hood|circlet|tiara|\\bears\\b|"
                 "headband|headgear|coif"},
        {"Torso", "armou?r|mail\\b|robes?\\b|dress|gown|raiment|cape\\b|cloak|mantle|coat|poncho|"
                  "vest|bustier|\\btop\\b|suit|tuxedo|shirt|tutu|toga|attire|cuirass"},
        {"Arms", "glove|gauntlet|mitten|bindings?\\b"},
        {"Legs", "trouser|pants|tights|legging|kilt|briefs|bottoms|kneecap|socks"},
        {"Feet", "boots?\\b|shoes?\\b|sandal|clogs?\\b|heels?\\b|pumps?\\b|sabaton"},
    };
    static const PatternList none;
    if (group == QStringLiteral("Weapon"))
        return weapon;
    if (group == QStringLiteral("Armor"))
        return armor;
    return none;
}

static const PatternList &shopExtraPatterns(const QString &group)
{
    static const PatternList weapon = {
        {"Staves", "pole\\b|mop\\b|wrench|stick"}, {"Wands", "wand|staff|rod\\b"},
        {"Hammers", "hammer|mace\\b|tamper"}, {"Spears", "fork\\b"}, {"Swords", "dragonsbane"},
    };
    static const PatternList armor = {
        {"Shields", "\\blid\\b"},
        {"Head", "headwear|ribbon|\\bband\\b|bonnet|topper|shades|specs|afro|moustache|veil|cosy|"
                 "mitre|sweatband|hairband|\\bhat\\b|\\bcap\\b|pumpkin head|skull cap|\\bears\\b"},
        {"Arms", "mitts|handwear|fingerwear|bracers?\\b|armguard|bracelets"},
        {"Legs", "skirt|slacks|jeans|knickers|shorts|britches|pantaloons|stockings|trousers|bottoms"},
        {"Feet", "geta|footwear|sollerets|wellies|wellington|steppers|sneakers|wing-tips|"
                 "agiliboots|acroboots|sandals|shoes|boots"},
        {"Torso", "garb|costume|outfit|clothes|t-shirt|shirt|blazer|jaguarment|midriff|bikini|"
                  "\\btop\\b|suit|smart suit|dress|cuirass|mail\\b"},
    };
    static const PatternList none;
    if (group == QStringLiteral("Weapon"))
        return weapon;
    if (group == QStringLiteral("Armor"))
        return armor;
    return none;
}

static QString otherLabel(const QString &group)
{
    if (group == QStringLiteral("Weapon"))
        return QStringLiteral("Other weapons");
    if (group == QStringLiteral("Armor"))
        return QStringLiteral("Other armour");
    return QString();
}

QList<QList<int>> alphabeticalRuns(const QStringList &names)
{
    QList<QList<int>> runs;
    std::optional<QString> prev;
    for (int i = 0; i < names.size(); ++i) {
        const QString key = sortKey(names[i]);
        if (!prev || key < *prev)
            runs.append(QList<int>());
        runs.last().append(i);
        prev = key;
    }
    return runs;
}

static QStringList labelRuns(const QStringList &names, const PatternList &patterns)
{
    QList<QPair<QString, QRegularExpression>> compiled;
    for (const auto &p : patterns)
        compiled.append({p.first, rx(p.second, true)});
    QStringList labels;
    for (int i = 0; i < names.size(); ++i)
        labels << QString();
    for (const auto &run : alphabeticalRuns(names)) {
        QList<QPair<QString, int>> votes;   // insertion-ordered counter
        for (int i : run) {
            for (const auto &c : compiled) {
                if (!contains(c.second, names[i]))
                    continue;
                auto it = std::find_if(votes.begin(), votes.end(), [&](const auto &v) { return v.first == c.first; });
                if (it == votes.end())
                    votes.append({c.first, 1});
                else
                    ++it->second;
            }
        }
        std::stable_sort(votes.begin(), votes.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
        if (!votes.isEmpty() && (votes.size() == 1 || votes[0].second > votes[1].second))
            for (int i : run)
                labels[i] = votes[0].first;
    }
    return labels;
}

Ings parseSheetIngredients(const QString &text)
{
    static const QRegularExpression qty = rx(QStringLiteral("^(.*?)\\s*\\((\\d+)\\)\\s*$"));
    Ings out;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = squash(raw);
        if (line.isEmpty())
            continue;
        const auto m = qty.match(line);
        if (m.hasMatch())
            out.append({m.captured(1).trimmed(), m.captured(2).toInt()});
        else
            out.append({line, 1});
    }
    return out;
}

QList<Recipe> recipesFromColumns(const SheetColumns &columns)
{
    QList<Recipe> out;
    for (const auto &[group, rawRows] : columns) {
        QList<QPair<QString, QString>> rows;
        QStringList names;
        for (const auto &r : rawRows) {
            const QString n = r.first.trimmed();
            if (!n.isEmpty()) {
                rows.append({n, r.second});
                names << n;
            }
        }
        const PatternList &pats = subtypePatterns(group);
        QStringList labels = pats.isEmpty() ? QStringList() : labelRuns(names, pats);
        for (int i = 0; i < rows.size(); ++i) {
            Ings ingredients;
            if (!rows[i].second.isEmpty()) {
                for (auto ing : parseSheetIngredients(rows[i].second)) {
                    if (ing.name.toLower() == QStringLiteral("agressence"))   // typo in the spreadsheet
                        ing.name = QStringLiteral("Aggressence");
                    ingredients.append(ing);
                }
            }
            if (ingredients.isEmpty())
                continue;
            QString category = labels.isEmpty() ? QString() : labels[i];
            if (category.isEmpty()) {
                if (group == QStringLiteral("Accessory"))
                    category = QStringLiteral("Accessories");
                else if (group == QStringLiteral("Item"))
                    category = ITEMS_CAT;
                else
                    category = otherLabel(group);
                if (category.isEmpty())
                    category = group;
            }
            Recipe rec;
            rec.item = rows[i].first;
            rec.category = category;
            rec.ingredients = ingredients;
            rec.source = QStringLiteral("spreadsheet");
            out.append(rec);
        }
    }
    return out;
}

// --------------------------------------------------------------------------- item pages
const QStringList DETAIL_SHOWN = {"Classification", "Used by", "Attack", "Defence", "Defense", "Style", "Special",
                                  "Rarity", "Buy price", "Sell price", "Where to find", "Reward for", "Can be used for"};

std::optional<Details> parseItemPage(const QString &htmlText, const QString &name)
{
    static const QStringList known = {"Classification", "Used by", "Attack", "Defence", "Defense", "Style", "Special",
                                      "Rarity", "Buy price", "Sell price", "How to make", "Where to find", "Reward for",
                                      "Can be used for"};
    static const QSet<QString> subLabels = {"Use", "Equip", "Note", "Notes", "Tip", "Warning"};
    static const QRegularExpression detailRx = rx(QStringLiteral("^([A-Z][A-Za-z' ]{1,24}?)\\s*:\\s*(.*)$"));

    Document doc(htmlText);
    const Node *root = doc.findById(QStringLiteral("page-content"));
    if (!root)
        root = doc.body();
    if (!root)
        root = doc.root();
    QStringList desc;
    QList<QPair<QString, QString>> fields;
    for (const QString &line : html::cleanLines(html::flatten(root, true))) {
        if (fields.isEmpty() && dkey(line) == dkey(name))
            continue;   // the page title repeated in the body
        const auto m = detailRx.match(line);
        QString label, value;
        if (m.hasMatch()) {
            label = m.captured(1).trimmed();
            value = m.captured(2).trimmed();
        } else {
            QString l = line;
            while (l.endsWith(QLatin1Char(':')))
                l.chop(1);
            label = l.trimmed();
        }
        const bool fieldLine = (m.hasMatch() && !subLabels.contains(label)
                                && label.split(QLatin1Char(' '), Qt::SkipEmptyParts).size() <= 3)
            || (!m.hasMatch() && known.contains(label));
        if (fieldLine) {
            fields.append({label, value});
            continue;
        }
        if (!fields.isEmpty())
            fields.last().second = stripChars(fields.last().second + QStringLiteral("; ") + line, QStringLiteral("; "));
        else
            desc << line;
    }
    if (fields.isEmpty())
        return std::nullopt;
    Details d;
    for (auto &f : fields)
        f.second = f.second.left(400);
    d.fields = fields;
    d.description = desc.join(QLatin1Char(' ')).left(600);
    return d;
}

QStringList detailsFound(const std::optional<Details> &det)
{
    static const QList<QPair<QString, QString>> foundFields = {
        {"Where to find", ""}, {"Reward for", "Quest reward: "}, {"Location", ""}, {"Locations", ""},
        {"Obtained", ""}, {"Found", ""}, {"How to obtain", ""},
    };
    QStringList out;
    if (!det)
        return out;
    for (const auto &[label, value] : det->fields) {
        auto it = std::find_if(foundFields.begin(), foundFields.end(), [&](const auto &f) { return f.first == label; });
        if (it == foundFields.end() || value.isEmpty())
            continue;
        for (const QString &raw : value.split(QStringLiteral("; "))) {
            const QString part = stripChars(raw, QStringLiteral(" .;"));
            const QString low = part.toLower();
            if (!part.isEmpty() && !NA_WORDS.contains(low) && low != QStringLiteral("unknown")
                && low != QStringLiteral("not available")) {
                const QString entry = it->second + part;
                if (!out.contains(entry))
                    out << entry;
            }
        }
    }
    return out;
}

QHash<QString, QString> parseWikiIndex(const QString &htmlText)
{
    Document doc(htmlText);
    const Node *root = doc.findById(QStringLiteral("page-content"));
    if (!root)
        root = doc.root();
    const QUrl base(WIKIDOT + QLatin1Char('/'));
    QHash<QString, QString> out;
    for (Node *a : root->findAll({QStringLiteral("a")})) {
        if (!a->attrs.contains(QStringLiteral("href")))
            continue;
        const QString text = squash(a->getText());
        const QString href = a->attr(QStringLiteral("href"));
        if (text.isEmpty() || href.startsWith(QLatin1Char('#')) || href.startsWith(QStringLiteral("javascript"))
            || href.startsWith(QStringLiteral("mailto")))
            continue;
        const QUrl url = base.resolved(QUrl(href));
        if (url.authority() == base.authority() && !out.contains(dkey(text)))
            out.insert(dkey(text), url.toString(QUrl::FullyEncoded));
    }
    return out;
}

// --------------------------------------------------------------------------- sprites
static QString spriteName(const Node *img)
{
    const Node *parent = img->findParent(QStringLiteral("a"));
    QString raw = img->attr(QStringLiteral("alt")).trimmed();
    if (raw.isEmpty() && parent)
        raw = parent->attr(QStringLiteral("title")).trimmed();
    if (raw.isEmpty()) {
        QString src = img->attr(QStringLiteral("src"));
        if (src.isEmpty())
            src = img->attr(QStringLiteral("data-src"));
        const QString path = pyUnquote(QUrl(src).path(QUrl::FullyEncoded));
        const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.contains(QStringLiteral("thumb")) && parts.size() >= 2)
            raw = parts.at(parts.size() - 2);
        else if (!parts.isEmpty())
            raw = parts.last();
    }
    static const QRegularExpression prefix = rx(QStringLiteral("^(File|Image):"), true);
    static const QRegularExpression ext = rx(QStringLiteral("\\.(png|gif|jpe?g)$"), true);
    static const QRegularExpression dq = rx(QStringLiteral("\\s*\\b(DQIX|DQ9)\\b\\s*(icon|sprite)?\\s*$"), true);
    static const QRegularExpression icon = rx(QStringLiteral("\\s+(icon|sprite)$"), true);
    raw = sub(raw, prefix, QString());
    raw = sub(raw, ext, QString()).replace(QLatin1Char('_'), QLatin1Char(' '));
    raw = sub(raw, dq, QString());
    raw = sub(raw, icon, QString());
    return raw.trimmed();
}

static QString fullImageUrl(const QString &src)
{
    static const QRegularExpression thumb = rx(QStringLiteral("^(.*/images)/thumb/([0-9a-f]/[0-9a-f]{2}/[^/]+)/[^/]+$"));
    const QString url = QUrl(SPRITE_URL).resolved(QUrl(src)).toString(QUrl::FullyEncoded);
    const auto m = thumb.match(url);
    return m.hasMatch() ? m.captured(1) + QLatin1Char('/') + m.captured(2) : url;
}

QHash<QString, QString> parseSprites(const QString &htmlText)
{
    Document doc(htmlText);
    const Node *root = doc.findById(QStringLiteral("mw-content-text"));
    if (!root)
        root = doc.findById(QStringLiteral("content"));
    if (!root)
        root = doc.root();
    static const QRegularExpression imgExt = rx(QStringLiteral("\\.(png|gif|jpe?g)$"), true);
    QHash<QString, QString> out;
    for (Node *img : root->findAll({QStringLiteral("img")})) {
        QString src = img->attr(QStringLiteral("data-src"));
        if (src.isEmpty())
            src = img->attr(QStringLiteral("src"));
        if (src.isEmpty() || src.startsWith(QStringLiteral("data:")))
            continue;
        const QString name = spriteName(img);
        if (!name.isEmpty() && contains(imgExt, QUrl(src).path()) && !out.contains(skey(name)))
            out.insert(skey(name), fullImageUrl(src));
    }
    return out;
}

// --------------------------------------------------------------------------- shop-only categories
// `hint` is a category name, or "@Weapon" / "@Armor" / "@Both" when all we know is which kind of
// shop sold it.
QString shopCategory(const QString &name, const QString &hint)
{
    if (!hint.isEmpty() && !hint.startsWith(QLatin1Char('@')))
        return hint;
    QStringList groups;
    if (hint == QStringLiteral("@Weapon"))
        groups = {"Weapon"};
    else if (hint == QStringLiteral("@Armor"))
        groups = {"Armor"};
    else
        groups = {"Weapon", "Armor"};
    for (int table = 0; table < 2; ++table) {
        for (const QString &g : groups) {
            const PatternList &pats = table == 0 ? subtypePatterns(g) : shopExtraPatterns(g);
            for (const auto &[cat, pattern] : pats)
                if (contains(rx(pattern, true), name))
                    return cat;
        }
    }
    static const QRegularExpression accessory = rx(QStringLiteral("\\bring\\b|earrings?|bracer\\b|bow tie|\\bbracelets?\\b"), true);
    if (contains(accessory, name))
        return QStringLiteral("Accessories");
    if (hint == QStringLiteral("@Weapon"))
        return otherLabel(QStringLiteral("Weapon"));
    if (hint == QStringLiteral("@Armor") || hint == QStringLiteral("@Both"))
        return otherLabel(QStringLiteral("Armor"));
    return ITEMS_CAT;
}

} // namespace dq9
