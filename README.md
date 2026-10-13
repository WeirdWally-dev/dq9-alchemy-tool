# DQ9 Alchemy Helper

A desktop app for **Dragon Quest IX: Sentinels of the Starry Skies** that tells you how to get any piece of equipment or item: craft it in the Krak Pot, farm it from monsters, or buy it in a shop.

Pick an equipment type, pick an item, and the app draws a tree showing everything that goes into it. Each recipe is broken down into its ingredients, and each ingredient shows the monsters that drop it (with drop odds and locations) and the shops that sell it.

## Features

- **Equipment browser.** Choose a category (swords, axes, spears, head, torso, accessories, ingredients and more) and filter the item list by name.
- **Recipe tree.** A visual bubble map of the full recipe, unfolding nested recipes for crafted ingredients. Quantities sit on the connecting lines.
- **Monster drops.** Which monsters drop an item, at what rate, and where to find them. Monsters are sorted by best drop rate first, and the "Monsters shown" box controls how many appear. A small **+** badge reveals the rest.
- **Shops.** Every town and shop that sells an item (362 items, 567 listings across 15 shop pages).
- **Craft or farm toggle.** When an item can be both crafted and dropped, show just one route to keep the tree readable.
- **Item details.** Double-click any bubble for its description, sell price, stats and special effects.
- **Dark mode** throughout.
- **Polite and cached.** Online data is fetched once, respecting each site's `robots.txt`, and cached in `~/.dq9_helper/`.

## Requirements

- Python 3.9 or newer (developed on 3.13)
- PyQt5, requests, beautifulsoup4, openpyxl

## Installation

```bash
git clone https://github.com/WeirdWally-dev/dq9-alchemy-tool.git
cd dq9-alchemy-tool
python -m pip install -r requirements.txt
```

## Usage

```bash
python dq9_alchemy_helper.py
```

The first launch downloads the enemy list (a single request). Monster locations and item details are looked up the first time you view them, then cached.

> **VS Code users:** if you get `ModuleNotFoundError: No module named 'PyQt5'` but `pip` says it's installed, VS Code is using a different Python interpreter than your terminal. Pick the right one with **Python: Select Interpreter** in the Command Palette.

### Controls

| Control | What it does |
|---|---|
| Equipment type | Chooses the category of items to list |
| Filter box | Narrows the item list by name |
| Monsters shown | How many dropping monsters to show per item (highest drop rate first) |
| Show | Prefer the alchemy recipe or the monster drop when an item has both |
| Fit to window | Rescales the tree to fit the view |
| Import spreadsheet... | Re-reads a recipe spreadsheet (`.xlsx`) |
| Import woodus page... | Imports a recipes page you saved from your browser (see below) |
| Refresh data | Clears cached data and fetches it again |

## Data sources

| Data | Source | How it gets in |
|---|---|---|
| Alchemy recipes (445) | Community recipe spreadsheet | Bundled as `sheet_recipes.json` |
| Monsters, drops, drop rates, locations | [Dragon Quest Wiki](https://dragon-quest.org/wiki/List_of_enemies_in_Dragon_Quest_IX) | Fetched on demand and cached |
| Shop listings | [dqnine.wikidot.com/shops](http://dqnine.wikidot.com/shops) | Bundled as `shops_bundled.json` |
| Item details | dqnine.wikidot.com item pages | Fetched on demand and cached |
| Extra recipes (optional) | [Woodus Dragon's Den](https://www.woodus.com/den/games/dq9ds/alchemy_recipes.php) | Manual import |

Woodus's `robots.txt` asks automated tools not to fetch its recipe page, so the app never downloads it. To use it anyway, open the page in your browser, save it with **Ctrl+S** as "Webpage, HTML only", and load it with **Import woodus page...**.

Item names are matched exactly, because the game has distinct items with near-identical names (for example the *Tortoiseshell* ingredient and the *Tortoise shell* armour).

## Project structure

```
dq9_alchemy_helper.py   PyQt5 GUI: main window, tree view, detail dialogs
dq9_data.py             Data layer: scraping, parsing, caching, cross-referencing
dq9_tree.py             Builds the recipe/drop/shop tree the GUI draws
sheet_recipes.json      Bundled alchemy recipes
shops_bundled.json      Bundled shop listings
test_parsers.py         Test suite (no network or PyQt5 needed)
```

## Running the tests

```bash
python test_parsers.py
```

## License

GPL-3.0. See [LICENSE](LICENSE). The app uses PyQt5, which is licensed under the GPL.

Dragon Quest is a trademark of Square Enix. This is an unofficial fan tool, not affiliated with or endorsed by Square Enix. Game data belongs to its original sources and the community sites listed above.
