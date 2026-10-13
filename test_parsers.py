"""Run with:  python test_parsers.py   (no network, no PyQt needed)"""
import dq9_data as d

IMG = "https://dragon-quest.org/w/images/x.gif"


def loot(kind, name, title, rate):
    return (f'<img alt="{kind}" src="{IMG}"> <a href="/w/index.php?title={title.replace(" ", "_")}" '
            f'title="{title}">{name}</a> &nbsp; {rate} &nbsp; ')


ENEMY_HTML = f"""
<table class="wikitable"><tr><th>Enemy</th><th>Gold</th><th>Experience</th><th>Loot and Rate</th></tr>
<tr><td>#001 <a href="/w/index.php?title=Slime" title="Slime">Slime</a></td><td>4 G</td><td>2</td>
 <td>{loot("Item", "Medicinal herb", "Medicinal herb", "1/8")}{loot("Item", "Slimedrop", "Slimedrop", "1/16")}</td></tr>
<tr><td>#066 <a href="/w/index.php?title=Spinchilla" title="Spinchilla">Spinchilla</a></td><td>112 G</td><td>300</td>
 <td>{loot("Item", "Seashell", "Sun-bleached seashell", "1/16")}{loot("Item", "Crimson coral", "Crimson coral", "1\u20442")}</td></tr>
<tr><td>#012 <a href="/w/index.php?title=Winkster" title="Winkster">Winkster</a></td><td>25 G</td><td>22</td>
 <td>{loot("Legs", "Cotton trousers", "Cotton trousers", "1/32")}{loot("Wand", "Oak staff", "Oaken staff", "1/64")}</td></tr>
<tr><td>#175 <a href="/w/index.php?title=Dreadful_drackal" title="x">Dreadful drackal</a></td><td>180 G</td><td>2000</td>
 <td>{loot("Item", "Magic beast hide", "Magic beast hide", "116/")}{loot("Item", "Magic beast horn", "Magic beast horn", "1/32")}</td></tr>
<tr><td>#231 <a href="/w/index.php?title=Grim_reaper">Grim reaper</a></td><td>165 G</td><td>5000</td>
 <td>{loot("Arms", "Minister's mittens", "Priest costume", "1/64")}{loot("Head", "Mitre", "Priest costume", "1/64")}</td></tr>
</table>"""

# three plausible layouts for the Location(s) field on the enemy page
LOC_TABLE = """<h2>Appearances</h2><h3>Dragon Quest VIII</h3><table><tr><th>Location(s)</th><td>Farebury Region</td></tr></table>
<h3><span>Dragon Quest IX: Sentinels of the Starry Skies</span></h3>
<table><tr><th>Experience</th><td>2</td></tr>
<tr><th>Location(s)</th><td><a href="/x">Angel Falls</a> region<br><a href="/y">The Hexagon</a><br><a href="/z">Bloomingdale (region)</a> region</td></tr>
<tr><th>Item(s) dropped</th><td>Medicinal herb 1/8</td></tr></table><h3>Dragon Quest XI</h3><p>Location(s) Heliodor</p>"""

LOC_DL = """<h3>Dragon Quest IX: Sentinels of the Starry Skies</h3><dl>
<dt>Location(s)</dt><dd><a>Angel Falls</a> region</dd><dd><a>The Hexagon</a></dd>
<dt>Item(s) dropped</dt><dd>Herb</dd></dl><h3>Next game</h3>"""

LOC_PLAIN = """<div class="mw-heading mw-heading3"><h3>Dragon Quest IX: Sentinels of the Starry Skies</h3></div>
<div>Location(s)<br><a>Angel Falls</a> region<br><a>The Hexagon</a><br>Item(s) dropped<br>Medicinal herb 1/8</div>
<div class="mw-heading mw-heading3"><h3>Dragon Quest XI</h3></div><div>Location(s)<br>Wrong place</div>"""

RECIPE_HTML = """<table><tr><th>Item</th><th>Type</th><th>Ingredients</th><th>Pot time</th></tr>
<tr><td>Copper sword</td><td>Sword</td><td>2x Iron ore<br>Slimedrop</td><td>10</td></tr>
<tr><td>Oaken staff</td><td>Staff</td><td>Sun-bleached seashell, 3 Crimson coral</td><td>5</td></tr>
<tr><td>Slime crown</td><td>Helmet</td><td>Slimedrop x4 + Mythril ore</td><td></td></tr></table>"""


def test_shops():
    import json, threading, tempfile, pathlib
    st = d.Store.__new__(d.Store)
    st.dir = pathlib.Path(tempfile.mkdtemp())
    st._loc_lock = threading.Lock()
    shops, items = st.shops(), st.shop_items()
    assert len(shops) == len(items) == 362, (len(shops), len(items))
    assert shops["coppersword"] == ["Angel Falls (Item Shop)", "Stornway (Weapon Shops)"]
    assert "tortoiseshell" not in shops and "tortoiseshell~equip" in shops
    assert d.shop_category("Pot Lid", "") == "Shields"
    assert d.shop_category("Carrying pole", "@Weapon") == "Staves"
    assert d.shop_category("Hardwood headwear", "@Armor") == "Head"
    assert d.shop_category("Gold ring", "") == "Accessories"
    assert d.shop_category("Medicinal herb", "") == d.ITEMS_CAT
    assert d.shop_category("Zork", "@Weapon") == "Other weapons"
    assert d.shop_category("White Bouquet", "Fans") == "Fans"
    reg = d.Registry([], st.load_sheet_recipes(), items)
    assert reg.recipe_count == 445
    assert reg.resolve("Copper sword").category == "Swords"
    html, _ = d.render_item_html(reg, reg.resolve("Copper sword"), 5, lambda e: ("loading", []), shops)
    assert "Stornway (Weapon Shops)" in html
    # a crafted ingredient that shops sell shows where to buy it
    html, _ = d.render_item_html(reg, reg.resolve("Dragon slayer"), 5, lambda e: ("loading", []), shops)
    assert "Buy:" in html


def test_item_pages():
    html = ("<html><body><div id='page-content'><p>Aurora blade</p>"
            "<p>A wondrous weapon that flings flames if used as an item in battle.</p>"
            "<p><b>Classification</b>: Sword<br/><b>Used by</b>: <a href='/warrior'>Warrior</a>, "
            "<a href='/thief'>Thief</a></p><p><b>Attack</b>: 70<br/><b>Style</b>: 38<br/>"
            "<b>Special</b>: <i>Use:</i> Deals 6-10 Fire damage to all enemies</p>"
            "<p><b>Buy price</b>: Cannot Buy<br/><b>Sell price</b>: 4,700 G</p>"
            "<p><b>How to make</b>: <a href='/x'>Cautery sword</a> x1</p></div></body></html>")
    det = d.parse_item_page(html, "Aurora blade")
    f = dict(det["fields"])
    assert det["description"].startswith("A wondrous weapon"), det
    assert f["Classification"] == "Sword" and f["Used by"] == "Warrior, Thief", f
    assert f["Special"] == "Use: Deals 6-10 Fire damage to all enemies", f
    assert f["Sell price"] == "4,700 G" and f["Attack"] == "70"
    assert d.parse_item_page("<div id='page-content'><p>nothing</p></div>", "x") is None
    # label and value in separate table cells
    t = d.parse_item_page("<div id='page-content'><p>Desc.</p><table><tr><td>Sell price</td>"
                          "<td>10 G</td></tr></table></div>", "x")
    assert dict(t["fields"])["Sell price"] == "10 G", t
    idx = d.parse_wiki_index("<div id='page-content'><a href='/aurora-blade'>Aurora blade</a>"
                             "<a href='http://other.com/x'>Other</a><a href='#top'>Top</a></div>")
    assert idx == {"aurora blade": "https://dqnine.wikidot.com/aurora-blade"}, idx
    assert d.wikidot_slug("Warrior's sword") == "warrior-s-sword"
    assert d.wikidot_slug("\u00dcber falcon blade") == "uber-falcon-blade"
    reg = d.Registry([], [d.Recipe("Aurora blade", "Swords", [("Holy water", 1)])])
    html_out, _ = d.render_item_html(reg, reg.resolve("Aurora blade"), 5, lambda e: ("loading", []), {},
                                     lambda n: ("ok", det))
    assert "4,700 G" in html_out and "flings flames" in html_out
    html_out, _ = d.render_item_html(reg, reg.resolve("Aurora blade"), 5, lambda e: ("loading", []), {},
                                     lambda n: ("loading", None))
    assert "Loading item details" in html_out


def test_tree():
    import dq9_tree as t
    rec = lambda item, ing: d.Recipe(item, "Swords", ing)   # noqa: E731
    reg = d.Registry([], [rec("A", [("B", 2), ("C", 1)]), rec("B", [("A", 1), ("D", 1)]),
                          d.Recipe("A", "Swords", [("E", 1)])], {})
    root = t.build_tree(reg, reg.resolve("A"), 5, {"d": ["Zere (Item Shop)"]})
    assert [c.kind for c in root.children] == ["recipe", "recipe"], [c.kind for c in root.children]
    b = root.children[0].children[0]
    assert b.label == "B" and b.edge == "x2" and b.kind == "craft"
    assert [c.label for c in b.children] == ["A", "D"]
    assert b.children[0].sub == "(loops back)" and not b.children[0].children   # loop stops
    assert b.children[1].kind == "shop"                                      # D is only in a shop
    w, h = t.layout(root)
    nodes = list(t.walk(root))
    leaves = sorted(n.y for n in nodes if not n.children)
    assert all(b2 - a2 >= t.NODE_H for a2, b2 in zip(leaves, leaves[1:])), leaves   # no overlap
    for n in nodes:
        for c in n.children:
            assert c.x > n.x
        if n.children:
            assert n.children[0].y <= n.y <= n.children[-1].y
    assert w > 0 and h > 0
    # real data: the whole Dragon slayer chain
    import threading, tempfile, pathlib
    st = d.Store.__new__(d.Store)
    st.dir = pathlib.Path(tempfile.mkdtemp())
    st._loc_lock = threading.Lock()
    reg = d.Registry([], st.load_sheet_recipes(), st.shop_items())
    root = t.build_tree(reg, reg.resolve("Dragon slayer"), 5, st.shops())
    labels = {n.label for n in t.walk(root)}
    assert {"Mighty armlet", "Strength ring", "Aggressence", "Medicinal herb"} <= labels, labels
    html, _ = d.render_enemy_html(d.Enemy(1, "Slime", "u", "1", "2", [d.Drop("Bandana", "Head", 1, 16)]),
                                  lambda e: ("ok", ["Zere"]))
    assert "Slime" in html and "Zere" in html and "1/16" in html


def test_prefer():
    import dq9_tree as t
    sword = d.Enemy(1, "Slime", "u", "", "", [d.Drop("Zap", "Sword", 1, 8), d.Drop("Herb", "Item", 1, 4)])
    reg = d.Registry([sword], [d.Recipe("Zap", "Swords", [("Herb", 2)])], {})
    a = t.build_tree(reg, reg.resolve("Zap"), 5, {}, "alchemy")
    assert [c.kind for c in a.children] == ["base"] and a.children[0].edge == "x2"
    assert [c.kind for c in a.children[0].children] == ["monster"]    # Herb has no recipe: its drops
    m = t.build_tree(reg, reg.resolve("Zap"), 5, {}, "drops")
    assert [c.kind for c in m.children] == ["monster"] and m.children[0].label == "Slime"
    two = d.Registry([d.Enemy(1, "A", "u", "", "", [d.Drop("Herb", "Item", 1, 4)]),
                      d.Enemy(2, "B", "u", "", "", [d.Drop("Herb", "Item", 1, 8)])], [], {})
    one = t.build_tree(two, two.resolve("Herb"), 1, {})
    assert [c.label for c in one.children] == ["A"] and one.children[0].more == 1   # + badge, no grey box
    both = t.build_tree(two, two.resolve("Herb"), 2, {})
    assert [c.label for c in both.children] == ["A", "B"] and all(c.more == 0 for c in both.children)
    # no recipe and no drops of the preferred kind: falls back to what exists
    only_r = d.Registry([], [d.Recipe("Zap", "Swords", [("Herb", 1)])], {})
    assert t.build_tree(only_r, only_r.resolve("Zap"), 5, {}, "drops").children[0].label == "Herb"


def test_sprites():
    import threading, tempfile, pathlib
    html = ("<div id='mw-content-text'><table><tr><td><a href='/w/File:x' title='File:Aggressence DQIX icon.png'>"
            "<img alt='Aggressence DQIX icon.png' src='/w/images/thumb/d/dc/Aggressence_DQIX_icon.png/32px-Aggressence_DQIX_icon.png'/></a></td>"
            "<td><a href='/w/Aggressence'>Aggressence</a></td>"
            "<td><a title='Strength ring'><img src='/w/images/5/58/Strength_ring_DQIX_icon.png'/></a>"
            "<img alt='\u00dcber Gringham whip' src='/w/images/a/ab/Uber_Gringham_whip_DQIX_icon.png'/>"
            "<img src='data:image/gif;base64,AAAA' alt='skip'/>"
            "<img alt='Aggressence' src='/w/images/zz/zz/Other.png'/></td></tr></table></div>")
    sp = d.parse_sprites(html)
    assert sp["aggressence"] == "https://dragon-quest.org/w/images/d/dc/Aggressence_DQIX_icon.png", sp   # thumb -> original, first wins
    assert sp["strength ring"].endswith("/5/58/Strength_ring_DQIX_icon.png"), sp
    assert "\u00fcber gringham whip" in sp and "skip" not in sp, sp
    st = d.Store.__new__(d.Store)
    st.dir = pathlib.Path(tempfile.mkdtemp())
    st._loc_lock = threading.Lock()
    st.sprite_index = {"tortoiseshell": "u1", "tortoise shell": "u2", "dragon's scale": "u3"}
    st._sprite_norm = {}
    st.load_sprite_index = None
    # same lookups load_sprite_index builds: ambiguous normalised names must not be shared
    for k in st.sprite_index:
        st._sprite_norm.setdefault(d.norm(k), []).append(k)
    st._sprite_norm = {n: v[0] for n, v in st._sprite_norm.items() if len(v) == 1}
    assert st.has_sprite("Tortoiseshell") and st.has_sprite("Dragon\u2019s scale")
    assert not st.has_sprite("Mystery item")
    import dq9_tree as t
    reg = d.Registry([], [d.Recipe("A", "Swords", [("B", 1)])], {})
    root = t.build_tree(reg, reg.resolve("A"), 1, {})
    assert root.sprite == "A"


def main():
    enemies = d.parse_enemy_list(ENEMY_HTML)
    assert [e.name for e in enemies] == ["Slime", "Spinchilla", "Winkster", "Dreadful drackal", "Grim reaper"]
    assert enemies[0].url == "https://dragon-quest.org/wiki/Slime", enemies[0].url
    assert enemies[0].number == 1 and enemies[0].gold == "4 G"
    s = {x.item: x for x in enemies[0].drops}
    assert (s["Medicinal herb"].num, s["Medicinal herb"].den) == (1, 8)
    assert enemies[1].drops[1].den == 2                       # U+2044 fraction slash
    assert (enemies[3].drops[0].num, enemies[3].drops[0].den) == (1, 16)   # "116/" typo
    assert enemies[2].drops[1].kind == "Wand"

    for html in (LOC_TABLE, LOC_DL, LOC_PLAIN):
        locs = d.parse_enemy_locations(html)
        assert locs == ["Angel Falls region", "The Hexagon", "Bloomingdale (region) region"][: len(locs)] or \
            locs[:2] == ["Angel Falls region", "The Hexagon"], locs
        assert "Farebury Region" not in locs and "Wrong place" not in locs and "Heliodor" not in locs, locs
        assert not any("dropped" in x.lower() or "herb" in x.lower() for x in locs), locs
    assert d.parse_enemy_locations("<h3>Dragon Quest VIII</h3><p>nothing</p>") == []

    recipes = d.parse_recipes(RECIPE_HTML)
    assert [r.item for r in recipes] == ["Copper sword", "Oaken staff", "Slime crown"]
    assert recipes[0].ingredients == [("Iron ore", 2), ("Slimedrop", 1)], recipes[0].ingredients
    assert recipes[1].ingredients == [("Sun-bleached seashell", 1), ("Crimson coral", 3)]
    assert recipes[2].ingredients == [("Slimedrop", 4), ("Mythril ore", 1)]
    assert [r.category for r in recipes] == ["Swords", "Staves", "Head"]
    assert recipes[0].extra == {"Pot Time": "10"}

    reg = d.Registry(enemies, recipes)
    # cross-site name matching: wiki says "Oak staff"/"Seashell", recipe site says "Oaken staff"/"Sun-bleached seashell"
    staff = reg.resolve("Oaken staff")
    assert staff is reg.resolve("Oak staff") and staff.recipes and staff.droppers
    assert reg.resolve("Sun-bleached seashell").droppers[0][0].name == "Spinchilla"
    # distinct items sharing one wiki link title must not be merged
    assert reg.resolve("Mitre") is not reg.resolve("Minister's mittens")
    # recipe category beats wiki icon; unknown item gets created
    assert staff.category == "Staves"
    assert reg.resolve("Copper sword").category == "Swords"
    cats = reg.categories()
    assert cats.index("Swords") < cats.index("Head") and cats[-1] == d.ITEMS_CAT, cats
    assert [i.name for i in reg.items_in("Staves")] == ["Oak staff"] or [i.name for i in reg.items_in("Staves")] == ["Oaken staff"]

    # rendering
    info = reg.resolve("Copper sword")
    html, needed = d.render_item_html(reg, info, 5, lambda e: ("ok", ["Somewhere"]), {})
    assert "Alchemy" in html and "2 &times; Iron ore" in html and "No monster on the enemy list" in html
    info = reg.resolve("Slime crown")
    html, needed = d.render_item_html(reg, info, 5, lambda e: ("loading", []), {})
    assert "Slimedrop" in html and "Slime" in [e.name for e in needed]

    try:
        d.parse_recipes("<html><body><p>nope</p></body></html>")
        raise SystemExit("expected ParseError")
    except d.ParseError as e:
        assert "No recipes" in str(e)
    test_sheet()
    test_merge_and_nesting()
    test_exact_names()
    test_shops()
    test_item_pages()
    test_tree()
    test_prefer()
    test_sprites()
    print("all parser tests passed")


def test_sheet():
    # ingredient cells: "(n)" quantities, missing quantity, stray spaces, accented names
    assert d.parse_sheet_ingredients("Boss shield \n Agate of evolution (1)\n Mighty armlet (1)") == \
        [("Boss shield", 1), ("Agate of evolution", 1), ("Mighty armlet", 1)]
    assert d.parse_sheet_ingredients("Rusty sword (1)\n Glass frit (9)") == [("Rusty sword", 1), ("Glass frit", 9)]
    assert d.parse_sheet_ingredients("\u00dcber war hammer (1)") == [("\u00dcber war hammer", 1)]
    assert d.parse_sheet_ingredients("") == []

    # alphabetical runs: a step backwards starts a new type; accents sort as plain letters
    assert d._alphabetical_runs(["Supernova sword", "\u00dcber falcon blade", "Warrior's sword"]) == [[0, 1, 2]]
    assert d._alphabetical_runs(["Warrior's sword", "Celestial spear", "Trident", "Assassin's dagger"]) == [[0], [1, 2], [3]]

    cols = {
        "Weapon": [("Alpha sword", "Iron ore (1)"), ("Beta blade", "Iron ore (2)"),              # run 1
                   ("Aardvark spear", "Slimedrop (1)"), ("Cat lance", "Slimedrop (2)"),          # run 2
                   ("Ant dagger", "Wing of bat (1)"), ("Fenrir fang", "Wing of bat (2)"),
                   ("Soul breaker", "Wing of bat (3)"),                  # run 3: no keyword of its own
                   ("Aaa thing", "Iron ore (3)"), ("Bbb thing", "Iron ore (4)")],  # run 4: no keywords at all
        "Armor": [("Big shield", "Iron ore (1)"), ("Psyche swiper", "Iron ore (2)"),
                  ("Apple hat", "Iron ore (3)")],
        "Accessory": [("Agility ring", "Gold ring (1)")],
        "Item": [("Gold bar", "Flintstone (3)")],
        "Weird": [("Thing", "Stuff (1)")],
    }
    rs = {r.item: r for r in d.recipes_from_columns(cols)}
    assert rs["Alpha sword"].category == rs["Beta blade"].category == "Swords"
    assert rs["Aardvark spear"].category == "Spears"
    assert rs["Ant dagger"].category == rs["Fenrir fang"].category == rs["Soul breaker"].category == "Knives"
    assert rs["Aaa thing"].category == rs["Bbb thing"].category == "Other weapons"
    assert rs["Big shield"].category == rs["Psyche swiper"].category == "Shields"   # run vote beats name
    assert rs["Apple hat"].category == "Head"
    assert rs["Agility ring"].category == "Accessories" and rs["Gold bar"].category == d.ITEMS_CAT
    assert rs["Thing"].category == "Weird" and rs["Alpha sword"].source == "spreadsheet"

    # the shipped copy of the real spreadsheet
    shipped = d.Store(__import__("tempfile").mkdtemp()).load_sheet_recipes()
    assert len(shipped) == 445, len(shipped)
    cats = __import__("collections").Counter(r.category for r in shipped)
    assert cats["Swords"] == 17 and cats["Knives"] == 15 and cats["Shields"] == 29 and cats["Torso"] == 67, cats
    assert not any(c.startswith("Other") for c in cats), cats
    by = {r.item: r for r in shipped}
    for name, cat in (("Fenrir fang", "Knives"), ("Gladius", "Knives"), ("Psyche swiper", "Shields"),
                      ("Gold platter", "Shields"), ("Ace of clubs", "Hammers"), ("Halberd", "Spears"),
                      ("Raging ruby", "Accessories"), ("Agate of evolution", d.ITEMS_CAT)):
        assert by[name].category == cat, (name, by[name].category)
    assert by["Aurora blade"].ingredients == [("Cautery sword", 1), ("Brighten rock", 3), ("Holy water", 1)]
    assert by["Big boss shield"].ingredients[0] == ("Boss shield", 1)   # no (n) in the sheet -> 1
    assert not any(n == "Agressence" for r in shipped for n, _ in r.ingredients)   # sheet typo is corrected

    # real file, when it is next to this test and openpyxl is installed
    import os
    here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dq9_stuff.xlsx")
    try:
        import openpyxl  # noqa: F401
    except ImportError:
        openpyxl = None
    if openpyxl and os.path.exists(here):
        assert d.parse_recipe_sheet(here) == shipped
        print("  (also re-parsed dq9_stuff.xlsx and it matches the shipped sheet_recipes.json)")


def _drop(item, kind, den):
    return d.Drop(item, kind, 1, den, item)


def test_merge_and_nesting():
    R = d.Recipe
    enemies = [
        d.Enemy(1, "Riptide", "u1", drops=[_drop("Dragon's scale", "Item", 32)]),
        d.Enemy(2, "Axolhotl", "u2", drops=[_drop("Cautery sword", "Sword", 64), _drop("Skull helm", "Spear", 8)]),
        d.Enemy(3, "Goodybag", "u3", drops=[_drop("Brighten rock", "Item", 16)]),
    ]
    woodus = [R("Slime crown", "Head", [("Dragon's scale", 3), ("Mythril ore", 1)], {"Pot time": "5"}),
              R("Axe thing", "Axes", [("Iron ore", 1)])]
    sheet = [
        R("Slime crown", "", [("Dragon scale", 3), ("Mythril ore", 1)], {}, "spreadsheet"),   # same recipe, other spelling
        R("Axe thing", "Swords", [("Rock", 2)], {}, "spreadsheet"),                           # different recipe, other category
        R("Aurora blade", "Swords", [("Cautery sword", 1), ("Brighten rock", 3)], {}, "spreadsheet"),
        R("Skull helm", "Head", [("Iron ore", 1)], {}, "spreadsheet"),                       # wiki icon says Spear
        R("Big crown", "Head", [("Slime crown", 1), ("Gold bar", 2)], {}, "spreadsheet"),
        R("Gold bar", d.ITEMS_CAT, [("Aurora blade", 1)], {}, "spreadsheet"),                # loops: crown -> bar -> blade...
        R("Alpha", d.ITEMS_CAT, [("Beta", 1)], {}, "spreadsheet"),
        R("Beta", d.ITEMS_CAT, [("Alpha", 1)], {}, "spreadsheet"),
    ] + [R(f"Link{i}", d.ITEMS_CAT, [(f"Link{i + 1}", 1)], {}, "spreadsheet") for i in range(12)]
    reg = d.Registry(enemies, woodus + sheet)

    crown = reg.resolve("Slime crown")
    assert len(crown.recipes) == 1 and crown.recipes[0].source == "spreadsheet, woodus", crown.recipes
    assert crown.recipes[0].extra == {"Pot time": "5"} and crown.category == "Head"
    axe = reg.resolve("Axe thing")
    assert len(axe.recipes) == 2 and axe.category == "Axes"          # woodus' own label beats the sheet's, any order
    reg2 = d.Registry(enemies, sheet + woodus)
    assert reg2.resolve("Axe thing").category == "Axes"
    assert reg.resolve("Skull helm").category == "Head"              # sheet type beats the wiki icon
    assert reg.recipe_count == len(woodus) + len(sheet) - 1          # only the duplicate collapsed

    # nested recipes: Aurora blade -> Cautery sword (drops) + Brighten rock (drops)
    html, needed = d.render_item_html(reg, reg.resolve("Aurora blade"), 5, lambda e: ("ok", ["Somewhere"]), {})
    assert "Axolhotl" in html and "Goodybag" in html and "Source: spreadsheet" in html
    assert {e.name for e in needed} == {"Axolhotl", "Goodybag"}
    # an ingredient that is itself crafted is unfolded, not reported as "no monster drops it"
    html, _ = d.render_item_html(reg, reg.resolve("Big crown"), 5, lambda e: ("ok", ["X"]), {})
    assert "Can be made by alchemy" in html and "<blockquote>" in html
    assert html.count("Riptide") >= 1 and "No monster on the enemy list" in html   # Mythril ore / Gold bar chain
    # loops stop
    html, _ = d.render_item_html(reg, reg.resolve("Alpha"), 5, lambda e: ("ok", ["X"]), {})
    assert "loops back" in html
    # very deep chains stop too
    html, _ = d.render_item_html(reg, reg.resolve("Link0"), 5, lambda e: ("ok", ["X"]), {})
    assert "Select it in the list" in html and html.count("<blockquote>") == d.MAX_NEST


def test_exact_names():
    """Real near-collisions from the data: matching must be exact (ignoring case/spacing/'s)."""
    R = d.Recipe
    enemies = [d.Enemy(1, "Wyrtle", "u1", drops=[_drop("Tortoiseshell", "Item", 8)]),
               d.Enemy(2, "Spinchilla", "u2", drops=[_drop("Safety shoes", "Feet", 64)])]
    recipes = [
        R("Tortoise shell", "Torso", [("Tortoiseshell", 5), ("Iron ore", 1)], {}, "spreadsheet"),
        R("Tortoiseshell fan", "Fans", [("Steel fan", 1), ("Tortoiseshell", 3)], {}, "spreadsheet"),
        R("Safest shoes", "Feet", [("Safer shoes", 1)], {}, "spreadsheet"),
        R("Razer-wing boomerang", "Boomerangs", [("Razor-wing boomerang", 1)], {}, "spreadsheet"),
        R("Erazor-wing boomerang", "Boomerangs", [("Razer-wing boomerang", 1)], {}, "spreadsheet"),
        R("Hotshot bow", "Bows", [("Potshot bow", 1)], {}, "spreadsheet"),
    ]
    reg = d.Registry(enemies, recipes)
    assert d.norm("Dragon's scale") == d.norm("Dragon scale") == d.norm("DRAGON  SCALE")
    assert d.norm("Safest shoes") != d.norm("Safety shoes")

    # ingredient "Tortoiseshell" and armour "Tortoise shell" are different items
    ing = reg.resolve("Tortoiseshell")
    assert ing.category == d.ITEMS_CAT and ing.droppers and not ing.recipes
    armour = reg.items["tortoiseshell~equip"]
    assert armour.name == "Tortoise shell" and armour.category == "Torso" and armour.recipes
    assert "Tortoise shell" in [i.name for i in reg.items_in("Torso")]
    assert "Tortoiseshell" in [i.name for i in reg.items_in(d.ITEMS_CAT)]
    html, _ = d.render_item_html(reg, reg.resolve("Tortoiseshell fan"), 5, lambda e: ("ok", ["X"]), {})
    assert "Wyrtle" in html and "Can be made by alchemy" not in html     # the ingredient's drops, not the armour's recipe

    # one-letter-apart names stay separate
    assert reg.resolve("Safest shoes") is not reg.resolve("Safety shoes")
    assert reg.resolve("Safety shoes").droppers and not reg.resolve("Safest shoes").droppers
    assert reg.resolve("Razer-wing boomerang") is not reg.resolve("Erazor-wing boomerang")
    assert reg.resolve("Razor-wing boomerang") is None                  # unknown stays unknown, not guessed
    assert reg.resolve("Hotshot bow") is not None and reg.resolve("Potshot bow") is None
    html, _ = d.render_item_html(reg, reg.resolve("Razer-wing boomerang"), 5, lambda e: ("ok", ["X"]), {})
    assert "No monster on the enemy list" in html


if __name__ == "__main__":
    main()
