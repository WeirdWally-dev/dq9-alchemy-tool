"""
dq9_data.py - data layer for the DQ9 Alchemy Helper.

Sources
-------
* Dragon Quest Wiki "List of enemies in Dragon Quest IX"  -> monsters, drops, drop rates.
  Locations are NOT in that table; they live on each monster's own wiki page, so they are
  fetched lazily (one page per monster, cached forever after the first look-up).
* woodus.com alchemy recipes page -> recipes (item, type, ingredients).
  That site's robots.txt forbids automated access, so this module checks robots.txt and, if
  blocked, the GUI asks you to import a copy of the page you saved from your own browser.
* dq9_stuff.xlsx ("All Alchemy Recipes" spreadsheet) -> a second recipe source. A parsed copy
  ships next to this file as sheet_recipes.json, so it works with no extra steps; the GUI's
  "Import spreadsheet..." button re-reads an .xlsx (needs the openpyxl package).
  Both recipe sources are merged and de-duplicated in Registry.

Everything downloaded is cached in ~/.dq9_helper/ so the sites are hit as little as possible.
"""
from __future__ import annotations

import difflib
import hashlib
import json
import re
import threading
import time
import unicodedata
from collections import Counter
from dataclasses import asdict, dataclass, field, replace
from html import escape as esc
from pathlib import Path
from typing import Callable, Dict, List, Optional, Tuple
from urllib.parse import parse_qs, quote, unquote, urljoin, urlparse
from urllib.robotparser import RobotFileParser

import requests
from bs4 import BeautifulSoup, NavigableString, Tag

ENEMY_URL = "https://dragon-quest.org/wiki/List_of_enemies_in_Dragon_Quest_IX"
RECIPE_URL = "https://www.woodus.com/den/games/dq9ds/alchemy_recipes.php"
APP_DIR = Path.home() / ".dq9_helper"
BUNDLED_SHEET = Path(__file__).resolve().with_name("sheet_recipes.json")
SPRITE_URL = "https://dragon-quest.org/w/index.php?title=List_of_alchemy_ingredients_in_Dragon_Quest_IX"
WIKIDOT = "https://dqnine.wikidot.com"
WIKIDOT_INDEX = WIKIDOT + "/alchemy"
BUNDLED_SHOPS = Path(__file__).resolve().with_name("shops_bundled.json")

ITEMS_CAT = "Ingredients & items"
EQUIP_ORDER = [
    "Swords", "Spears", "Axes", "Staves", "Wands", "Whips", "Claws", "Knives", "Bows",
    "Hammers", "Fans", "Boomerangs", "Shields", "Head", "Torso", "Arms", "Legs", "Feet",
    "Accessories",
]


class ParseError(Exception):
    pass


class RobotsDisallowed(Exception):
    pass


# --------------------------------------------------------------------------- helpers
def norm(s: str) -> str:
    """Matching key for an item name: ignores case, spaces, punctuation and a possessive 's
    ("Dragon's scale" == "Dragon scale"). Deliberately NOT fuzzy: DQ9 has many distinct items
    one letter apart (Razer-wing / Razor-wing / Erazor-wing boomerang, Safest / Safety shoes,
    Hotshot / Potshot bow), and merging those would show the wrong recipe."""
    return re.sub(r"[^a-z0-9]", "", re.sub(r"['’]s\b", "", s.lower()))


def _same_ingredients(a: List[Tuple[str, int]], b: List[Tuple[str, int]]) -> bool:
    """True if two ingredient lists are the same recipe (same names and quantities)."""
    return sorted((norm(n), q) for n, q in a) == sorted((norm(n), q) for n, q in b)


def _similar(a: str, b: str) -> bool:
    na, nb = norm(a), norm(b)
    if not na or not nb:
        return False
    return na in nb or nb in na or difflib.SequenceMatcher(None, na, nb).ratio() >= 0.75


_CATS = {
    "sword": "Swords", "spear": "Spears", "axe": "Axes", "staff": "Staves", "stave": "Staves",
    "wand": "Wands", "whip": "Whips", "claw": "Claws", "knife": "Knives", "dagger": "Knives",
    "bow": "Bows", "hammer": "Hammers", "fan": "Fans", "boomerang": "Boomerangs",
    "shield": "Shields", "head": "Head", "helmet": "Head", "hat": "Head", "headgear": "Head",
    "headwear": "Head", "torso": "Torso", "body": "Torso", "bodyarmour": "Torso",
    "bodyarmor": "Torso", "armour": "Torso", "armor": "Torso", "arms": "Arms", "arm": "Arms",
    "gloves": "Arms", "handwear": "Arms", "legs": "Legs", "leg": "Legs", "legwear": "Legs",
    "feet": "Feet", "foot": "Feet", "shoes": "Feet", "footwear": "Feet", "boots": "Feet",
    "accessory": "Accessories", "accessories": "Accessories",
    "item": ITEMS_CAT, "consumable": ITEMS_CAT, "tool": ITEMS_CAT, "material": ITEMS_CAT,
    "ingredient": ITEMS_CAT,
}


def canon_category(raw: str) -> str:
    k = re.sub(r"[^a-z]", "", (raw or "").lower())
    if k in _CATS:
        return _CATS[k]
    for suffix in ("es", "s"):
        if k.endswith(suffix) and k[: -len(suffix)] in _CATS:
            return _CATS[k[: -len(suffix)]]
    return (raw or "").strip().title() or "Other"


RATE_RE = re.compile(r"(\d+)\s*[/\u2044]\s*(\d+)")


def parse_rate(text: str) -> Optional[Tuple[int, int]]:
    m = RATE_RE.search(text)
    if m:
        return int(m.group(1)), int(m.group(2))
    # The wiki has at least one typo where "1/16" was entered as "116/".
    m = re.match(r"^\s*1(\d+)\s*[/\u2044]\s*$", text)
    if m:
        return 1, int(m.group(1))
    return None


def wiki_url(href: str) -> str:
    """Turn /w/index.php?title=Foo links into the canonical /wiki/Foo form."""
    full = urljoin(ENEMY_URL, href)
    p = urlparse(full)
    if p.path.endswith("index.php"):
        title = parse_qs(p.query).get("title")
        if title:
            return "https://dragon-quest.org/wiki/" + quote(title[0].replace(" ", "_"), safe="_():,'!-")
    return full.split("#")[0]


# --------------------------------------------------------------------------- models
@dataclass
class Drop:
    item: str
    kind: str          # icon label on the wiki: Item, Sword, Torso, ...
    num: int = 0
    den: int = 0
    title: str = ""    # wiki link title, sometimes a fuller item name

    @property
    def prob(self) -> float:
        return self.num / self.den if self.den else 0.0

    @property
    def odds_text(self) -> str:
        if not self.den:
            return "?"
        return f"{self.num}/{self.den} ({self.prob * 100:.2f}%)"


@dataclass
class Enemy:
    number: int
    name: str
    url: str
    gold: str = ""
    exp: str = ""
    drops: List[Drop] = field(default_factory=list)

    @classmethod
    def from_dict(cls, d: dict) -> "Enemy":
        d = dict(d)
        d["drops"] = [Drop(**x) for x in d.get("drops", [])]
        return cls(**d)


@dataclass
class Recipe:
    item: str
    category: str
    ingredients: List[Tuple[str, int]]
    extra: Dict[str, str] = field(default_factory=dict)
    source: str = ""   # "woodus" (or "" for older caches), "spreadsheet", or both once merged

    @classmethod
    def from_dict(cls, d: dict) -> "Recipe":
        d = dict(d)
        d["ingredients"] = [tuple(x) for x in d.get("ingredients", [])]
        return cls(**d)


# --------------------------------------------------------------------------- HTML helpers
_BLOCKS = {"li", "p", "div", "tr", "dd", "dt", "ul", "ol", "table", "h1", "h2", "h3", "h4"}


def flatten(el) -> str:
    """Text of an element, with <br> and block elements turned into newlines."""
    if isinstance(el, NavigableString):
        return str(el)
    if not isinstance(el, Tag) or el.name in ("script", "style"):
        return ""
    if el.name == "br":
        return "\n"
    inner = "".join(flatten(c) for c in el.children)
    return f"\n{inner}\n" if el.name in _BLOCKS else inner


def _clean_lines(text: str) -> List[str]:
    out = []
    for line in text.split("\n"):
        line = re.sub(r"\s+", " ", line.replace("\u00a0", " ")).strip()
        if line:
            out.append(line)
    return out


# --------------------------------------------------------------------------- enemy list
def _parse_loot(td: Tag) -> List[Drop]:
    drops: List[Drop] = []
    kind: Optional[str] = None
    pending: Optional[dict] = None

    def flush(rate: Optional[Tuple[int, int]] = None):
        nonlocal pending
        if pending:
            n, d = rate if rate else (0, 0)
            drops.append(Drop(pending["item"], pending["kind"], n, d, pending["title"]))
            pending = None

    for node in td.descendants:
        if isinstance(node, Tag):
            if node.name == "img":
                kind = (node.get("alt") or "Item").strip()
            elif node.name == "a":
                txt = node.get_text(" ", strip=True)
                if txt:
                    flush()
                    pending = {"item": txt, "title": node.get("title") or "", "kind": kind or "Item"}
                    kind = None
        elif isinstance(node, NavigableString) and node.parent.name != "a":
            text = str(node)
            if pending and text.strip():
                rate = parse_rate(text)
                if rate:
                    flush(rate)
    flush()
    return drops


def parse_enemy_list(html: str) -> List[Enemy]:
    soup = BeautifulSoup(html, "html.parser")
    enemies: List[Enemy] = []
    for table in soup.find_all("table"):
        head = table.find("tr")
        head_text = head.get_text(" ").lower() if head else ""
        if "enemy" not in head_text or "loot" not in head_text:
            continue
        for tr in table.find_all("tr"):
            tds = tr.find_all("td", recursive=False)
            if len(tds) < 4:
                continue
            m = re.search(r"#\s*(\d+)", tds[0].get_text(" "))
            a = tds[0].find("a")
            if not m or not a:
                continue
            enemies.append(Enemy(
                number=int(m.group(1)),
                name=a.get_text(" ", strip=True),
                url=wiki_url(a.get("href", "")),
                gold=tds[1].get_text(" ", strip=True),
                exp=tds[2].get_text(" ", strip=True),
                drops=_parse_loot(tds[3]),
            ))
    if not enemies:
        raise ParseError("No enemy table with 'Enemy' and 'Loot' columns was found on the page.")
    return enemies


# --------------------------------------------------------------------------- enemy page: locations
_LABEL_RE = re.compile(r"^\s*Locations?\s*(\(s\))?\s*:?\s*$", re.I)
_STOP_RE = re.compile(
    r"^\s*(items?\b.*\b(dropped|drops?)\b|dropped items?|item drops?|stolen items?|"
    r"\w[\w /-]*resistance|family|experience|spells?\b|skills?\b|in-game description)",
    re.I,
)


def _dq9_section_nodes(soup: BeautifulSoup) -> List:
    heading = None
    for h in soup.find_all(re.compile(r"^h[2-4]$")):
        t = h.get_text(" ")
        if "Dragon Quest IX" in t and "Sentinels" in t:
            heading = h
            break
    if heading is None:
        return []
    level = int(heading.name[1])
    start = heading
    if heading.parent and heading.parent.name == "div" and "mw-heading" in (heading.parent.get("class") or []):
        start = heading.parent

    def is_stop(el) -> bool:
        if not isinstance(el, Tag):
            return False
        h = el if re.fullmatch(r"h[1-6]", el.name or "") else None
        if h is None and "mw-heading" in (el.get("class") or []):
            h = el.find(re.compile(r"^h[1-6]$"))
        return h is not None and int(h.name[1]) <= level

    nodes = []
    for sib in start.next_siblings:
        if is_stop(sib):
            break
        if isinstance(sib, Tag):
            nodes.append(sib)
    return nodes


def _following_lines(anchor) -> List[str]:
    text = "".join(flatten(s) for s in anchor.next_siblings)
    lines = []
    for line in _clean_lines(text):
        if _STOP_RE.match(line) and lines:
            break
        if _STOP_RE.match(line):
            return []
        lines.append(line)
    return lines


def parse_enemy_locations(html: str) -> List[str]:
    soup = BeautifulSoup(html, "html.parser")
    results: List[str] = []
    for node in _dq9_section_nodes(soup):
        for label in node.find_all(string=_LABEL_RE):
            anchor = label
            # step out of tiny wrappers like <b>Location(s)</b> / <th>...</th>
            for _ in range(4):
                lines = _following_lines(anchor)
                if lines:
                    break
                anchor = anchor.parent
                if anchor is None or anchor.name in ("body", "html", "[document]"):
                    break
            for line in lines:
                if line.lower() not in ("n/a", "-", "none") and line not in results:
                    results.append(line)
    return results


# --------------------------------------------------------------------------- recipes
_ING_KEYS = ("ingredient", "material", "component", "needed", "required", "requires")
_ITEM_KEYS = ("item", "name", "result", "product", "creates")
_TYPE_KEYS = ("type", "category")


def parse_ingredients(text: str) -> List[Tuple[str, int]]:
    out = []
    for part in re.split(r"\n|,|\+|;|&", text):
        part = re.sub(r"\s+", " ", part).strip(" .")
        if not part:
            continue
        m = re.match(r"^(\d+)\s*[x\u00d7]?\s+(.+)$", part, re.I)
        m2 = re.match(r"^(.+?)\s*[x\u00d7]\s*(\d+)$", part, re.I)
        if m:
            out.append((m.group(2).strip(), int(m.group(1))))
        elif m2:
            out.append((m2.group(1).strip(), int(m2.group(2))))
        else:
            out.append((part, 1))
    return out


def parse_recipes(html: str) -> List[Recipe]:
    soup = BeautifulSoup(html, "html.parser")
    recipes: List[Recipe] = []
    seen_headers: List[str] = []
    for table in soup.find_all("table"):
        rows = table.find_all("tr")
        if len(rows) < 2:
            continue
        header = [c.get_text(" ", strip=True).lower() for c in rows[0].find_all(["th", "td"])]
        seen_headers.append(" | ".join(header)[:120])

        def find(keys, exclude=()):
            for i, h in enumerate(header):
                if i not in exclude and any(k in h for k in keys):
                    return i
            return None

        i_type = find(_TYPE_KEYS)
        i_item = find(_ITEM_KEYS, exclude=(i_type,) if i_type is not None else ())
        ing_cols = [i for i, h in enumerate(header) if any(k in h for k in _ING_KEYS)]
        if i_item is None and not ing_cols:
            # no recognisable header: assume item | type | ingredients...
            i_item, i_type, ing_cols, first_data = 0, 1, [2], 0
        else:
            first_data = 1
            if i_item is None:
                i_item = 0
            if not ing_cols:
                known = {i_item, i_type}
                ing_cols = [i for i in range(len(header)) if i not in known][:1]
        for tr in rows[first_data:]:
            cells = tr.find_all(["td", "th"], recursive=False)
            if len(cells) <= max([i_item] + ing_cols):
                continue
            item = cells[i_item].get_text(" ", strip=True)
            ingredients: List[Tuple[str, int]] = []
            for c in ing_cols:
                ingredients += parse_ingredients(flatten(cells[c]))
            if not item or not ingredients:
                continue
            raw_type = cells[i_type].get_text(" ", strip=True) if i_type is not None and i_type < len(cells) else ""
            extra = {}
            used = {i_item, i_type, *ing_cols}
            for i, c in enumerate(cells):
                if i not in used and i < len(header) and header[i]:
                    v = c.get_text(" ", strip=True)
                    if v:
                        extra[header[i].title()] = v
            recipes.append(Recipe(item, canon_category(raw_type) if raw_type else "", ingredients, extra))
    if not recipes:
        hint = "; ".join(seen_headers[:5]) or "no tables at all"
        raise ParseError(
            "No recipes could be read from that page. Table headers seen: " + hint +
            ". If the recipes are loaded by JavaScript or revealed on click, a saved copy won't contain them."
        )
    return recipes


# --------------------------------------------------------------------------- recipe spreadsheet
# The spreadsheet has four column pairs (Weapon / Armor / Accessory / Item name + materials) but
# never says WHICH weapon or armour type an entry is. It does list each type as its own
# alphabetical run (all swords A-Z, then all spears A-Z, ...), so a drop backwards in the
# alphabet marks the start of a new type. Each run is then named by a vote over the keywords in
# its item names. That copes with names like "Fenrir fang" (a knife) or "Psyche swiper" (a
# shield), which no per-name keyword rule would get right.
_SUBTYPE_PATTERNS: Dict[str, Dict[str, str]] = {
    "Weapon": {
        "Swords": r"sword|blade\b|foil\b|rapier",
        "Spears": r"spear|lance|trident|poker|halberd|partisan",
        "Knives": r"dagger|knife|dirk|needle|\bfang\b|\btang\b",
        "Wands": r"wand|staff",
        "Whips": r"whip|sidewinder",
        "Staves": r"stick|\brod\b|\bbar\b|pillar|cosh|cudgel",
        "Claws": r"claw|handrill",
        "Fans": r"\bfan\b",
        "Axes": r"\baxe\b|climaxe",
        "Hammers": r"hammer|maul\b",
        "Boomerangs": r"boomerang|rang\b|wing\b",
        "Bows": r"\bbow\b|longbow",
    },
    "Armor": {
        "Shields": r"shield|platter",
        "Head": r"\bhat\b|helm|\bcap\b|crown|mask|turban|bandana|hood|circlet|tiara|\bears\b|"
                r"headband|headgear|coif",
        "Torso": r"armou?r|mail\b|robes?\b|dress|gown|raiment|cape\b|cloak|mantle|coat|poncho|"
                 r"vest|bustier|\btop\b|suit|tuxedo|shirt|tutu|toga|attire|cuirass",
        "Arms": r"glove|gauntlet|mitten|bindings?\b",
        "Legs": r"trouser|pants|tights|legging|kilt|briefs|bottoms|kneecap|socks",
        "Feet": r"boots?\b|shoes?\b|sandal|clogs?\b|heels?\b|pumps?\b|sabaton",
    },
}
_NAME_FIXES = {"agressence": "Aggressence"}   # typos in the spreadsheet's ingredient cells
_OTHER_LABEL = {"Weapon": "Other weapons", "Armor": "Other armour"}
_FIXED_LABEL = {"Accessory": "Accessories", "Item": ITEMS_CAT}
_GROUP_WORDS = {"weapon": "Weapon", "armor": "Armor", "armour": "Armor",
                "accessory": "Accessory", "item": "Item"}


def _sort_key(name: str) -> str:
    """Case-, accent- and spacing-insensitive key ("Über war hammer" files under U)."""
    decomposed = unicodedata.normalize("NFKD", name.lower())
    return "".join(c for c in decomposed if not unicodedata.combining(c)).strip()


def _alphabetical_runs(names: List[str]) -> List[List[int]]:
    runs: List[List[int]] = []
    prev: Optional[str] = None
    for i, name in enumerate(names):
        key = _sort_key(name)
        if prev is None or key < prev:
            runs.append([])
        runs[-1].append(i)
        prev = key
    return runs


def _label_runs(names: List[str], patterns: Dict[str, str]) -> List[Optional[str]]:
    """One subtype per name (None where a run's keywords give no clear winner)."""
    compiled = {cat: re.compile(rx, re.I) for cat, rx in patterns.items()}
    labels: List[Optional[str]] = [None] * len(names)
    for run in _alphabetical_runs(names):
        votes: Counter = Counter()
        for i in run:
            for cat, rx in compiled.items():
                if rx.search(names[i]):
                    votes[cat] += 1
        ranked = votes.most_common(2)
        if ranked and (len(ranked) == 1 or ranked[0][1] > ranked[1][1]):
            for i in run:
                labels[i] = ranked[0][0]
    return labels


def parse_sheet_ingredients(text: str) -> List[Tuple[str, int]]:
    """'Cautery sword (1)\\n Brighten rock (3)' -> [('Cautery sword', 1), ('Brighten rock', 3)].
    A line without a (quantity) counts as 1."""
    out = []
    for line in str(text).split("\n"):
        line = re.sub(r"\s+", " ", line.replace(" ", " ")).strip()
        if not line:
            continue
        m = re.match(r"^(.*?)\s*\((\d+)\)\s*$", line)
        out.append((m.group(1).strip(), int(m.group(2))) if m else (line, 1))
    return out


def _group_of(header) -> Optional[str]:
    m = re.match(r"^(\w+)\s+name$", re.sub(r"\s+", " ", str(header or "").strip().lower()))
    return (_GROUP_WORDS.get(m.group(1)) or m.group(1).title()) if m else None


def recipes_from_columns(columns: Dict[str, List[Tuple[str, str]]]) -> List[Recipe]:
    """columns: group ("Weapon", "Armor", ...) -> [(item name, materials text)] in sheet order."""
    out: List[Recipe] = []
    for group, rows in columns.items():
        rows = [(str(n).strip(), m) for n, m in rows if n is not None and str(n).strip()]
        if group in _SUBTYPE_PATTERNS:
            labels = _label_runs([n for n, _ in rows], _SUBTYPE_PATTERNS[group])
        else:
            labels = [None] * len(rows)
        for (name, mats), label in zip(rows, labels):
            ingredients = [(_NAME_FIXES.get(n.lower(), n), q) for n, q in parse_sheet_ingredients(mats)] if mats else []
            if not ingredients:
                continue
            category = label or _FIXED_LABEL.get(group) or _OTHER_LABEL.get(group) or group
            out.append(Recipe(name, category, ingredients, {}, "spreadsheet"))
    return out


def parse_recipe_sheet(path: str) -> List[Recipe]:
    try:
        import openpyxl
    except ImportError as e:
        raise ParseError("Reading an .xlsx file needs the openpyxl package. Install it with:\n"
                         "    python -m pip install openpyxl") from e
    wb = openpyxl.load_workbook(path, data_only=True)
    columns: Dict[str, List[Tuple[str, str]]] = {}
    first_rows: List[str] = []
    for ws in wb.worksheets:
        rows = [list(r) for r in ws.iter_rows(values_only=True)]
        for h, header in enumerate(rows[:10]):
            found = {}
            for c, cell in enumerate(header):
                group = _group_of(cell)
                nxt = str(header[c + 1] or "").lower() if c + 1 < len(header) else ""
                if group and ("material" in nxt or "ingredient" in nxt):
                    found[c] = group
            if not found:
                continue
            for c, group in found.items():
                for row in rows[h + 1:]:
                    name = row[c] if c < len(row) else None
                    mats = row[c + 1] if c + 1 < len(row) else None
                    if name is not None and str(name).strip():
                        columns.setdefault(group, []).append((name, "" if mats is None else str(mats)))
            break
        else:
            first_rows.append(" | ".join(str(x) for x in (rows[0] if rows else []) if x is not None)[:100])
    recipes = recipes_from_columns(columns)
    if not recipes:
        raise ParseError(
            "No recipes found. The spreadsheet needs column pairs headed like \"Weapon Name\" / "
            "\"Materials Required\". First row(s) seen: " + "; ".join(first_rows or ["(empty)"]))
    return recipes



# --------------------------------------------------------------------------- item pages (dqnine.wikidot.com)
_DETAIL_KNOWN = ["Classification", "Used by", "Attack", "Defence", "Defense", "Style", "Special",
                 "Rarity", "Buy price", "Sell price", "How to make", "Where to find",
                 "Can be used for"]
_DETAIL_SUB = {"Use", "Equip", "Note", "Notes", "Tip", "Warning"}   # sub-labels inside a value
_DETAIL_RX = re.compile(r"^([A-Z][A-Za-z' ]{1,24}?)\s*:\s*(.*)$")
DETAIL_SHOWN = ["Classification", "Used by", "Attack", "Defence", "Defense", "Style", "Special",
                "Rarity", "Buy price", "Sell price", "Where to find", "Can be used for"]


def dkey(name: str) -> str:
    """Cache key for item details: exact name (so Tortoiseshell and Tortoise shell stay apart)."""
    return re.sub(r"\s+", " ", name.strip().lower())


def wikidot_slug(name: str) -> str:
    plain = unicodedata.normalize("NFKD", name.lower())
    plain = "".join(c for c in plain if not unicodedata.combining(c))
    return re.sub(r"[^a-z0-9]+", "-", plain).strip("-")


def parse_wiki_index(html: str) -> Dict[str, str]:
    """Item name -> page URL, from the links on the wiki's alchemy page."""
    soup = BeautifulSoup(html, "html.parser")
    root = soup.find(id="page-content") or soup
    out: Dict[str, str] = {}
    for a in root.find_all("a", href=True):
        text = re.sub(r"\s+", " ", a.get_text()).strip()
        href = a["href"]
        if not text or href.startswith(("#", "javascript", "mailto")):
            continue
        url = urljoin(WIKIDOT + "/", href)
        if urlparse(url).netloc == urlparse(WIKIDOT).netloc:
            out.setdefault(dkey(text), url)
    return out


def parse_item_page(html: str, name: str = "") -> Optional[dict]:
    """{'description': str, 'fields': [[label, value], ...]} from an item page, or None.
    Works on the page's text (bold "Label: value" lines), so it doesn't depend on exact markup."""
    soup = BeautifulSoup(html, "html.parser")
    root = soup.find(id="page-content") or soup.body or soup
    for t in root.find_all(["script", "style"]):
        t.decompose()
    for cell in root.find_all(["td", "th"]):
        cell.append("\n")   # keep a table's label and value on separate lines
    desc: List[str] = []
    fields: List[List[str]] = []
    for line in _clean_lines(flatten(root)):
        if not fields and dkey(line) == dkey(name):
            continue   # the page title repeated in the body
        m = _DETAIL_RX.match(line)
        label, value = (m.group(1).strip(), m.group(2).strip()) if m else (line.rstrip(":").strip(), "")
        if (m and label not in _DETAIL_SUB and len(label.split()) <= 3) or \
                (not m and label in _DETAIL_KNOWN):
            fields.append([label, value])
            continue
        if fields:
            fields[-1][1] = (fields[-1][1] + "; " + line).strip("; ")
        else:
            desc.append(line)
    if not fields:
        return None
    for f in fields:
        f[1] = f[1][:400]
    return {"description": " ".join(desc)[:600], "fields": fields}



# --------------------------------------------------------------------------- sprites (dragon-quest.org)
def skey(name: str) -> str:
    """Sprite-index key: lowercase, curly quotes straightened, spaces collapsed."""
    return re.sub(r"\s+", " ", name.replace("\u2019", "'").strip().lower())


def _sprite_name(img: Tag) -> str:
    """Item name behind an icon: its alt text, else the link title, else the file name."""
    parent = img.find_parent("a")
    raw = (img.get("alt") or "").strip() or (parent.get("title", "").strip() if parent else "")
    if not raw:
        src = unquote(urlparse(img.get("src") or img.get("data-src") or "").path)
        parts = [x for x in src.split("/") if x]
        raw = parts[-2] if "thumb" in parts and len(parts) >= 2 else (parts[-1] if parts else "")
    raw = re.sub(r"^(File|Image):", "", raw, flags=re.I)
    raw = re.sub(r"\.(png|gif|jpe?g)$", "", raw, flags=re.I).replace("_", " ")
    raw = re.sub(r"\s*\b(DQIX|DQ9)\b\s*(icon|sprite)?\s*$", "", raw, flags=re.I)
    raw = re.sub(r"\s+(icon|sprite)$", "", raw, flags=re.I)
    return raw.strip()


def _full_image_url(src: str) -> str:
    """Thumbnail URLs (/w/images/thumb/d/d1/Name.png/32px-Name.png) -> the original file."""
    url = urljoin(SPRITE_URL, src)
    m = re.match(r"^(.*/images)/thumb/([0-9a-f]/[0-9a-f]{2}/[^/]+)/[^/]+$", url)
    return f"{m.group(1)}/{m.group(2)}" if m else url


def parse_sprites(html: str) -> Dict[str, str]:
    """{skey(item name): image URL} for every icon on the alchemy-ingredients page."""
    soup = BeautifulSoup(html, "html.parser")
    root = soup.find(id="mw-content-text") or soup.find(id="content") or soup
    out: Dict[str, str] = {}
    for img in root.find_all("img"):
        src = img.get("data-src") or img.get("src")
        if not src or src.startswith("data:"):
            continue
        name = _sprite_name(img)
        if name and re.search(r"\.(png|gif|jpe?g)$", urlparse(src).path, re.I):
            out.setdefault(skey(name), _full_image_url(src))
    return out


# --------------------------------------------------------------------------- HTTP + cache
class Http:
    UA = "DQ9AlchemyHelper/1.0 (personal-use desktop tool)"
    MIN_GAP = 1.0

    def __init__(self):
        self.session = requests.Session()
        self.session.headers["User-Agent"] = self.UA
        self._robots: Dict[str, RobotFileParser] = {}
        self._lock = threading.Lock()
        self._last = 0.0
        self._img_lock = threading.Lock()
        self._img_last = 0.0

    def _robots_for(self, url: str) -> RobotFileParser:
        p = urlparse(url)
        base = f"{p.scheme}://{p.netloc}"
        if base not in self._robots:
            rp = RobotFileParser()
            r = self.session.get(base + "/robots.txt", timeout=20)
            if r.status_code in (401, 403):
                rp.disallow_all = True
            elif 400 <= r.status_code < 500:
                rp.allow_all = True
            elif r.ok:
                rp.parse(r.text.splitlines())
            else:
                r.raise_for_status()
            self._robots[base] = rp
        return self._robots[base]

    def allowed(self, url: str) -> bool:
        return self._robots_for(url).can_fetch(self.UA, url)

    def get(self, url: str) -> str:
        if not self.allowed(url):
            raise RobotsDisallowed(url)
        with self._lock:  # one request at a time, >= MIN_GAP seconds apart
            wait = self.MIN_GAP - (time.time() - self._last)
            if wait > 0:
                time.sleep(wait)
            try:
                r = self.session.get(url, timeout=30)
            finally:
                self._last = time.time()
        r.raise_for_status()
        r.encoding = r.encoding or "utf-8"
        return r.text


    def get_bytes(self, url: str, gap: float = 0.5) -> bytes:
        """Binary download (sprites). Same robots.txt rule; static files get a shorter pause."""
        if not self.allowed(url):
            raise RobotsDisallowed(url)
        with self._img_lock:
            wait = gap - (time.time() - self._img_last)
            if wait > 0:
                time.sleep(wait)
            try:
                r = self.session.get(url, timeout=30)
            finally:
                self._img_last = time.time()
        r.raise_for_status()
        return r.content


class Store:
    def __init__(self, app_dir: Path = APP_DIR, http: Optional[Http] = None):
        self.dir = Path(app_dir)
        self.dir.mkdir(parents=True, exist_ok=True)
        self.http = http or Http()
        self._loc_lock = threading.Lock()
        self.loc_cache: Dict[str, List[str]] = self._read("locations.json", {})
        self.detail_cache: Dict[str, dict] = self._read("details.json", {})
        self._index: Optional[Dict[str, str]] = None
        self.sprite_index: Dict[str, str] = {}
        self._sprite_norm: Dict[str, str] = {}
        self._index_lock = threading.Lock()

    def _read(self, name, default):
        try:
            return json.loads((self.dir / name).read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return default

    def _write(self, name, data):
        (self.dir / name).write_text(json.dumps(data, indent=1, ensure_ascii=False), encoding="utf-8")

    # enemies ---------------------------------------------------------------
    def load_enemies(self, force: bool = False) -> List[Enemy]:
        cached = self._read("enemies.json", None)
        if cached and not force:
            return [Enemy.from_dict(d) for d in cached]
        enemies = parse_enemy_list(self.http.get(ENEMY_URL))
        self._write("enemies.json", [asdict(e) for e in enemies])
        return enemies

    # recipes ---------------------------------------------------------------
    def load_recipes(self, force: bool = False) -> Tuple[List[Recipe], str]:
        cached = self._read("recipes.json", None)
        if cached and not force:
            return [Recipe.from_dict(d) for d in cached], ""
        try:
            recipes = parse_recipes(self.http.get(RECIPE_URL))
        except RobotsDisallowed:
            if cached:
                return [Recipe.from_dict(d) for d in cached], "robots"
            raise
        self._write("recipes.json", [asdict(r) for r in recipes])
        return recipes, ""

    def import_recipes_file(self, path: str) -> List[Recipe]:
        html = Path(path).read_text(encoding="utf-8", errors="replace")
        recipes = parse_recipes(html)
        self._write("recipes.json", [asdict(r) for r in recipes])
        return recipes

    # spreadsheet recipes ---------------------------------------------------
    def load_sheet_recipes(self) -> List[Recipe]:
        """Your imported copy (in ~/.dq9_helper) if there is one, else the parsed copy shipped
        next to this file."""
        for path in (self.dir / "sheet_recipes.json", BUNDLED_SHEET):
            try:
                raw = json.loads(path.read_text(encoding="utf-8"))
            except (OSError, ValueError):
                continue
            return [Recipe.from_dict(d) for d in raw]
        return []

    def import_sheet_file(self, path: str) -> List[Recipe]:
        recipes = parse_recipe_sheet(path)
        self._write("sheet_recipes.json", [asdict(r) for r in recipes])
        return recipes

    def cached_recipes(self) -> List[Recipe]:
        """Every recipe available without touching the network (woodus cache + spreadsheet)."""
        raw = self._read("recipes.json", None) or []
        return [Recipe.from_dict(d) for d in raw] + self.load_sheet_recipes()


    # sprites ---------------------------------------------------------------
    def load_sprite_index(self, force: bool = False) -> Dict[str, str]:
        """name -> image URL, from dragon-quest.org's alchemy-ingredients page (one request, cached)."""
        cached = self._read("sprites.json", None)
        if cached and not force:
            self.sprite_index = cached
        else:
            self.sprite_index = parse_sprites(self.http.get(SPRITE_URL))
            self._write("sprites.json", self.sprite_index)
        self._sprite_norm = {}
        clash = set()
        for k in self.sprite_index:
            n = norm(k)
            if n in self._sprite_norm:
                clash.add(n)
            self._sprite_norm[n] = k
        for n in clash:
            del self._sprite_norm[n]   # ambiguous (e.g. Tortoiseshell / Tortoise shell): exact name only
        return self.sprite_index

    def _sprite_key(self, name: str) -> Optional[str]:
        k = skey(name)
        if k in self.sprite_index:
            return k
        return self._sprite_norm.get(norm(name))

    def has_sprite(self, name: str) -> bool:
        return self._sprite_key(name) is not None

    def _sprite_file(self, url: str) -> Path:
        ext = Path(urlparse(url).path).suffix.lower() or ".png"
        return self.dir / "sprites" / (hashlib.sha1(url.encode()).hexdigest()[:16] + ext)

    def sprite_path(self, name: str) -> Optional[Path]:
        """The cached image file for an item, if we already have it."""
        k = self._sprite_key(name)
        if k is None:
            return None
        f = self._sprite_file(self.sprite_index[k])
        return f if f.exists() else None

    def fetch_sprite(self, name: str) -> Optional[Path]:
        k = self._sprite_key(name)
        if k is None:
            return None
        url = self.sprite_index[k]
        f = self._sprite_file(url)
        if not f.exists():
            data_ = self.http.get_bytes(url)
            f.parent.mkdir(exist_ok=True)
            f.write_bytes(data_)
        return f

    # item details (dqnine.wikidot.com) -------------------------------------
    def cached_details(self, name: str) -> Optional[dict]:
        with self._loc_lock:
            return self.detail_cache.get(dkey(name))

    def item_details(self, name: str) -> Optional[dict]:
        """Description, sell price, effects... for one item (cached; one request per new item)."""
        hit = self.cached_details(name)
        if hit is not None:
            return hit or None
        index = self._wiki_index()
        url = index.get(dkey(name)) or f"{WIKIDOT}/{wikidot_slug(name)}"
        try:
            html = self.http.get(url)
            result = parse_item_page(html, name)
        except requests.HTTPError as e:
            if e.response is not None and e.response.status_code == 404:
                result = None   # no such page: remember that, don't ask again
            else:
                raise
        with self._loc_lock:
            self.detail_cache[dkey(name)] = result or {}
            self._write("details.json", self.detail_cache)
        return result

    def _wiki_index(self) -> Dict[str, str]:
        with self._index_lock:
            if self._index is None:
                cached = self._read("wiki_index.json", None)
                if cached:
                    self._index = cached
                else:
                    self._index = parse_wiki_index(self.http.get(WIKIDOT_INDEX))
                    self._write("wiki_index.json", self._index)
            return self._index

    def clear_details(self):
        with self._loc_lock:
            self.detail_cache = {}
            self._write("details.json", {})
        self._index = None
        try:
            (self.dir / "wiki_index.json").unlink()
        except OSError:
            pass

    # locations -------------------------------------------------------------
    def cached_locations(self, enemy: Enemy) -> Optional[List[str]]:
        with self._loc_lock:
            return self.loc_cache.get(enemy.url)

    def locations(self, enemy: Enemy) -> List[str]:
        hit = self.cached_locations(enemy)
        if hit is not None:
            return hit
        html = self.http.get(enemy.url)
        locs = parse_enemy_locations(html)
        if not locs:  # keep the page so a parser problem can be diagnosed
            dbg = self.dir / "debug"
            dbg.mkdir(exist_ok=True)
            (dbg / (re.sub(r"[^A-Za-z0-9_-]", "_", enemy.name) + ".html")).write_text(html, encoding="utf-8")
            return []
        with self._loc_lock:
            self.loc_cache[enemy.url] = locs
            self._write("locations.json", self.loc_cache)
        return locs

    # shop data: bundled list (dqnine.wikidot.com/shops) plus an optional user file -------
    def _bundled_shops(self) -> dict:
        try:
            return json.loads(BUNDLED_SHOPS.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return {}

    def shops(self) -> Dict[str, List[str]]:
        """item key -> ["Town (Shop)", ...]. ~/.dq9_helper/shops.json adds to the bundled list."""
        def key(k):
            return k.lower() if "~" in k else norm(k)   # "~" keys are internal (see _item_for)
        out: Dict[str, List[str]] = {}
        user = self._read("shops.json", {})
        for raw in (self._bundled_shops().get("shops", {}), user):
            for k, v in raw.items():
                for label in ([v] if isinstance(v, str) else list(v)):
                    lst = out.setdefault(key(k), [])
                    if label not in lst:
                        lst.append(label)
        return out

    def shop_items(self) -> Dict[str, str]:
        """Every item name the bundled shop list sells -> a category hint ("" if none)."""
        b = self._bundled_shops()
        hints = b.get("hints", {})
        return {name: hints.get(name) or "" for name in b.get("shops", {})}


# --------------------------------------------------------------------------- cross-reference
@dataclass
class ItemInfo:
    key: str
    name: str
    category: str
    recipes: List[Recipe] = field(default_factory=list)
    droppers: List[Tuple[Enemy, Drop]] = field(default_factory=list)


_SHOP_EXTRA: Dict[str, Dict[str, str]] = {   # words only the shop-only names need
    "Weapon": {"Staves": r"pole\b|mop\b|wrench|stick", "Wands": r"wand|staff|rod\b",
               "Hammers": r"hammer|mace\b|tamper", "Spears": r"fork\b", "Swords": r"dragonsbane"},
    "Armor": {
        "Shields": r"\blid\b",
        "Head": r"headwear|ribbon|\bband\b|bonnet|topper|shades|specs|afro|moustache|veil|cosy|"
                r"mitre|sweatband|hairband|\bhat\b|\bcap\b|pumpkin head|skull cap|\bears\b",
        "Arms": r"mitts|handwear|fingerwear|bracers?\b|armguard|bracelets",
        "Legs": r"skirt|slacks|jeans|knickers|shorts|britches|pantaloons|stockings|trousers|bottoms",
        "Feet": r"geta|footwear|sollerets|wellies|wellington|steppers|sneakers|wing-tips|"
                r"agiliboots|acroboots|sandals|shoes|boots",
        "Torso": r"garb|costume|outfit|clothes|t-shirt|shirt|blazer|jaguarment|midriff|bikini|"
                 r"\btop\b|suit|smart suit|dress|cuirass|mail\b",
    },
}
_ACCESSORY_RX = re.compile(r"\bring\b|earrings?|bracer\b|bow tie|\bbracelets?\b", re.I)


def shop_category(name: str, hint: str = "") -> str:
    """Category of an item that only appears in the shop list. `hint` is a category name, or
    "@Weapon" / "@Armor" / "@Both" when all we know is which kind of shop sold it."""
    if hint and not hint.startswith("@"):
        return hint
    groups = {"@Weapon": ["Weapon"], "@Armor": ["Armor"], "@Both": ["Weapon", "Armor"]}.get(
        hint, ["Weapon", "Armor"])
    for table in (_SUBTYPE_PATTERNS, _SHOP_EXTRA):
        for g in groups:
            for cat, rx in table[g].items():
                if re.search(rx, name, re.I):
                    return cat
    if _ACCESSORY_RX.search(name):
        return "Accessories"
    if hint == "@Weapon":
        return _OTHER_LABEL["Weapon"]
    if hint in ("@Armor", "@Both"):
        return _OTHER_LABEL["Armor"]
    return ITEMS_CAT


class Registry:
    """Joins the enemy drop table with the recipe list, matching item names across sites."""

    def __init__(self, enemies: List[Enemy], recipes: List[Recipe],
                 shop_items: Optional[Dict[str, str]] = None):
        self.enemies = enemies
        self.recipes = recipes
        self.shop_items = shop_items or {}
        self.items: Dict[str, ItemInfo] = {}
        self.alias: Dict[str, str] = {}
        self._kinds: Dict[str, Counter] = {}
        self._names: Dict[str, Counter] = {}
        self._cat_prio: Dict[str, int] = {}
        self._build()

    @property
    def recipe_count(self) -> int:
        """Recipes after de-duplicating the sources."""
        return sum(len(i.recipes) for i in self.items.values())

    def _add_alias(self, name: str, key: str):
        n = norm(name)
        if n:
            self.alias.setdefault(n, key)

    def resolve(self, name: str) -> Optional[ItemInfo]:
        key = self.alias.get(norm(name))
        return self.items[key] if key else None

    def _item_for(self, r: Recipe) -> ItemInfo:
        """The item a recipe makes, creating it if neither source knew it before."""
        info = self.resolve(r.item)
        if info is None:
            key = norm(r.item)
            info = self.items.setdefault(key, ItemInfo(key, r.item, r.category or ITEMS_CAT))
            self._add_alias(r.item, key)
            return info
        if (r.category and r.category != ITEMS_CAT and info.category == ITEMS_CAT
                and info.key == norm(r.item)
                and re.sub(r"\s+", " ", r.item.strip().lower()) != re.sub(r"\s+", " ", info.name.strip().lower())):
            # Same letters once spaces are ignored, but a different thing: the wiki's
            # "Tortoiseshell" (an ingredient) vs the spreadsheet's "Tortoise shell" (a torso
            # armour). Keep them apart; the name alias keeps pointing at the ingredient.
            skey = info.key + "~equip"
            return self.items.setdefault(skey, ItemInfo(skey, r.item, r.category))
        return info

    def _build(self):
        for enemy in self.enemies:
            for d in enemy.drops:
                key = norm(d.item)
                if not key:
                    continue
                if key not in self.items:
                    self.items[key] = ItemInfo(key, d.item, "")
                    self._kinds[key] = Counter()
                    self._names[key] = Counter()
                info = self.items[key]
                info.droppers.append((enemy, d))
                self._kinds[key][canon_category(d.kind)] += 1
                self._names[key][d.item] += 1
                self._add_alias(d.item, key)
                if d.title and _similar(d.item, d.title):
                    self._add_alias(d.title, key)
        for key, info in self.items.items():
            info.category = self._kinds[key].most_common(1)[0][0]
            info.name = self._names[key].most_common(1)[0][0]
            info.droppers.sort(key=lambda t: (-t[1].prob, t[0].number))

        for r in self.recipes:
            info = self._item_for(r)
            src = r.source or "woodus"
            same = next((x for x in info.recipes if _same_ingredients(x.ingredients, r.ingredients)), None)
            if same:  # the same recipe from another source: merge instead of listing it twice
                same.source = ", ".join(sorted(set(same.source.split(", ")) | {src}))
                same.category = same.category or r.category
                same.extra = {**r.extra, **same.extra}
            else:
                if not info.recipes:
                    info.name = r.item  # recipe sources' spelling is the likelier in-game name
                info.recipes.append(replace(r, source=src, extra=dict(r.extra)))
            # Category: wiki icon < spreadsheet's block-derived type < woodus' own type label.
            prio = 1 if src == "spreadsheet" else 2
            if r.category and prio >= self._cat_prio.get(info.key, 0):
                info.category = r.category
                self._cat_prio[info.key] = prio

        # Things only a shop sells (no drop, no recipe) still need a place in the lists.
        for name, hint in self.shop_items.items():
            if "~" in name or self.resolve(name):
                continue
            key = norm(name)
            self.items[key] = ItemInfo(key, name, shop_category(name, hint))
            self._add_alias(name, key)

    def categories(self) -> List[str]:
        present = {i.category for i in self.items.values() if i.category}
        ordered = [c for c in EQUIP_ORDER if c in present]
        rest = sorted(present - set(EQUIP_ORDER) - {ITEMS_CAT})
        return ordered + rest + ([ITEMS_CAT] if ITEMS_CAT in present else [])

    def items_in(self, category: str, text: str = "") -> List[ItemInfo]:
        t = text.strip().lower()
        res = [i for i in self.items.values() if i.category == category and t in i.name.lower()]
        return sorted(res, key=lambda i: i.name.lower())

    def droppers_of(self, name: str) -> List[Tuple[Enemy, Drop]]:
        info = self.resolve(name)
        return info.droppers if info else []


# --------------------------------------------------------------------------- rendering
LocFn = Callable[[Enemy], Tuple[str, List[str]]]   # -> (state, locations); state: ok|loading|unknown|error


def _locs_html(state: str, locs: List[str]) -> str:
    if state == "ok":
        return "; ".join(esc(x) for x in locs)
    if state == "error":
        return "<i style='color:#c0392b'>couldn't load</i>"
    if state == "unknown":
        return "<i style='color:#888'>not listed on the wiki page</i>"
    return "<i style='color:#888'>looking up&hellip;</i>"


def _monster_table(rows, per_n: int, loc_fn: LocFn, needed: list) -> str:
    shown = rows[:per_n]
    out = ["<table border='1' cellspacing='0' cellpadding='4' width='100%'>"
           "<tr><th align='left'>Monster</th><th align='left'>Drop odds</th><th align='left'>Where</th></tr>"]
    for enemy, drop in shown:
        needed.append(enemy)
        state, locs = loc_fn(enemy)
        out.append(f"<tr><td>{esc(enemy.name)}</td><td>{esc(drop.odds_text)}</td><td>{_locs_html(state, locs)}</td></tr>")
    out.append("</table>")
    if len(rows) > per_n:
        out.append(f"<p><small>+{len(rows) - per_n} more monsters drop this. "
                   f"Raise &quot;Monsters shown&quot; to see them.</small></p>")
    return "".join(out)


MAX_NEST = 6   # how many levels of "this ingredient is itself crafted" to unfold


def _recipe_html(reg: Registry, r: Recipe, per_n: int, loc_fn: LocFn, shops: Dict[str, List[str]],
                 needed: list, path: frozenset, depth: int) -> str:
    """One recipe's ingredient list, then where to get each ingredient: shop, monster drops, or
    (unfolded underneath) its own recipe. `path` holds the items already being unfolded, so a
    recipe that loops back on itself stops instead of recursing forever."""
    h = ["<p><b>Ingredients:</b> " + ", ".join(f"{q} &times; {esc(nm)}" for nm, q in r.ingredients) + "</p>"]
    if depth == 0:
        if r.extra:
            h.append("<p>" + " &nbsp;|&nbsp; ".join(f"{esc(k)}: {esc(v)}" for k, v in r.extra.items()) + "</p>")
        if r.source:
            h.append(f"<p><small style='color:#888'>Source: {esc(r.source)}</small></p>")
    for nm, q in r.ingredients:
        ing = reg.resolve(nm)
        key = ing.key if ing else norm(nm)
        title = f"{q} &times; {esc(nm)}"
        h.append(f"<h4>{title}</h4>" if depth == 0 else f"<p><b>{title}</b></p>")
        ing_shops = shops.get(norm(nm)) or (shops.get(ing.key) if ing else None)
        if ing_shops:
            h.append("<p><b>Buy:</b> " + esc("; ".join(ing_shops)) + "</p>")
        if ing and ing.droppers:
            h.append(_monster_table(ing.droppers, per_n, loc_fn, needed))
        if ing and ing.recipes:
            if key in path:
                h.append("<p><i>(Its recipe loops back to an item already shown above.)</i></p>")
            elif depth >= MAX_NEST:
                h.append("<p><i>Can be made by alchemy. Select it in the list to see how.</i></p>")
            else:
                more = len(ing.recipes) - 1
                note = f" (showing 1 of {len(ing.recipes)} recipes; select it in the list for the rest)" if more else ""
                h.append(f"<p><i>Can be made by alchemy{note}:</i></p><blockquote>")
                h.append(_recipe_html(reg, ing.recipes[0], per_n, loc_fn, shops, needed, path | {key}, depth + 1))
                h.append("</blockquote>")
        elif not ing_shops and not (ing and ing.droppers):
            h.append("<p><i>No monster on the enemy list drops this and no recipe makes it. "
                     "It may be found in the world (chests, shiny spots, quests).</i></p>")
    return "".join(h)


def _details_html(state: str, det: Optional[dict]) -> str:
    if state == "loading":
        return "<p><small><i>Loading item details...</i></small></p>"
    if not det:
        return ""
    h = []
    if det.get("description"):
        h.append(f"<p><i>{esc(det['description'])}</i></p>")
    rows = {k: v for k, v in det.get("fields", []) if v}
    body = "".join(f"<tr><td><b>{esc(k)}</b>&nbsp;&nbsp;</td><td>{esc(rows[k])}</td></tr>"
                   for k in DETAIL_SHOWN if k in rows)
    if body:
        h.append(f"<table cellspacing='2'>{body}</table>")
    return "".join(h)


def render_item_html(reg: Registry, info: ItemInfo, per_n: int, loc_fn: LocFn,
                     shops: Dict[str, List[str]], detail_fn=None) -> Tuple[str, List[Enemy]]:
    """detail_fn(name) -> ("ok"|"loading"|"none", dict|None) supplies the wiki's item facts."""
    needed: List[Enemy] = []
    h = [f"<h2>{esc(info.name)}</h2><p><b>{esc(info.category)}</b></p>"]
    if detail_fn:
        h.append(_details_html(*detail_fn(info.name)))

    own_shops = shops.get(info.key)
    if own_shops:
        h.append("<h3>Buy it</h3><ul>" + "".join(f"<li>{esc(s)}</li>" for s in own_shops) + "</ul>")

    if info.recipes:
        h.append("<h3>Alchemy</h3>")
        for n, r in enumerate(info.recipes, 1):
            if len(info.recipes) > 1:
                h.append(f"<h4>Recipe {n}</h4>")
            h.append(_recipe_html(reg, r, per_n, loc_fn, shops, needed, frozenset({info.key}), 0))

    if info.droppers:
        h.append("<h3>Dropped by monsters</h3>")
        h.append(_monster_table(info.droppers, per_n, loc_fn, needed))

    if not info.recipes and not info.droppers and not own_shops:
        h.append("<p><i>No alchemy recipe or monster drop found for this item.</i></p>")
    if not own_shops:
        h.append("<p><small>Not sold in any town shop on the shop list.</small></p>")
    return "".join(h), needed


def render_enemy_html(enemy: Enemy, loc_fn: LocFn) -> Tuple[str, List[Enemy]]:
    """Info popup for one monster: stats, where it lives, everything it can drop."""
    state, locs = loc_fn(enemy)
    h = [f"<h2>{esc(enemy.name)}</h2>"]
    stats = [f"{k}: {esc(v)}" for k, v in (("EXP", enemy.exp), ("Gold", enemy.gold)) if v]
    if stats:
        h.append("<p>" + " &nbsp;|&nbsp; ".join(stats) + "</p>")
    h.append("<h3>Where to find it</h3><p>" + _locs_html(state, locs) + "</p>")
    if enemy.drops:
        h.append("<h3>Drops</h3><table border='1' cellspacing='0' cellpadding='4'>"
                 "<tr><th align='left'>Item</th><th align='left'>Type</th><th align='left'>Odds</th></tr>")
        for d in sorted(enemy.drops, key=lambda x: -x.prob):
            h.append(f"<tr><td>{esc(d.item)}</td><td>{esc(d.kind)}</td><td>{esc(d.odds_text)}</td></tr>")
        h.append("</table>")
    if enemy.url:
        h.append(f"<p><small>{esc(enemy.url)}</small></p>")
    return "".join(h), [enemy]
