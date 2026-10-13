# Changelog

Each release's notes on GitHub come from its section below. Add a new section at the top before you tag a release.

## 1.3.0 - 2026-10-13 (Android)

First Android release: the whole app rewritten in C++ with Qt 6, on the `android-cpp` branch.

- Same features as the 1.2.0 Windows app: recipe tree, material list, bookmarks, weapon list, shops, monster drops and locations, chests and quests.
- Laid out for phones: one screen at a time, tap a bubble for its details, pinch to zoom the tree, Android back goes back a screen.
- HTML scraping moves from BeautifulSoup to the gumbo HTML5 parser; robots.txt is honoured exactly as before.
- Importing the `.xlsx` recipe spreadsheet isn't available on Android (the bundled recipes are still included).
- The Python test suite is ported to C++ and runs on every build.

---

## 1.2.0 - 2026-10-13

- **Bookmarks.** Star items with **☆ Bookmark** (Ctrl+D) and get back to them from **★ Bookmarks** (Ctrl+B), at the top of the Equipment type list. Right-click to remove or reorder. Saved between runs.
- Changing the equipment type or the filter keeps the selected item open when it's still in the list.

## 1.1.0 - 2026-10-13

- **Weapon list.** Every weapon on the Dragon Quest Wiki's weapon list now shows its picture, attack, vocations, buy and sell price and notes. Quest rewards, chests, bonus-alchemy chances and boss drops appear as teal **Found** bubbles; cells the wiki marks N/A are left out. Longbow gets its real picture (the list page shows a placeholder).
- **Material list.** Switch **View** to *Material list* for a single shopping list: every ingredient added up across the whole recipe chain, plus what you'll craft along the way, in order. Tick a craftable item to break it down into its ingredients, or untick it to list the item itself. Choices are remembered. Set how many to make, pick between recipes, and copy the list as text.
- **Fewer "no known source" items.** Items that only come from chests, grottos, dungeons or quests (like the Silver platter) now show where to find them, taken from their dqnine.wikidot.com page.
- Names that differ only by a plural or an accent now match (Iron nails / Iron nail, Über / Uber).

## 1.0.0 - 2026-10-13

First release, as a standalone Windows app.

- Browse equipment by type and see how to get any item: alchemy, monster drops or shops.
- Recipe tree with nested recipes, drop odds, monster locations and shop listings.
- Craft-or-farm toggle, adjustable number of monsters shown, and a **+** badge for the rest.
- Double-click any bubble for the item's description, price, stats and effects.
- 445 bundled recipes and 362 bundled shop items; monster and item details fetched on demand and cached.
- Dark mode.
