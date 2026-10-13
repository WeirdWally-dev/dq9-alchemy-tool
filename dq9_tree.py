"""
Turns an item into a left-to-right branching tree of "bubbles" (no Qt in here, so it can be
tested on its own):  item -> its recipe(s) -> ingredients -> (their recipes ...) -> monsters / shops.
"""
from dataclasses import dataclass, field
from typing import Dict, List, Optional

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
               prefer: str = "alchemy") -> Node:
    """prefer: "alchemy" or "drops". An item that has both a recipe and monster drops shows only
    the preferred one (an item with just one of them shows that one). Shops always add a leaf."""
    count = [0]

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
        if it.found and (depth == 0 or src != "alchemy" or not node.children):
            label = "Found" if len(it.found) == 1 else f"Found ({len(it.found)} ways)"
            node.children.append(new("found", label, it.found[0], edge="get", key=it.key,
                                     tip="\n".join(it.found)))

    def ingredient(name: str, qty: int, path: frozenset, depth: int, edge: str) -> Node:
        edge = f"x{qty}" if qty > 1 else edge
        it = reg.resolve(name)
        if it is None:
            lab = shops.get(data.norm(name))
            kind = "shop" if lab else "unknown"
            return new(kind, name, "shop" if lab else "no known source", edge=edge,
                       tip="\n".join(lab or ["No monster drops it and no recipe makes it."]))
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
