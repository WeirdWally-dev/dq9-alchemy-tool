# Changelog

Each release's notes on GitHub come from its section below. Add a new section at the top before you tag a release.

## 1.4.0 - 2026-10-15

- **Search all items.** New **🔍 Search all items** entry at the top of the type list (and in the ⋮ menu; Ctrl+F on Windows). Type part of a name to find any weapon, armour piece, accessory or ingredient without picking its type first. Exact and starts-with matches come first, and case, spaces, punctuation and accents don't matter.
- **Glossary: what it's used in.** New **📖 Glossary** entry (⋮ menu, Ctrl+G on Windows) listing every item, weapon and ingredient, including ingredients that only appear inside recipes, with how many recipes each goes into. Tap one to see what it's used to make, one level at a time: Bow 1 shows Bow 2 (not Bow 3); tap Bow 2 to see Bow 3. Each entry shows the full recipe and how many things *it* is used in. A trail at the top shows the path, Back steps down a level, and **How to get…** opens the recipe tree.
- **What it's used in** is also in an item page's ⋮ menu and in the long-press / right-click menu of the item lists.

## 1.3.3 - 2026-10-13

- **Smooth zooming and panning on phones.** The recipe tree is now drawn with the GPU on Android, so pinch-zoom keeps up with your fingers and there's no more tearing.
- Pinch zooms around the spot between your fingers and that spot stays under them; two fingers can pan while zooming. One-finger drags follow your finger exactly.
- The **+** and **−** buttons zoom smoothly instead of jumping.
- A tap opens a bubble only if your finger didn't move, so panning no longer opens things by accident.

## 1.3.2 - 2026-10-13

- **Fixed: the app never finished loading** (nothing in the lists, on Android and everywhere else). The pause between web requests was miscalculated, so the second request of a session waited forever.
- **Phone menus rebuilt.** The ⋮ menus and the dropdowns (equipment type, alchemy/drops) now open as a full screen of large buttons instead of Qt's small pop-ups, which didn't work properly with touch. Back closes them.
- **Windows app is back**, now built from the same C++ code: a `DQ9-Alchemy-Helper-Setup.exe` installer (no admin rights needed) and a portable zip. Same desktop layout as before (item list on the left, tree on the right, details in their own windows) with tooltips, Ctrl+D / Ctrl+B and mouse-wheel zoom. Your existing `%USERPROFILE%\.dq9_helper` cache and bookmarks carry over.
- **Import spreadsheet...** works again (Windows and Android): re-reads the `.xlsx` recipe spreadsheet.
- Behind the scenes: every build now runs the app on an Android emulator and checks the live wiki pages, so problems like the loading bug get caught before a release.

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
