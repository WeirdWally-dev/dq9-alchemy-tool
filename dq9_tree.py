"""
Turns an item into a left-to-right branching tree of "bubbles" (no Qt in here, so it can be
tested on its own):  item -> its recipe(s) -> ingredients -> (their recipes ...) -> monsters / shops.
"""
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional

import dq9_data as data

NODE_W, NODE_H, GAP_X, GAP_Y = 176, 54, 64, 14
MAX_NODES = 700   # stop unfolding sub-recipes past this, so a huge chain stays drawable

# kind: item (the thing itself) | craft (ingredient made by alchemy) | base (ingredient not made by
# alchemy) | recipe (one of several recipes) | monster | shop | found (quest reward, chest, ...) |
# more | unknown
KINDS = ("item", "craft", "base", "recipe", "monster", "shop", "found", "more", "unknown")


@dataclass
class Node:
    kind: str
    label: str
    sub: str = ""
    edge: str = ""                     # text on the line coming into this bubble ("x3", "drop", ...)
    key: Optional[str] = None          # registry item this bubble opens on double-click
    enemy: Optional[data.Enemy] = None  # monster bubbles open the monster's page
    tip: str = ""
    sprite: str = ""                   # item name to look up an icon for ("" = none)
    more: int = 0                      # monsters beyond the ones drawn (a "+" badge is shown if > 0)
    more_key: Optional[str] = None     # item whose full monster list the badge opens
    children: List["Node"] = field(default_factory=list)
    x: float = 0.0
    y: float = 0.0


def build_tree(reg: data.Registry, info: data.ItemInfo, per_n: int, shops: Dict[str, List[str]],
               prefer: str = "alchemy", found_fn: Optional[Callable[[str], List[str]]] = None) -> Node:
    """prefer: "alchemy" or "drops". An item that has both a recipe and monster drops shows only
    the preferred one (an item with just one of them shows that one). Shops always add a leaf.
    found_fn(name) -> extra ways to get an item (its wiki page's "Where to find", quest rewards).
    It is asked about the chosen item and about anything that would otherwise have no known source,
    so the GUI only looks up the pages that matter."""
    count = [0]

    def extra_found(name: str, known: List[str], need: bool) -> List[str]:
        if not (found_fn and need):
            return list(known)
        return list(known) + [f for f in found_fn(name) if f not in known]

    def found_leaf(found: List[str], key: Optional[str]) -> Node:
        label = "Found" if len(found) == 1 else f"Found ({len(found)} ways)"
        return new("found", label, found[0], edge="get", key=key, tip="\n".join(found))

    def new(*a, **k) -> Node:
        count[0] += 1
        return Node(*a, **k)

    def source_of(it: data.ItemInfo) -> str:
        if it.recipes and (prefer != "drops" or not it.droppers):
            return "alchemy"
        return "drops" if it.droppers else ""

    def item_sources(node: Node, it: data.ItemInfo, path: frozenset, depth: int):
        """Hang the one way (per `prefer`) to get `it` under `node`, plus its shops."""
        src = source_of(it)
        if src == "alchemy":
            if depth > data.MAX_NEST or count[0] >= MAX_NODES:
                node.sub = "(recipe not unfolded)"
            else:
                recipes = it.recipes
                for n, r in enumerate(recipes, 1):
                    parent = node
                    if len(recipes) > 1:
                        parent = new("recipe", f"Recipe {n}", r.source, key=it.key)
                        node.children.append(parent)
                    for nm, q in r.ingredients:
                        parent.children.append(ingredient(nm, q, path | {it.key}, depth + 1, ""))
        elif src == "drops":
            for enemy, drop in it.droppers[:per_n]:
                node.children.append(new("monster", enemy.name, drop.odds_text, edge="drop",
                                         enemy=enemy, tip=f"{enemy.name}: {drop.odds_text}"))
            hidden = len(it.droppers) - per_n
            if hidden > 0 and node.children:
                last = node.children[-1]
                last.more, last.more_key = hidden, it.key
                last.tip += f"\n+{hidden} more monster{'s' if hidden > 1 else ''} also drop this " \
                            f"(click the + or raise 'Monsters shown')"
        labels = shops.get(it.key)
        if labels:
            node.children.append(new("shop", f"Shops ({len(labels)})", labels[0], edge="buy",
                                     key=it.key, tip="\n".join(labels)))
        # Quest rewards, chests, ... always for the chosen item; for an ingredient only when they
        # aren't already behind a recipe, so big trees stay readable.
        found = extra_found(it.name, it.found, depth == 0 or not node.children)
        if found and (depth == 0 or src != "alchemy" or not node.children):
            node.children.append(found_leaf(found, it.key))

    def ingredient(name: str, qty: int, path: frozenset, depth: int, edge: str) -> Node:
        edge = f"x{qty}" if qty > 1 else edge
        it = reg.resolve(name)
        if it is None:
            lab = shops.get(data.norm(name))
            if lab:
                return new("shop", name, "shop", edge=edge, tip="\n".join(lab))
            found = extra_found(name, [], True)
            if found:
                node = new("base", name, "", edge=edge, sprite=name)
                node.children.append(found_leaf(found, None))
                return node
            return new("unknown", name, "no known source", edge=edge,
                       tip="No monster drops it and no recipe makes it.")
        kind = "craft" if source_of(it) == "alchemy" else "base"
        node = new(kind, it.name, it.category, edge=edge, key=it.key, sprite=it.name)
        if it.key in path:
            node.sub = "(loops back)"
            return node
        item_sources(node, it, path, depth)
        if not node.children and not node.sub.startswith("("):
            node.kind = "unknown"
            node.sub = "no known source"
        return node

    root = new("item", info.name, info.category, key=info.key, sprite=info.name)
    item_sources(root, info, frozenset(), 0)
    if not root.children:
        root.sub = f"{info.category}  -  no known source"
    return root


def layout(root: Node):
    """Fills in x, y (top-left of each bubble). Returns (width, height) of the whole tree."""
    row = [0]

    def rec(n: Node, depth: int):
        n.x = depth * (NODE_W + GAP_X)
        if not n.children:
            n.y = row[0] * (NODE_H + GAP_Y)
            row[0] += 1
        else:
            for c in n.children:
                rec(c, depth + 1)
            n.y = (n.children[0].y + n.children[-1].y) / 2
    import sys
    old = sys.getrecursionlimit()
    sys.setrecursionlimit(max(old, 5000))
    rec(root, 0)
    width = max(n.x for n in walk(root)) + NODE_W
    return width, max(row[0], 1) * (NODE_H + GAP_Y) - GAP_Y


def walk(n: Node):
    yield n
    for c in n.children:
        yield from walk(c)


# --------------------------------------------------------------------------- material list
@dataclass
class ListRow:
    name: str
    key: Optional[str]          # registry key ("" / None if the name matches no known item)
    qty: int
    craftable: bool = False     # has a recipe, so it can be broken down into its ingredients
    expanded: bool = False      # broken down (its ingredients are counted instead of it)
    loop: bool = False          # can't be broken down here: its recipe needs an item above it
    how: str = ""               # one line on where to get it


@dataclass
class MaterialList:
    target: str
    count: int
    materials: List[ListRow] = field(default_factory=list)   # what to gather / buy, totalled
    crafted: List[ListRow] = field(default_factory=list)     # what gets crafted along the way, in order
    notes: List[str] = field(default_factory=list)


def default_breakdown(it: data.ItemInfo, prefer: str = "alchemy") -> bool:
    """Same choice the tree makes: craft it unless monster drops are preferred and it has some."""
    return bool(it.recipes) and (prefer != "drops" or not it.droppers)


def how_to_get(it: Optional[data.ItemInfo], name: str, shops: Dict[str, List[str]],
               found_fn: Optional[Callable[[str], List[str]]] = None) -> str:
    """A short 'where to get it' line for one list row."""
    parts = []
    labels = shops.get(it.key if it else data.norm(name)) or shops.get(data.norm(name))
    if labels:
        parts.append("Buy: " + labels[0] + (f" (+{len(labels) - 1} more)" if len(labels) > 1 else ""))
    if it and it.droppers:
        enemy, drop = it.droppers[0]
        more = len(it.droppers) - 1
        parts.append(f"Drop: {enemy.name} {drop.odds_text}" + (f" (+{more} more)" if more else ""))
    found = list(it.found) if it else []
    if not parts and not found and found_fn and not (it and it.recipes):
        found = found_fn(it.name if it else name)
    if found:
        parts.append("Found: " + found[0] + (f" (+{len(found) - 1} more)" if len(found) > 1 else ""))
    if it and it.recipes:
        parts.append("Craftable")
    return "  ·  ".join(parts) or "no known source"


def material_list(reg: data.Registry, info: data.ItemInfo, shops: Dict[str, List[str]],
                  prefer: str = "alchemy", breakdown: Optional[Dict[str, bool]] = None,
                  count: int = 1, recipe_index: int = 0,
                  found_fn: Optional[Callable[[str], List[str]]] = None) -> MaterialList:
    """Everything needed to make `count` of `info`, with crafted ingredients broken down into what
    they're made of and equal items added together. breakdown[key] = False keeps a craftable item
    on the list as itself (you have it or will farm it) and ignores its recipe; True breaks it down.
    Items not in `breakdown` follow default_breakdown. The chosen item uses recipes[recipe_index];
    ingredients use their first recipe (the one the tree and details window show first)."""
    breakdown = breakdown or {}
    out = MaterialList(info.name, count)
    mats: Dict[str, List] = {}      # key -> [name, qty, ItemInfo|None, loop]
    crafted: Dict[str, List] = {}   # key -> [name, qty, ItemInfo]  (order: crafted first -> last)

    def wants(it: data.ItemInfo) -> bool:
        b = breakdown.get(it.key)
        return default_breakdown(it, prefer) if b is None else (b and bool(it.recipes))

    def add(name: str, qty: int, path: frozenset, depth: int):
        it = reg.resolve(name)
        key = it.key if it else "?" + data.norm(name)
        if it and it.recipes and wants(it) and key not in path and depth <= 50:
            for nm, q in it.recipes[0].ingredients:
                add(nm, q * qty, path | {key}, depth + 1)
            row = crafted.setdefault(key, [it.name, 0, it])
            row[1] += qty
            crafted[key] = crafted.pop(key)   # keep "made last" order: ingredients before what uses them
            return
        row = mats.setdefault(key, [it.name if it else name, 0, it, False])
        row[1] += qty
        if it and it.recipes and wants(it) and key in path:
            row[3] = True

    if not info.recipes:
        out.notes.append(f"{info.name} has no alchemy recipe, so there is nothing to break down.")
        add(info.name, count, frozenset(), 0)
    else:
        r = info.recipes[min(max(recipe_index, 0), len(info.recipes) - 1)]
        for nm, q in r.ingredients:
            add(nm, q * count, frozenset({info.key}), 1)

    for key, (name, qty, it, loop) in mats.items():
        out.materials.append(ListRow(name, it.key if it else None, qty, bool(it and it.recipes), False, loop,
                                     how_to_get(it, name, shops, found_fn)))
    for key, (name, qty, it) in crafted.items():
        out.crafted.append(ListRow(name, it.key, qty, True, True, False,
                                   f"{len(it.recipes)} recipes, using the first" if len(it.recipes) > 1 else ""))
    if any(r.loop for r in out.materials):
        out.notes.append("Some items can't be broken down here because their recipe needs an item "
                         "further up the list.")
    return out


def material_list_text(ml: MaterialList) -> str:
    """Plain-text copy of a material list (for the clipboard)."""
    lines = [f"To make {ml.count} × {ml.target}:", "", "Gather / buy:"]
    lines += [f"  {r.qty:>3} × {r.name}    ({r.how})" for r in ml.materials]
    if ml.crafted:
        lines += ["", "Craft along the way (in this order):"]
        lines += [f"  {r.qty:>3} × {r.name}" for r in ml.crafted]
    if ml.crafted:
        lines.append(f"  {ml.count:>3} × {ml.target}   (the item itself)")
    if ml.notes:
        lines += [""] + ml.notes
    return "\n".join(lines)
