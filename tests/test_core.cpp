// Port of the Python test_parsers.py. No network and no display needed:  ./dq9_tests
#include "core/html.h"
#include "core/parsers.h"
#include "core/registry.h"
#include "core/render.h"
#include "core/robots.h"
#include "core/store.h"
#include "core/text.h"
#include "core/tree.h"
#include "core/wikitext.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cstdio>
#include <functional>
#include <map>

using namespace dq9;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                                                     \
    do {                                                                                                \
        ++g_checks;                                                                                     \
        if (!(cond)) {                                                                                  \
            ++g_failures;                                                                               \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                        \
        }                                                                                               \
    } while (0)

#define CHECK_EQ(a, b)                                                                                  \
    do {                                                                                                \
        ++g_checks;                                                                                     \
        const auto va = (a);                                                                            \
        const auto vb = (b);                                                                            \
        if (!(va == vb)) {                                                                              \
            ++g_failures;                                                                               \
            std::fprintf(stderr, "FAIL %s:%d: %s == %s\n    got:      %s\n    expected: %s\n", __FILE__, \
                         __LINE__, #a, #b, qPrintable(show(va)), qPrintable(show(vb)));                 \
        }                                                                                               \
    } while (0)

template <typename T> QString show(const T &v) { return QString::number(v); }
template <> QString show(const QString &v) { return QLatin1Char('"') + v + QLatin1Char('"'); }
template <> QString show(const QStringList &v) { return QLatin1Char('[') + v.join(QStringLiteral(" | ")) + QLatin1Char(']'); }
template <> QString show(const bool &v) { return v ? QStringLiteral("true") : QStringLiteral("false"); }
template <> QString show(const Ings &v)
{
    QStringList parts;
    for (const auto &i : v)
        parts << QStringLiteral("(%1, %2)").arg(i.name).arg(i.qty);
    return parts.join(QStringLiteral(", "));
}
template <> QString show(const std::optional<int> &v) { return v ? QString::number(*v) : QStringLiteral("None"); }
template <> QString show(const Pairs &v)
{
    QStringList parts;
    for (const auto &kv : v)
        parts << kv.first + QStringLiteral(": ") + kv.second;
    return QLatin1Char('{') + parts.join(QStringLiteral(", ")) + QLatin1Char('}');
}

template <typename T, typename F> QStringList mapNames(const QList<T> &l, F f)
{
    QStringList out;
    for (const auto &x : l)
        out << f(x);
    return out;
}

static Ings I(std::initializer_list<std::pair<const char *, int>> l)
{
    Ings out;
    for (const auto &p : l)
        out.append({QString::fromUtf8(p.first), p.second});
    return out;
}

static Recipe R(const QString &item, const QString &cat, const Ings &ings, const Pairs &extra = {}, const QString &source = {})
{
    Recipe r;
    r.item = item;
    r.category = cat;
    r.ingredients = ings;
    r.extra = extra;
    r.source = source;
    return r;
}

static Drop D(const QString &item, const QString &kind, int num, int den, const QString &title = {})
{
    Drop d;
    d.item = item;
    d.kind = kind;
    d.num = num;
    d.den = den;
    d.title = title;
    return d;
}

static EnemyPtr E(int number, const QString &name, const QString &url, QList<Drop> drops, const QString &gold = {},
                  const QString &exp = {})
{
    auto e = std::make_shared<Enemy>();
    e->number = number;
    e->name = name;
    e->url = url;
    e->gold = gold;
    e->exp = exp;
    e->drops = drops;
    return e;
}

static LocFn locOk(const QStringList &l)
{
    return [l](const EnemyPtr &) { return LocState{QStringLiteral("ok"), l}; };
}
static LocFn locLoading()
{
    return [](const EnemyPtr &) { return LocState{QStringLiteral("loading"), {}}; };
}

// A network that must never be used.
class NoHttp : public Http {
public:
    QString get(const QString &url) override { throw HttpError(0, QStringLiteral("offline test: ") + url); }
    QByteArray getBytes(const QString &url, double) override { throw HttpError(0, QStringLiteral("offline test: ") + url); }
};

static std::unique_ptr<QTemporaryDir> g_tmp;
static int g_tmpN = 0;
static std::unique_ptr<Store> freshStore()
{
    const QString d = g_tmp->filePath(QString::number(++g_tmpN));
    return std::make_unique<Store>(d, std::make_shared<NoHttp>());
}

// --------------------------------------------------------------------------- fixtures
static const QString IMG = QStringLiteral("https://dragon-quest.org/w/images/x.gif");

static QString loot(const QString &kind, const QString &name, const QString &title, const QString &rate)
{
    QString t = title;
    return QStringLiteral("<img alt=\"%1\" src=\"%2\"> <a href=\"/w/index.php?title=%3\" title=\"%4\">%5</a> &nbsp; %6 &nbsp; ")
        .arg(kind, IMG, t.replace(QLatin1Char(' '), QLatin1Char('_')), title, name, rate);
}

static QString enemyHtml()
{
    return QStringLiteral("<table class=\"wikitable\"><tr><th>Enemy</th><th>Gold</th><th>Experience</th><th>Loot and Rate</th></tr>"
                          "<tr><td>#001 <a href=\"/w/index.php?title=Slime\" title=\"Slime\">Slime</a></td><td>4 G</td><td>2</td>"
                          " <td>")
        + loot("Item", "Medicinal herb", "Medicinal herb", "1/8") + loot("Item", "Slimedrop", "Slimedrop", "1/16")
        + QStringLiteral("</td></tr><tr><td>#066 <a href=\"/w/index.php?title=Spinchilla\" title=\"Spinchilla\">Spinchilla</a></td><td>112 G</td><td>300</td> <td>")
        + loot("Item", "Seashell", "Sun-bleached seashell", "1/16") + loot("Item", "Crimson coral", "Crimson coral", QStringLiteral("1⁄2"))
        + QStringLiteral("</td></tr><tr><td>#012 <a href=\"/w/index.php?title=Winkster\" title=\"Winkster\">Winkster</a></td><td>25 G</td><td>22</td> <td>")
        + loot("Legs", "Cotton trousers", "Cotton trousers", "1/32") + loot("Wand", "Oak staff", "Oaken staff", "1/64")
        + QStringLiteral("</td></tr><tr><td>#175 <a href=\"/w/index.php?title=Dreadful_drackal\" title=\"x\">Dreadful drackal</a></td><td>180 G</td><td>2000</td> <td>")
        + loot("Item", "Magic beast hide", "Magic beast hide", "116/") + loot("Item", "Magic beast horn", "Magic beast horn", "1/32")
        + QStringLiteral("</td></tr><tr><td>#231 <a href=\"/w/index.php?title=Grim_reaper\">Grim reaper</a></td><td>165 G</td><td>5000</td> <td>")
        + loot("Arms", "Minister's mittens", "Priest costume", "1/64") + loot("Head", "Mitre", "Priest costume", "1/64")
        + QStringLiteral("</td></tr></table>");
}

static const char *LOC_TABLE = R"(<h2>Appearances</h2><h3>Dragon Quest VIII</h3><table><tr><th>Location(s)</th><td>Farebury Region</td></tr></table>
<h3><span>Dragon Quest IX: Sentinels of the Starry Skies</span></h3>
<table><tr><th>Experience</th><td>2</td></tr>
<tr><th>Location(s)</th><td><a href="/x">Angel Falls</a> region<br><a href="/y">The Hexagon</a><br><a href="/z">Bloomingdale (region)</a> region</td></tr>
<tr><th>Item(s) dropped</th><td>Medicinal herb 1/8</td></tr></table><h3>Dragon Quest XI</h3><p>Location(s) Heliodor</p>)";

static const char *LOC_DL = R"(<h3>Dragon Quest IX: Sentinels of the Starry Skies</h3><dl>
<dt>Location(s)</dt><dd><a>Angel Falls</a> region</dd><dd><a>The Hexagon</a></dd>
<dt>Item(s) dropped</dt><dd>Herb</dd></dl><h3>Next game</h3>)";

static const char *LOC_PLAIN = R"(<div class="mw-heading mw-heading3"><h3>Dragon Quest IX: Sentinels of the Starry Skies</h3></div>
<div>Location(s)<br><a>Angel Falls</a> region<br><a>The Hexagon</a><br>Item(s) dropped<br>Medicinal herb 1/8</div>
<div class="mw-heading mw-heading3"><h3>Dragon Quest XI</h3></div><div>Location(s)<br>Wrong place</div>)";

static const char *RECIPE_HTML = R"(<table><tr><th>Item</th><th>Type</th><th>Ingredients</th><th>Pot time</th></tr>
<tr><td>Copper sword</td><td>Sword</td><td>2x Iron ore<br>Slimedrop</td><td>10</td></tr>
<tr><td>Oaken staff</td><td>Staff</td><td>Sun-bleached seashell, 3 Crimson coral</td><td>5</td></tr>
<tr><td>Slime crown</td><td>Helmet</td><td>Slimedrop x4 + Mythril ore</td><td></td></tr></table>)";

static QString weaponWikitext()
{
    QFile f(QStringLiteral(":/tests/weapon_wikitext.txt"));
    if (f.open(QIODevice::ReadOnly))
        return QString::fromUtf8(f.readAll());
    return {};
}

static const char *SILVER_PLATTER_HTML = R"(<div id="page-content"><p>A salver made of silver that makes a stopgap shield.</p>
<p><strong>Classification:</strong> <a href="/shields">Shield</a><br/><strong>Defence:</strong> 6</p>
<p><strong>Rarity:</strong> 1/5<br/><strong>Buy price:</strong> Cannot Buy<br/><strong>Sell price:</strong> 100 G</p>
<p><strong>Where to find:</strong></p><ul><li><a href="/the-bad-cave">The Bad Cave</a>, The Plumbed Depths</li>
<li>Rank 2 (H) blue chest in grotto (2%)</li><li>Rank 4 overworld blue chest outside grotto (5%)</li></ul>
<p><strong>Reward for:</strong> 028 Desert Foxes (repeat)</p>
<p><strong>Alchemises:</strong></p><ul><li>Gold platter</li></ul></div>)";

static QString fieldValue(const Details &d, const QString &label)
{
    for (const auto &f : d.fields)
        if (f.first == label)
            return f.second;
    return {};
}

// --------------------------------------------------------------------------- tests
static void testBasics()
{
    const auto enemies = parseEnemyList(enemyHtml());
    CHECK_EQ(mapNames(enemies, [](const Enemy &e) { return e.name; }),
             (QStringList{"Slime", "Spinchilla", "Winkster", "Dreadful drackal", "Grim reaper"}));
    CHECK_EQ(enemies[0].url, QStringLiteral("https://dragon-quest.org/wiki/Slime"));
    CHECK(enemies[0].number == 1 && enemies[0].gold == QStringLiteral("4 G"));
    CHECK(enemies[0].drops[0].item == QStringLiteral("Medicinal herb") && enemies[0].drops[0].num == 1 && enemies[0].drops[0].den == 8);
    CHECK_EQ(enemies[1].drops[1].den, 2);                       // U+2044 fraction slash
    CHECK(enemies[3].drops[0].num == 1 && enemies[3].drops[0].den == 16);   // "116/" typo
    CHECK_EQ(enemies[2].drops[1].kind, QStringLiteral("Wand"));

    for (const char *page : {LOC_TABLE, LOC_DL, LOC_PLAIN}) {
        const QStringList locs = parseEnemyLocations(QString::fromUtf8(page));
        const QStringList want = {"Angel Falls region", "The Hexagon", "Bloomingdale (region) region"};
        CHECK(locs == want.mid(0, locs.size()) || locs.mid(0, 2) == want.mid(0, 2));
        CHECK(locs.size() >= 2);
        CHECK(!locs.contains("Farebury Region") && !locs.contains("Wrong place") && !locs.contains("Heliodor"));
        for (const auto &l : locs)
            CHECK(!l.toLower().contains("dropped") && !l.toLower().contains("herb"));
    }
    CHECK(parseEnemyLocations(QStringLiteral("<h3>Dragon Quest VIII</h3><p>nothing</p>")).isEmpty());

    const auto recipes = parseRecipes(QString::fromUtf8(RECIPE_HTML));
    CHECK_EQ(mapNames(recipes, [](const Recipe &r) { return r.item; }), (QStringList{"Copper sword", "Oaken staff", "Slime crown"}));
    CHECK_EQ(recipes[0].ingredients, I({{"Iron ore", 2}, {"Slimedrop", 1}}));
    CHECK_EQ(recipes[1].ingredients, I({{"Sun-bleached seashell", 1}, {"Crimson coral", 3}}));
    CHECK_EQ(recipes[2].ingredients, I({{"Slimedrop", 4}, {"Mythril ore", 1}}));
    CHECK_EQ(mapNames(recipes, [](const Recipe &r) { return r.category; }), (QStringList{"Swords", "Staves", "Head"}));
    CHECK_EQ(recipes[0].extra, (Pairs{{"Pot Time", "10"}}));

    QList<EnemyPtr> eps;
    for (const auto &e : enemies)
        eps << std::make_shared<Enemy>(e);
    Registry reg(eps, recipes);
    const ItemPtr staff = reg.resolve("Oaken staff");
    CHECK(staff && staff == reg.resolve("Oak staff") && !staff->recipes.isEmpty() && !staff->droppers.isEmpty());
    CHECK_EQ(reg.resolve("Sun-bleached seashell")->droppers[0].enemy->name, QStringLiteral("Spinchilla"));
    CHECK(reg.resolve("Mitre") != reg.resolve("Minister's mittens"));
    CHECK_EQ(staff->category, QStringLiteral("Staves"));
    CHECK_EQ(reg.resolve("Copper sword")->category, QStringLiteral("Swords"));
    const QStringList cats = reg.categories();
    CHECK(cats.indexOf("Swords") < cats.indexOf("Head") && cats.last() == ITEMS_CAT);

    auto html = renderItemHtml(reg, *reg.resolve("Copper sword"), 5, locOk({"Somewhere"}), {}).html;
    CHECK(html.contains("Alchemy") && html.contains("2 &times; Iron ore") && html.contains("No monster on the enemy list"));
    const auto r2 = renderItemHtml(reg, *reg.resolve("Slime crown"), 5, locLoading(), {});
    CHECK(r2.html.contains("Slimedrop"));
    CHECK(mapNames(r2.needed, [](const EnemyPtr &e) { return e->name; }).contains("Slime"));

    bool threw = false;
    try {
        parseRecipes(QStringLiteral("<html><body><p>nope</p></body></html>"));
    } catch (const ParseError &e) {
        threw = errorText(e).contains("No recipes");
    }
    CHECK(threw);
}

static void testSheet()
{
    CHECK_EQ(parseSheetIngredients("Boss shield \n Agate of evolution (1)\n Mighty armlet (1)"),
             I({{"Boss shield", 1}, {"Agate of evolution", 1}, {"Mighty armlet", 1}}));
    CHECK_EQ(parseSheetIngredients("Rusty sword (1)\n Glass frit (9)"), I({{"Rusty sword", 1}, {"Glass frit", 9}}));
    CHECK_EQ(parseSheetIngredients(QStringLiteral("Über war hammer (1)")), (Ings{{QStringLiteral("Über war hammer"), 1}}));
    CHECK(parseSheetIngredients("").isEmpty());

    CHECK((alphabeticalRuns({"Supernova sword", QStringLiteral("Über falcon blade"), "Warrior's sword"}) == QList<QList<int>>{{0, 1, 2}}));
    CHECK((alphabeticalRuns({"Warrior's sword", "Celestial spear", "Trident", "Assassin's dagger"}) == QList<QList<int>>{{0}, {1, 2}, {3}}));

    const SheetColumns cols = {
        {"Weapon", {{"Alpha sword", "Iron ore (1)"}, {"Beta blade", "Iron ore (2)"},
                    {"Aardvark spear", "Slimedrop (1)"}, {"Cat lance", "Slimedrop (2)"},
                    {"Ant dagger", "Wing of bat (1)"}, {"Fenrir fang", "Wing of bat (2)"},
                    {"Soul breaker", "Wing of bat (3)"},
                    {"Aaa thing", "Iron ore (3)"}, {"Bbb thing", "Iron ore (4)"}}},
        {"Armor", {{"Big shield", "Iron ore (1)"}, {"Psyche swiper", "Iron ore (2)"}, {"Apple hat", "Iron ore (3)"}}},
        {"Accessory", {{"Agility ring", "Gold ring (1)"}}},
        {"Item", {{"Gold bar", "Flintstone (3)"}}},
        {"Weird", {{"Thing", "Stuff (1)"}}},
    };
    std::map<QString, Recipe> rs;
    for (const auto &r : recipesFromColumns(cols))
        rs[r.item] = r;
    CHECK(rs["Alpha sword"].category == "Swords" && rs["Beta blade"].category == "Swords");
    CHECK_EQ(rs["Aardvark spear"].category, QStringLiteral("Spears"));
    CHECK(rs["Ant dagger"].category == "Knives" && rs["Fenrir fang"].category == "Knives" && rs["Soul breaker"].category == "Knives");
    CHECK(rs["Aaa thing"].category == "Other weapons" && rs["Bbb thing"].category == "Other weapons");
    CHECK(rs["Big shield"].category == "Shields" && rs["Psyche swiper"].category == "Shields");
    CHECK_EQ(rs["Apple hat"].category, QStringLiteral("Head"));
    CHECK(rs["Agility ring"].category == "Accessories" && rs["Gold bar"].category == ITEMS_CAT);
    CHECK(rs["Thing"].category == "Weird" && rs["Alpha sword"].source == "spreadsheet");

    const auto shipped = freshStore()->loadSheetRecipes();
    CHECK_EQ(int(shipped.size()), 445);
    QHash<QString, int> catCount;
    for (const auto &r : shipped)
        ++catCount[r.category];
    CHECK(catCount["Swords"] == 17 && catCount["Knives"] == 15 && catCount["Shields"] == 29 && catCount["Torso"] == 67);
    for (auto it = catCount.begin(); it != catCount.end(); ++it)
        CHECK(!it.key().startsWith("Other"));
    std::map<QString, Recipe> by;
    for (const auto &r : shipped)
        by[r.item] = r;
    const QList<QPair<QString, QString>> expect = {
        {"Fenrir fang", "Knives"}, {"Gladius", "Knives"}, {"Psyche swiper", "Shields"}, {"Gold platter", "Shields"},
        {"Ace of clubs", "Hammers"}, {"Halberd", "Spears"}, {"Raging ruby", "Accessories"}, {"Agate of evolution", ITEMS_CAT}};
    for (const auto &[name, cat] : expect)
        CHECK_EQ(by[name].category, cat);
    CHECK_EQ(by["Aurora blade"].ingredients, I({{"Cautery sword", 1}, {"Brighten rock", 3}, {"Holy water", 1}}));
    CHECK(by["Big boss shield"].ingredients[0] == (Ing{"Boss shield", 1}));
    for (const auto &r : shipped)
        for (const auto &i : r.ingredients)
            CHECK(i.name != QStringLiteral("Agressence"));
}

static void testMergeAndNesting()
{
    QList<EnemyPtr> enemies = {
        E(1, "Riptide", "u1", {D("Dragon's scale", "Item", 1, 32, "Dragon's scale")}),
        E(2, "Axolhotl", "u2", {D("Cautery sword", "Sword", 1, 64, "Cautery sword"), D("Skull helm", "Spear", 1, 8, "Skull helm")}),
        E(3, "Goodybag", "u3", {D("Brighten rock", "Item", 1, 16, "Brighten rock")}),
    };
    QList<Recipe> woodus = {R("Slime crown", "Head", I({{"Dragon's scale", 3}, {"Mythril ore", 1}}), {{"Pot time", "5"}}),
                            R("Axe thing", "Axes", I({{"Iron ore", 1}}))};
    QList<Recipe> sheet = {
        R("Slime crown", "", I({{"Dragon scale", 3}, {"Mythril ore", 1}}), {}, "spreadsheet"),
        R("Axe thing", "Swords", I({{"Rock", 2}}), {}, "spreadsheet"),
        R("Aurora blade", "Swords", I({{"Cautery sword", 1}, {"Brighten rock", 3}}), {}, "spreadsheet"),
        R("Skull helm", "Head", I({{"Iron ore", 1}}), {}, "spreadsheet"),
        R("Big crown", "Head", I({{"Slime crown", 1}, {"Gold bar", 2}}), {}, "spreadsheet"),
        R("Gold bar", ITEMS_CAT, I({{"Aurora blade", 1}}), {}, "spreadsheet"),
        R("Alpha", ITEMS_CAT, I({{"Beta", 1}}), {}, "spreadsheet"),
        R("Beta", ITEMS_CAT, I({{"Alpha", 1}}), {}, "spreadsheet"),
    };
    for (int i = 0; i < 12; ++i)
        sheet << R(QStringLiteral("Link%1").arg(i), ITEMS_CAT, {{QStringLiteral("Link%1").arg(i + 1), 1}}, {}, "spreadsheet");
    Registry reg(enemies, woodus + sheet);

    const ItemPtr crown = reg.resolve("Slime crown");
    CHECK(crown->recipes.size() == 1 && crown->recipes[0].source == "spreadsheet, woodus");
    CHECK(crown->recipes[0].extra == (Pairs{{"Pot time", "5"}}) && crown->category == "Head");
    const ItemPtr axe = reg.resolve("Axe thing");
    CHECK(axe->recipes.size() == 2 && axe->category == "Axes");
    Registry reg2(enemies, sheet + woodus);
    CHECK_EQ(reg2.resolve("Axe thing")->category, QStringLiteral("Axes"));
    CHECK_EQ(reg.resolve("Skull helm")->category, QStringLiteral("Head"));
    CHECK_EQ(reg.recipeCount(), int(woodus.size() + sheet.size()) - 1);

    auto r = renderItemHtml(reg, *reg.resolve("Aurora blade"), 5, locOk({"Somewhere"}), {});
    CHECK(r.html.contains("Axolhotl") && r.html.contains("Goodybag") && r.html.contains("Source: spreadsheet"));
    QStringList needed = mapNames(r.needed, [](const EnemyPtr &e) { return e->name; });
    needed.removeDuplicates();
    needed.sort();
    CHECK_EQ(needed, (QStringList{"Axolhotl", "Goodybag"}));
    auto html = renderItemHtml(reg, *reg.resolve("Big crown"), 5, locOk({"X"}), {}).html;
    CHECK(html.contains("Can be made by alchemy") && html.contains("<blockquote>"));
    CHECK(html.count("Riptide") >= 1 && html.contains("No monster on the enemy list"));
    html = renderItemHtml(reg, *reg.resolve("Alpha"), 5, locOk({"X"}), {}).html;
    CHECK(html.contains("loops back"));
    html = renderItemHtml(reg, *reg.resolve("Link0"), 5, locOk({"X"}), {}).html;
    CHECK(html.contains("Select it in the list") && html.count("<blockquote>") == MAX_NEST);
}

static void testExactNames()
{
    QList<EnemyPtr> enemies = {E(1, "Wyrtle", "u1", {D("Tortoiseshell", "Item", 1, 8, "Tortoiseshell")}),
                               E(2, "Spinchilla", "u2", {D("Safety shoes", "Feet", 1, 64, "Safety shoes")})};
    QList<Recipe> recipes = {
        R("Tortoise shell", "Torso", I({{"Tortoiseshell", 5}, {"Iron ore", 1}}), {}, "spreadsheet"),
        R("Tortoiseshell fan", "Fans", I({{"Steel fan", 1}, {"Tortoiseshell", 3}}), {}, "spreadsheet"),
        R("Safest shoes", "Feet", I({{"Safer shoes", 1}}), {}, "spreadsheet"),
        R("Razer-wing boomerang", "Boomerangs", I({{"Razor-wing boomerang", 1}}), {}, "spreadsheet"),
        R("Erazor-wing boomerang", "Boomerangs", I({{"Razer-wing boomerang", 1}}), {}, "spreadsheet"),
        R("Hotshot bow", "Bows", I({{"Potshot bow", 1}}), {}, "spreadsheet"),
    };
    Registry reg(enemies, recipes);
    CHECK(norm("Dragon's scale") == norm("Dragon scale") && norm("Dragon scale") == norm("DRAGON  SCALE"));
    CHECK(norm("Safest shoes") != norm("Safety shoes"));

    const ItemPtr ing = reg.resolve("Tortoiseshell");
    CHECK(ing->category == ITEMS_CAT && !ing->droppers.isEmpty() && ing->recipes.isEmpty());
    const ItemPtr armour = reg.item("tortoiseshell~equip");
    CHECK(armour && armour->name == "Tortoise shell" && armour->category == "Torso" && !armour->recipes.isEmpty());
    CHECK(mapNames(reg.itemsIn("Torso"), [](const ItemPtr &i) { return i->name; }).contains("Tortoise shell"));
    CHECK(mapNames(reg.itemsIn(ITEMS_CAT), [](const ItemPtr &i) { return i->name; }).contains("Tortoiseshell"));
    auto html = renderItemHtml(reg, *reg.resolve("Tortoiseshell fan"), 5, locOk({"X"}), {}).html;
    CHECK(html.contains("Wyrtle") && !html.contains("Can be made by alchemy"));

    CHECK(reg.resolve("Safest shoes") != reg.resolve("Safety shoes"));
    CHECK(!reg.resolve("Safety shoes")->droppers.isEmpty() && reg.resolve("Safest shoes")->droppers.isEmpty());
    CHECK(reg.resolve("Razer-wing boomerang") != reg.resolve("Erazor-wing boomerang"));
    CHECK(reg.resolve("Razor-wing boomerang") == nullptr);
    CHECK(reg.resolve("Hotshot bow") != nullptr && reg.resolve("Potshot bow") == nullptr);
    html = renderItemHtml(reg, *reg.resolve("Razer-wing boomerang"), 5, locOk({"X"}), {}).html;
    CHECK(html.contains("No monster on the enemy list"));
}

static void testShops()
{
    auto st = freshStore();
    const auto shops = st->shops();
    const auto items = st->shopItems();
    CHECK_EQ(int(shops.size()), 362);
    CHECK_EQ(int(items.size()), 362);
    CHECK_EQ(shops.value("coppersword"), (QStringList{"Angel Falls (Item Shop)", "Stornway (Weapon Shops)"}));
    CHECK(!shops.contains("tortoiseshell") && shops.contains("tortoiseshell~equip"));
    CHECK_EQ(shopCategory("Pot Lid"), QStringLiteral("Shields"));
    CHECK_EQ(shopCategory("Carrying pole", "@Weapon"), QStringLiteral("Staves"));
    CHECK_EQ(shopCategory("Hardwood headwear", "@Armor"), QStringLiteral("Head"));
    CHECK_EQ(shopCategory("Gold ring"), QStringLiteral("Accessories"));
    CHECK_EQ(shopCategory("Medicinal herb"), ITEMS_CAT);
    CHECK_EQ(shopCategory("Zork", "@Weapon"), QStringLiteral("Other weapons"));
    CHECK_EQ(shopCategory("White Bouquet", "Fans"), QStringLiteral("Fans"));
    Registry reg({}, st->loadSheetRecipes(), items);
    CHECK_EQ(reg.recipeCount(), 445);
    CHECK_EQ(reg.resolve("Copper sword")->category, QStringLiteral("Swords"));
    auto html = renderItemHtml(reg, *reg.resolve("Copper sword"), 5, locLoading(), shops).html;
    CHECK(html.contains("Stornway (Weapon Shops)"));
    html = renderItemHtml(reg, *reg.resolve("Dragon slayer"), 5, locLoading(), shops).html;
    CHECK(html.contains("Buy:"));
}

static void testItemPages()
{
    const QString page = QStringLiteral(
        "<html><body><div id='page-content'><p>Aurora blade</p>"
        "<p>A wondrous weapon that flings flames if used as an item in battle.</p>"
        "<p><b>Classification</b>: Sword<br/><b>Used by</b>: <a href='/warrior'>Warrior</a>, "
        "<a href='/thief'>Thief</a></p><p><b>Attack</b>: 70<br/><b>Style</b>: 38<br/>"
        "<b>Special</b>: <i>Use:</i> Deals 6-10 Fire damage to all enemies</p>"
        "<p><b>Buy price</b>: Cannot Buy<br/><b>Sell price</b>: 4,700 G</p>"
        "<p><b>How to make</b>: <a href='/x'>Cautery sword</a> x1</p></div></body></html>");
    const auto det = parseItemPage(page, "Aurora blade");
    CHECK(det.has_value());
    if (!det)
        return;
    CHECK(det->description.startsWith("A wondrous weapon"));
    CHECK_EQ(fieldValue(*det, "Classification"), QStringLiteral("Sword"));
    CHECK_EQ(fieldValue(*det, "Used by"), QStringLiteral("Warrior, Thief"));
    CHECK_EQ(fieldValue(*det, "Special"), QStringLiteral("Use: Deals 6-10 Fire damage to all enemies"));
    CHECK(fieldValue(*det, "Sell price") == "4,700 G" && fieldValue(*det, "Attack") == "70");
    CHECK(!parseItemPage("<div id='page-content'><p>nothing</p></div>", "x").has_value());
    const auto t = parseItemPage("<div id='page-content'><p>Desc.</p><table><tr><td>Sell price</td>"
                                 "<td>10 G</td></tr></table></div>", "x");
    CHECK(t && fieldValue(*t, "Sell price") == "10 G");
    const auto idx = parseWikiIndex("<div id='page-content'><a href='/aurora-blade'>Aurora blade</a>"
                                    "<a href='http://other.com/x'>Other</a><a href='#top'>Top</a></div>");
    CHECK(idx.size() == 1 && idx.value("aurora blade") == "https://dqnine.wikidot.com/aurora-blade");
    CHECK_EQ(wikidotSlug("Warrior's sword"), QStringLiteral("warrior-s-sword"));
    CHECK_EQ(wikidotSlug(QStringLiteral("Über falcon blade")), QStringLiteral("uber-falcon-blade"));
    Registry reg({}, {R("Aurora blade", "Swords", I({{"Holy water", 1}}))});
    auto html = renderItemHtml(reg, *reg.resolve("Aurora blade"), 5, locLoading(), {},
                               [&](const QString &) { return DetailState{"ok", det}; }).html;
    CHECK(html.contains("4,700 G") && html.contains("flings flames"));
    html = renderItemHtml(reg, *reg.resolve("Aurora blade"), 5, locLoading(), {},
                          [](const QString &) { return DetailState{"loading", std::nullopt}; }).html;
    CHECK(html.contains("Loading item details"));
}

static void testTree()
{
    auto rec = [](const QString &item, const Ings &ing) { return R(item, "Swords", ing); };
    Registry reg({}, {rec("A", I({{"B", 2}, {"C", 1}})), rec("B", I({{"A", 1}, {"D", 1}})), R("A", "Swords", I({{"E", 1}}))});
    auto root = buildTree(reg, *reg.resolve("A"), 5, {{"d", {"Zere (Item Shop)"}}});
    CHECK_EQ(mapNames(root->children, [](const NodePtr &n) { return n->kind; }), (QStringList{"recipe", "recipe"}));
    const NodePtr b = root->children[0]->children[0];
    CHECK(b->label == "B" && b->edge == "x2" && b->kind == "craft");
    CHECK_EQ(mapNames(b->children, [](const NodePtr &n) { return n->label; }), (QStringList{"A", "D"}));
    CHECK(b->children[0]->sub == "(loops back)" && b->children[0]->children.isEmpty());
    CHECK_EQ(b->children[1]->kind, QStringLiteral("shop"));
    const auto [w, h] = layout(root);
    const auto nodes = walk(root);
    QList<double> leaves;
    for (const auto &n : nodes)
        if (n->children.isEmpty())
            leaves << n->y;
    std::sort(leaves.begin(), leaves.end());
    for (int i = 1; i < leaves.size(); ++i)
        CHECK(leaves[i] - leaves[i - 1] >= NODE_H);
    for (const auto &n : nodes) {
        for (const auto &c : n->children)
            CHECK(c->x > n->x);
        if (!n->children.isEmpty())
            CHECK(n->children.first()->y <= n->y && n->y <= n->children.last()->y);
    }
    CHECK(w > 0 && h > 0);

    auto st = freshStore();
    Registry real({}, st->loadSheetRecipes(), st->shopItems());
    root = buildTree(real, *real.resolve("Dragon slayer"), 5, st->shops());
    QStringList labels = mapNames(walk(root), [](const NodePtr &n) { return n->label; });
    for (const char *want : {"Mighty armlet", "Strength ring", "Aggressence", "Medicinal herb"})
        CHECK(labels.contains(QString::fromUtf8(want)));
    auto r = renderEnemyHtml(E(1, "Slime", "u", {D("Bandana", "Head", 1, 16)}, "1", "2"), locOk({"Zere"}));
    CHECK(r.html.contains("Slime") && r.html.contains("Zere") && r.html.contains("1/16"));
}

static void testPrefer()
{
    QList<EnemyPtr> slime = {E(1, "Slime", "u", {D("Zap", "Sword", 1, 8), D("Herb", "Item", 1, 4)})};
    Registry reg(slime, {R("Zap", "Swords", I({{"Herb", 2}}))});
    auto a = buildTree(reg, *reg.resolve("Zap"), 5, {}, "alchemy");
    CHECK(a->children.size() == 1 && a->children[0]->kind == "base" && a->children[0]->edge == "x2");
    CHECK(a->children[0]->children.size() == 1 && a->children[0]->children[0]->kind == "monster");
    auto m = buildTree(reg, *reg.resolve("Zap"), 5, {}, "drops");
    CHECK(m->children.size() == 1 && m->children[0]->kind == "monster" && m->children[0]->label == "Slime");
    Registry two({E(1, "A", "u", {D("Herb", "Item", 1, 4)}), E(2, "B", "u", {D("Herb", "Item", 1, 8)})}, {});
    auto one = buildTree(two, *two.resolve("Herb"), 1, {});
    CHECK(one->children.size() == 1 && one->children[0]->label == "A" && one->children[0]->more == 1);
    auto both = buildTree(two, *two.resolve("Herb"), 2, {});
    CHECK(both->children.size() == 2 && both->children[0]->more == 0 && both->children[1]->more == 0);
    Registry onlyR({}, {R("Zap", "Swords", I({{"Herb", 1}}))});
    CHECK_EQ(buildTree(onlyR, *onlyR.resolve("Zap"), 5, {}, "drops")->children[0]->label, QStringLiteral("Herb"));
}

static void testSprites()
{
    const QString page = QStringLiteral(
        "<div id='mw-content-text'><table><tr><td><a href='/w/File:x' title='File:Aggressence DQIX icon.png'>"
        "<img alt='Aggressence DQIX icon.png' src='/w/images/thumb/d/dc/Aggressence_DQIX_icon.png/32px-Aggressence_DQIX_icon.png'/></a></td>"
        "<td><a href='/w/Aggressence'>Aggressence</a></td>"
        "<td><a title='Strength ring'><img src='/w/images/5/58/Strength_ring_DQIX_icon.png'/></a>"
        "<img alt='Über Gringham whip' src='/w/images/a/ab/Uber_Gringham_whip_DQIX_icon.png'/>"
        "<img src='data:image/gif;base64,AAAA' alt='skip'/>"
        "<img alt='Aggressence' src='/w/images/zz/zz/Other.png'/></td></tr></table></div>");
    const auto sp = parseSprites(page);
    CHECK_EQ(sp.value("aggressence"), QStringLiteral("https://dragon-quest.org/w/images/d/dc/Aggressence_DQIX_icon.png"));
    CHECK(sp.value("strength ring").endsWith("/5/58/Strength_ring_DQIX_icon.png"));
    CHECK(sp.contains(QStringLiteral("über gringham whip")) && !sp.contains("skip"));
    auto st = freshStore();
    st->setSpriteIndex({{"tortoiseshell", "u1"}, {"tortoise shell", "u2"}, {"dragon's scale", "u3"}});
    CHECK(st->hasSprite("Tortoiseshell") && st->hasSprite(QStringLiteral("Dragon’s scale")));
    CHECK(!st->hasSprite("Mystery item"));
    Registry reg({}, {R("A", "Swords", I({{"B", 1}}))});
    CHECK_EQ(buildTree(reg, *reg.resolve("A"), 1, {})->sprite, QStringLiteral("A"));
}

class FakeWeaponHttp : public NoHttp {
public:
    QStringList calls;
    QString wikitext;
    QString get(const QString &url) override
    {
        calls << url;
        const QJsonObject o{{"parse", QJsonObject{{"title", "x"}, {"wikitext", wikitext}}}};
        return QString::fromUtf8(QJsonDocument(o).toJson());
    }
};

static void testWeapons()
{
    const QString wikitext = weaponWikitext();
    CHECK(!wikitext.isEmpty());
    const auto parsed = parseWeaponWikitext(wikitext);
    QList<WeaponPtr> ws;
    std::map<QString, WeaponPtr> w;
    for (const auto &x : parsed) {
        auto p = std::make_shared<Weapon>(x);
        ws << p;
        w[x.name] = p;
    }
    CHECK_EQ(int(ws.size()), 19);
    const auto &iron = *w["Iron axe"];
    CHECK(iron.category == "Axes" && iron.attack == "30" && iron.buy == 1800 && iron.sell == 900);
    CHECK(iron.towns == QStringList{"Porth Llaffan"} && iron.found.isEmpty() && iron.recipe.isEmpty());
    CHECK_EQ(iron.equipNames(), (QStringList{"Gladiator", "Ranger"}));
    CHECK_EQ(iron.spriteUrl(), QStringLiteral("https://dragon-quest.org/w/images/b/b4/IronAxe.png"));
    const auto &steel = *w["Steel axe"];
    CHECK_EQ(steel.recipe, I({{"Iron axe", 1}, {"Iron ore", 1}, {"Lava lump", 1}}));
    CHECK(steel.towns.isEmpty() && !steel.buy && steel.sell == 2100 && steel.found.isEmpty());
    CHECK_EQ(w["Kaiser axe"]->recipe, I({{"King axe", 1}, {"Slime crown", 1}, {"Monarchic mark", 1}}));
    const auto &pro = *w["Pro's axe"];
    CHECK(pro.found == QStringList{"Quest #79 reward"} && pro.drops == (QList<MonsterMark>{{"Trauminator", "R"}}));
    CHECK_EQ(pro.notes, QStringLiteral("20% bonus damage to plants."));
    const auto &galaxy = *w["Galaxy axe"];
    CHECK(galaxy.recipe.isEmpty() && galaxy.recipeNote.contains("alchemize") && galaxy.found.isEmpty());
    const auto &boom = *w["Boomerang"];
    CHECK(boom.category == "Boomerangs" && boom.towns == (QStringList{"Stornway", "Coffinwell"}));
    CHECK_EQ(w["Reinforced boomerang"]->recipe, I({{"Boomerang", 1}, {"Iron nail", 3}}));
    const auto &longbow = *w["Longbow"];
    CHECK_EQ(longbow.image, QStringLiteral("Long bow IX artwork.png"));
    CHECK_EQ(longbow.spriteUrl(), QStringLiteral("https://dragon-quest.org/w/images/d/d1/Long_bow_IX_artwork.png"));
    CHECK(w["Sacred claws"]->drops == (QList<MonsterMark>{{"Scarewolf", "R"}, {"Claws", "N"}}));
    CHECK(w["Falcon knife earrings"]->sell == 7000 && !w["Falcon knife earrings"]->buy);
    const auto &dread = *w["Dread dagger"];
    CHECK(dread.recipe.last() == (Ing{"Butterfly wing", 1}) && dread.recipeNote.contains("quantity"));
    CHECK(w["Sword breaker"]->buy == 15500 && w["Sword breaker"]->sell == 7750);
    const auto &pillar = *w["Pillar of strength"];
    CHECK(pillar.buy == 3400 && pillar.sell == 1700 && pillar.equip == (QStringList{"MA", "PA"}) && pillar.category == "Staves");
    const auto &star = *w["Starsmasher"];
    CHECK(star.recipe.size() == 3 && star.recipeNote == "Alchemiracle" && star.category == "Hammers");
    const auto &moon = *w["Moonmasher"];
    CHECK(moon.recipe.isEmpty() && moon.found.isEmpty() && moon.towns.isEmpty());
    const auto &copper = *w["Copper sword"];
    CHECK(copper.buy == 150 && copper.sell == 15 && copper.towns == (QStringList{"Angel Falls", "Stornway"}));
    const auto &wsw = *w["Warrior's sword"];
    CHECK(!wsw.buy && wsw.sell == 3700 && wsw.priceNote == "Quest 363 reward");
    CHECK(w["Miracle sword"]->priceNote == "32 Mini Medals" && w["Miracle sword"]->sell == 9250);
    const auto &gitt = *w["Scepter of Gitt"];
    CHECK(gitt.category == "Wands" && !gitt.buy && !gitt.sell && gitt.drops == (QList<MonsterMark>{{"King Godwyn", "1"}}));
    bool threw = false;
    try {
        parseWeaponWikitext("{|\n! A !! B\n|-\n| x || y\n|}");
    } catch (const ParseError &) {
        threw = true;
    }
    CHECK(threw);

    // merged into the registry
    auto scarewolf = E(9, "Scarewolf", "u", {D("Fur hood", "Head", 1, 64)});
    auto trauminator = E(10, "Trauminator", "u", {D("Pro's axe", "Axe", 1, 128)});
    QList<Recipe> sheet = {R("Dread dagger", "Knives", I({{"Batterfly knife", 1}, {"Disturbin turban", 1}, {"Butterfly wing", 2}}), {}, "spreadsheet"),
                           R("Uber war hammer", "Hammers", I({{"War hammer", 1}}), {}, "spreadsheet")};
    Registry reg({scarewolf, trauminator}, sheet, {}, ws);
    const ItemPtr proI = reg.resolve("Pro's axe");
    CHECK(proI->weapon == w["Pro's axe"] && proI->category == "Axes");
    CHECK(proI->droppers.size() == 1 && proI->droppers[0].enemy->name == "Trauminator");
    CHECK_EQ(proI->found, (QStringList{"Quest #79 reward"}));
    const ItemPtr sac = reg.resolve("Sacred claws");
    CHECK(sac->droppers.size() == 1 && sac->droppers[0].enemy->name == "Scarewolf" && sac->droppers[0].drop.oddsText() == "rare drop");
    CHECK_EQ(sac->found, (QStringList{"Dropped by Claws (common drop)"}));
    CHECK(reg.resolve("Galaxy axe")->found.value(0).startsWith("20% chance to alchemize"));
    const ItemPtr dd = reg.resolve("Dread dagger");
    CHECK(dd->recipes.size() == 1 && dd->recipes[0].ingredients.last() == (Ing{"Butterfly wing", 2}));
    const ItemPtr steelI = reg.resolve("Steel axe");
    CHECK(steelI->recipes[0].source == "weapon list" && steelI->category == "Axes");
    CHECK_EQ(reg.resolve("Starsmasher")->recipes[0].extra, (Pairs{{"Note", "Alchemiracle"}}));
    CHECK(reg.resolve("Moonmasher")->category == "Hammers" && reg.resolve("Moonmasher")->found.isEmpty());
    CHECK(reg.resolve(QStringLiteral("Über war hammer")) == reg.resolve("Uber war hammer"));
    CHECK(mapNames(reg.itemsIn("Bows"), [](const ItemPtr &i) { return i->name; }).contains("Longbow"));

    // shops: weapon-list towns join the bundled shop list without doubling a town
    auto st = freshStore();
    st->weapons = ws;
    const auto shops = st->shops();
    CHECK_EQ(shops.value("ironaxe"), (QStringList{"Porth Llaffan (Weapon/Armour Shop)"}));
    CHECK(shops.value("swordbreaker") == QStringList{"Upover (Weapon Shops)"} || shops.value("swordbreaker").value(0).contains("Upover"));
    CHECK_EQ(shops.value("coppersword"), (QStringList{"Angel Falls (Item Shop)", "Stornway (Weapon Shops)"}));
    CHECK(!shops.contains("steelaxe") && !shops.contains("warriorsword"));

    auto root = buildTree(reg, *steelI, 3, shops);
    CHECK_EQ(mapNames(root->children, [](const NodePtr &n) { return n->label; }), (QStringList{"Iron axe", "Iron ore", "Lava lump"}));
    CHECK(!root->children[0]->children.isEmpty() && root->children[0]->children[0]->kind == "shop");
    root = buildTree(reg, *reg.resolve("Pro's axe"), 3, shops);
    CHECK(mapNames(root->children, [](const NodePtr &n) { return n->kind; }) == (QStringList{"monster", "found"})
          && root->children[1]->sub == "Quest #79 reward");
    root = buildTree(reg, *reg.resolve("Galaxy axe"), 3, shops);
    CHECK_EQ(mapNames(root->children, [](const NodePtr &n) { return n->kind; }), (QStringList{"found"}));
    CHECK(buildTree(reg, *reg.resolve("Moonmasher"), 3, shops)->sub.endsWith("no known source"));
    const auto html = renderItemHtml(reg, *proI, 5, locOk({}), shops).html;
    CHECK(html.contains("Gladiator, Ranger") && html.contains("9,900 G") && html.contains("Quest #79 reward"));
    CHECK(!html.contains("N/A") && !html.contains("Buy price"));

    // sprites: weapon pictures join the icon index; the ingredient page's icons keep priority
    st->setSpriteIndex({{"iron axe", "icon-url"}});
    st->registerSprites(weaponSprites(ws));
    CHECK_EQ(st->spriteIndex().value("iron axe"), QStringLiteral("icon-url"));
    CHECK(st->hasSprite("Longbow") && st->hasSprite(QStringLiteral("Pro’s axe")));
    CHECK(st->spriteIndex().value("longbow").endsWith("/d/d1/Long_bow_IX_artwork.png"));

    // loading: cached wiki source is reused; the API answer is unwrapped
    auto fake = std::make_shared<FakeWeaponHttp>();
    fake->wikitext = wikitext;
    st->setHttp(fake);
    CHECK(st->loadWeapons().size() == 19 && fake->calls.size() == 1 && fake->calls[0].contains("api.php"));
    CHECK(st->loadWeapons().size() == 19 && fake->calls.size() == 1);
}

static void testUnsourcedItems()
{
    const auto det = parseItemPage(QString::fromUtf8(SILVER_PLATTER_HTML), "Silver platter");
    const QStringList found = detailsFound(det);
    CHECK_EQ(found, (QStringList{"The Bad Cave, The Plumbed Depths", "Rank 2 (H) blue chest in grotto (2%)",
                                 "Rank 4 overworld blue chest outside grotto (5%)", "Quest reward: 028 Desert Foxes (repeat)"}));
    Details na;
    na.fields = {{"Where to find", "N/A"}, {"Sell price", "5 G"}};
    CHECK(detailsFound(na).isEmpty());
    CHECK(detailsFound(std::nullopt).isEmpty());

    Registry reg({}, {R("Gold platter", "Shields", I({{"Gold bracer", 1}, {"Silver platter", 1}})),
                      R("Gold bracer", "Accessories", I({{"Gold bar", 1}}))},
                 {{"Gold bar", ""}});
    const Shops shops = {{"goldbar", {"Somewhere (Item Shop)"}}};
    QStringList asked;
    auto foundFn = [&](const QString &name) {
        asked << name;
        return name == "Silver platter" ? found : QStringList();
    };
    auto root = buildTree(reg, *reg.resolve("Gold platter"), 3, shops, "alchemy", foundFn);
    const NodePtr silver = root->children[1];
    CHECK(silver->label == "Silver platter" && silver->kind == "base");
    CHECK(!silver->children.isEmpty() && silver->children[0]->kind == "found" && silver->children[0]->label == "Found (4 ways)");
    CHECK(!asked.contains("Gold bracer") && !asked.contains("Gold bar"));
    CHECK(asked.count("Silver platter") == 1 && asked.contains("Gold platter"));
    root = buildTree(reg, *reg.resolve("Gold platter"), 3, shops, "alchemy", [](const QString &) { return QStringList(); });
    CHECK_EQ(root->children[1]->kind, QStringLiteral("unknown"));

    auto detailFn = [&](const QString &n) {
        return dkey(n) == "silver platter" ? DetailState{"ok", det} : DetailState{"loading", std::nullopt};
    };
    auto html = renderItemHtml(reg, *reg.resolve("Gold platter"), 5, locOk({}), shops, detailFn).html;
    CHECK(html.contains("Rank 2 (H) blue chest") && !html.section("Silver platter", -1).contains("Looking up"));
    html = renderItemHtml(reg, *reg.resolve("Gold platter"), 5, locOk({}), shops,
                          [](const QString &) { return DetailState{"loading", std::nullopt}; }).html;
    CHECK(html.contains("Looking up where to find it"));

    Registry plural({E(1, "Hammerhood", "u", {D("Iron nail", "Item", 1, 8)})},
                    {R("Spiked boomerang", "Boomerangs", I({{"Iron nails", 3}}))});
    CHECK(plural.resolve("Iron nails") == plural.resolve("Iron nail") && !plural.resolve("Iron nails")->droppers.isEmpty());
    CHECK(plural.resolve("Spiked boomerangs") == plural.resolve("Spiked boomerang"));
}

static void testMaterialList()
{
    Registry reg({E(1, "Slime", "u", {D("Slimedrop", "Item", 1, 8), D("Sword 3", "Sword", 1, 64)})},
                 {R("Sword 1", "Swords", I({{"Sword 2", 1}, {"Slimedrop", 5}})),
                  R("Sword 2", "Swords", I({{"Slimedrop", 4}})),
                  R("Sword 1", "Swords", I({{"Sword 3", 2}, {"Iron ore", 1}})),
                  R("Sword 3", "Swords", I({{"Sword 2", 1}, {"Iron ore", 2}})),
                  R("Loop A", "Swords", I({{"Loop B", 1}})), R("Loop B", "Swords", I({{"Loop A", 1}, {"Slimedrop", 1}}))},
                 {{"Iron ore", ""}});
    const Shops shops = {{"ironore", {"Zere (Item Shop)", "Stornway (Item Shop)"}}};
    const ItemPtr s1 = reg.resolve("Sword 1"), s2 = reg.resolve("Sword 2");
    auto rows = [](const QList<ListRow> &l) {
        return mapNames(l, [](const ListRow &r) { return QStringLiteral("%1=%2").arg(r.name).arg(r.qty); });
    };

    auto ml = materialList(reg, *s1, shops);
    CHECK_EQ(rows(ml.materials), (QStringList{"Slimedrop=9"}));
    CHECK(rows(ml.crafted) == QStringList{"Sword 2=1"} && ml.crafted[0].expanded);
    CHECK_EQ(ml.materials[0].how, QStringLiteral("Drop: Slime 1/8 (12.50%)"));
    ml = materialList(reg, *s1, shops, "alchemy", {{s2->key, false}});
    CHECK_EQ(rows(ml.materials), (QStringList{"Sword 2=1", "Slimedrop=5"}));
    CHECK(ml.materials[0].craftable && !ml.materials[0].expanded && !ml.materials[1].craftable);
    CHECK(ml.crafted.isEmpty());
    ml = materialList(reg, *s1, shops, "alchemy", {}, 3);
    CHECK(ml.materials[0].qty == 27 && ml.crafted[0].qty == 3);
    ml = materialList(reg, *s1, shops, "alchemy", {}, 1, 1);
    CHECK_EQ(rows(ml.materials), (QStringList{"Slimedrop=8", "Iron ore=5"}));
    CHECK_EQ(rows(ml.crafted), (QStringList{"Sword 2=2", "Sword 3=2"}));
    ml = materialList(reg, *s1, shops, "drops", {}, 1, 1);
    CHECK_EQ(rows(ml.materials), (QStringList{"Sword 3=2", "Iron ore=1"}));
    const QString sw3 = reg.resolve("Sword 3")->key;
    ml = materialList(reg, *s1, shops, "drops", {{sw3, true}}, 1, 1);
    CHECK(rows(ml.materials).contains("Iron ore=5"));
    for (const auto &r : ml.materials)
        if (r.name == "Iron ore")
            CHECK(r.how.contains("Buy: Zere (Item Shop) (+1 more)"));
    ml = materialList(reg, *reg.resolve("Loop A"), shops);
    bool sawLoop = false;
    for (const auto &r : ml.materials)
        if (r.name == "Loop A") {
            sawLoop = r.loop && r.qty == 1;
        }
    CHECK(sawLoop && !ml.notes.isEmpty());
    ml = materialList(reg, *reg.resolve("Slimedrop"), shops);
    CHECK(rows(ml.materials) == QStringList{"Slimedrop=1"} && !ml.notes.isEmpty());
    Registry reg2({}, {R("Gold platter", "Shields", I({{"Silver platter", 2}}))});
    ml = materialList(reg2, *reg2.resolve("Gold platter"), {}, "alchemy", {}, 1, 0,
                      [](const QString &) { return QStringList{"The Bad Cave"}; });
    CHECK_EQ(ml.materials[0].how, QStringLiteral("Found: The Bad Cave"));
    const QString text = materialListText(materialList(reg, *s1, shops));
    CHECK(text.contains(QStringLiteral("9 × Slimedrop")) && text.contains(QStringLiteral("1 × Sword 1")));

    auto st = freshStore();
    CHECK(st->loadListChoices().isEmpty());
    st->saveListChoices({{s2->key, false}});
    const auto back = st->loadListChoices();
    CHECK(back.size() == 1 && back.value(s2->key, true) == false);
}

static void testBookmarks()
{
    Registry reg({E(1, "Shell slime", "u", {D("Tortoiseshell", "Item", 1, 8)})},
                 {R("Gold platter", "Shields", I({{"Silver platter", 1}})), R("Tortoise shell", "Torso", I({{"Tortoiseshell", 2}}))});
    QList<Bookmark> marks;
    const ItemPtr gp = reg.resolve("Gold platter"), ts = reg.item("tortoiseshell~equip"), shell = reg.resolve("Tortoiseshell");
    CHECK(toggleBookmark(marks, *gp) && toggleBookmark(marks, *ts));
    CHECK(isBookmarked(marks, gp.get()) && isBookmarked(marks, ts.get()));
    CHECK(!isBookmarked(marks, shell.get()) && !isBookmarked(marks, nullptr));
    CHECK_EQ(mapNames(bookmarkItems(reg, marks), [](const auto &p) { return p.second->name; }), (QStringList{"Gold platter", "Tortoise shell"}));
    moveBookmark(marks, *ts, -1);
    CHECK_EQ(mapNames(marks, [](const Bookmark &m) { return m.name; }), (QStringList{"Tortoise shell", "Gold platter"}));
    moveBookmark(marks, *ts, -1);
    CHECK_EQ(mapNames(marks, [](const Bookmark &m) { return m.name; }), (QStringList{"Tortoise shell", "Gold platter"}));
    CHECK(!toggleBookmark(marks, *gp) && mapNames(marks, [](const Bookmark &m) { return m.name; }) == QStringList{"Tortoise shell"});
    marks = {{"oldkey", "Gold platter"}, {"gone", "Mystery blade"}};
    const auto found = bookmarkItems(reg, marks);
    CHECK(found[0].second == gp && found[1].second == nullptr);
    CHECK(refreshBookmarkKeys(reg, marks) && marks[0].key == gp->key && isBookmarked(marks, gp.get()));
    CHECK(!refreshBookmarkKeys(reg, marks));
    auto st = freshStore();
    CHECK(st->loadBookmarks().isEmpty());
    st->saveBookmarks(marks);
    CHECK(st->loadBookmarks() == marks);
    QFile f(st->dir() + QStringLiteral("/bookmarks.json"));
    if (f.open(QIODevice::WriteOnly)) {
        f.write("{\"not\": \"a list\"}");
        f.close();
    }
    CHECK(st->loadBookmarks().isEmpty());
}

static void testRobotsAndText()
{
    const Robots woodus = Robots::parse("User-agent: *\nDisallow: /den/games/dq9ds/alchemy_recipes.php\n");
    CHECK(!woodus.canFetch(Http::UA, RECIPE_URL));
    CHECK(woodus.canFetch(Http::UA, "https://www.woodus.com/other.php"));
    const Robots named = Robots::parse("User-agent: dq9alchemyhelper\nDisallow: /\n\nUser-agent: *\nAllow: /\n");
    CHECK(!named.canFetch(Http::UA, "https://x.org/page"));
    CHECK(named.canFetch("OtherBot/1.0", "https://x.org/page"));
    const Robots empty = Robots::parse("User-agent: *\nDisallow:\n");
    CHECK(empty.canFetch(Http::UA, "https://x.org/anything"));
    CHECK(Robots::allowAll().canFetch(Http::UA, "https://x.org/") && !Robots::disallowAll().canFetch(Http::UA, "https://x.org/"));

    CHECK(similar("Oak staff", "Oaken staff") && !similar("Mitre", "Priest costume"));
    CHECK(qAbs(sequenceRatio("abcd", "bcde") - 0.75) < 1e-9);
    CHECK_EQ(canonCategory("helmet"), QStringLiteral("Head"));
    CHECK_EQ(canonCategory("swords"), QStringLiteral("Swords"));
    CHECK_EQ(canonCategory("weird thing"), QStringLiteral("Weird Thing"));
    CHECK_EQ(wtText("{{Boomerang W}}"), QStringLiteral("Boomerang"));
    CHECK_EQ(wtText("[[A|b]] and [[C]]x"), QStringLiteral("b and Cx"));
    CHECK_EQ(wtText("[[Iron nail]]s", false), QStringLiteral("Iron nail"));
    CHECK_EQ(thousands(1234567), QStringLiteral("1,234,567"));
    CHECK_EQ(esc("<a href='x'>&\""), QStringLiteral("&lt;a href=&#x27;x&#x27;&gt;&amp;&quot;"));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    g_tmp = std::make_unique<QTemporaryDir>();
    const std::pair<const char *, std::function<void()>> tests[] = {
        {"basics", testBasics}, {"sheet", testSheet}, {"merge_and_nesting", testMergeAndNesting},
        {"exact_names", testExactNames}, {"shops", testShops}, {"item_pages", testItemPages},
        {"tree", testTree}, {"prefer", testPrefer}, {"sprites", testSprites}, {"weapons", testWeapons},
        {"unsourced_items", testUnsourcedItems}, {"material_list", testMaterialList},
        {"bookmarks", testBookmarks}, {"robots_and_text", testRobotsAndText},
    };
    for (const auto &[name, fn] : tests) {
        const int before = g_failures;
        try {
            fn();
        } catch (const std::exception &e) {
            ++g_failures;
            std::fprintf(stderr, "FAIL %s: exception: %s\n", name, e.what());
        }
        std::printf("%-20s %s\n", name, g_failures == before ? "ok" : "FAILED");
    }
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures == 0)
        std::printf("all parser tests passed\n");
    return g_failures ? 1 : 0;
}
