# DQ9 Alchemy Helper for Android (C++ / Qt 6)

This branch (`android-cpp`) is a C++ rewrite of the DQ9 Alchemy Helper so it runs natively on Android phones and tablets. The original Python/PyQt5 Windows app lives on `main` and is unchanged.

It tells you how to get any piece of equipment or item in **Dragon Quest IX: Sentinels of the Starry Skies**: craft it in the Krak Pot, farm it from monsters, or buy it in a shop.

## Features

Everything from the desktop app (version 1.2.0), laid out for a phone:

- **Browse** equipment by type (swords, axes, head, torso, accessories, ingredients and more) and filter by name.
- **Recipe tree.** The full recipe as a bubble map, with nested recipes unfolded, monster drops (best rate first) and shops. Drag to move, pinch to zoom, **Fit** to see it all. Tap any bubble for its details; a yellow **+** badge means more monsters drop it.
- **Material list.** The whole recipe chain flattened into one shopping list with totals, plus what you craft along the way, in order. Tick or untick craftable items to break them down or not. Copy the list to the clipboard.
- **Bookmarks.** Tap ☆ on an item to star it; pick **★ Bookmarks** at the top of the type list to see them all.
- **Weapon list, shops, quests and chests**, from the Dragon Quest Wiki, dqnine.wikidot.com and the bundled shop list.
- **Prefer alchemy or drops** when an item can be both crafted and dropped.
- **Dark theme**, Android back gesture goes back a screen.
- **Polite and cached.** Online data is fetched once, respecting each site's `robots.txt`, and kept in the app's private storage.

The one desktop feature not carried over is importing the `.xlsx` recipe spreadsheet; its 445 recipes still ship with the app.

## Install on your phone

1. Open the repo's **Actions** tab, pick the latest green **Build Android app** run on `android-cpp`, and download **DQ9-Alchemy-Helper-apk** from its Artifacts (or grab the APK from the latest release).
2. Copy `DQ9-Alchemy-Helper.apk` to your phone and open it. Android will ask you to allow installing from that source the first time.

Needs Android 9 or newer on a 64-bit (arm64) device, which covers nearly every phone from the last several years.

The first launch needs an internet connection to download the enemy and weapon lists (one request each). After that it works offline for anything you've already looked at.

## Building it yourself

You need Qt 6.8 (with the Android arm64 kit and the matching desktop kit), Android SDK + NDK r26b, JDK 17 and CMake 3.21+.

```bash
# desktop build + tests (handy for development; the window is phone-shaped)
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/gcc_64
cmake --build build
./build/dq9_tests
./build/DQ9AlchemyHelper

# Android APK
/path/to/Qt/6.8.3/android_arm64_v8a/bin/qt-cmake -S . -B build-android -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DQT_HOST_PATH=/path/to/Qt/6.8.3/gcc_64 \
    -DANDROID_SDK_ROOT=$ANDROID_SDK_ROOT -DANDROID_NDK_ROOT=$ANDROID_SDK_ROOT/ndk/26.1.10909125
cmake --build build-android --target apk
```

Or open `CMakeLists.txt` in Qt Creator, pick the Android kit and press Run with your phone plugged in.

HTTPS on Android needs OpenSSL inside the APK; CMake fetches KDAB's prebuilt `android_openssl` libraries for that automatically.

## Signing and releases

GitHub Actions builds the APK on every push to `android-cpp`. To have each new version install over the old one, add a signing key as repository secrets (**Settings → Secrets and variables → Actions**):

| Secret | What |
|---|---|
| `ANDROID_KEYSTORE_BASE64` | your `.jks` keystore, base64-encoded (`base64 -w0 release.jks`) |
| `ANDROID_KEYSTORE_PASSWORD` | its password |
| `ANDROID_KEY_ALIAS` | the key's alias |

Make a key once with `keytool -genkeypair -keystore release.jks -alias dq9 -keyalg RSA -keysize 2048 -validity 10000` and keep it safe. Without these secrets the build signs with a throwaway key, so you'd need to uninstall before installing the next build.

To publish a release: bump `VERSION` in `CMakeLists.txt` and add a matching `## x.y.z` section at the top of `CHANGELOG.md`. Then either make the release on GitHub's website with a new tag like `v1.4.0` targeting `android-cpp`, or tag and push:

```bash
git tag v1.4.0
git push origin android-cpp v1.4.0
```

The workflow builds the APK and attaches it to that release (creating the release if it doesn't exist yet).

## Project structure

```
src/core/          Data layer, no GUI (unit-tested)
  text, html       Name matching, regex helpers; a small DOM over the gumbo HTML5 parser
  parsers          Enemy list, monster pages, woodus recipes, item pages, icons, spreadsheet columns
  wikitext         The wiki's weapon list (read from its wikitext source)
  robots, http     robots.txt rules (same as Python's robotparser) and polite, rate-limited HTTP
  store            Download cache (app storage on Android, ~/.dq9_helper on desktop)
  registry         Cross-references every source; bookmarks
  render           HTML for the info pages
  tree             The recipe tree and the material list
src/app/           Qt Widgets UI: browse / item / info screens, tree canvas, background workers
tests/             Test suite (port of the Python test_parsers.py)
data/              Bundled recipes (445) and shop listings (362 items)
android/           Android manifest and launcher icon
third_party/gumbo  Google's gumbo HTML5 parser (Apache-2.0)
```

## Data sources

| Data | Source | How it gets in |
|---|---|---|
| Alchemy recipes (445) | Community recipe spreadsheet | Bundled (`data/sheet_recipes.json`) |
| Monsters, drops, drop rates, locations | [Dragon Quest Wiki](https://dragon-quest.org/wiki/List_of_enemies_in_Dragon_Quest_IX) | Fetched on demand and cached |
| Weapons: pictures, stats, prices, where to get them | [Dragon Quest Wiki](https://dragon-quest.org/wiki/List_of_weapons_in_Dragon_Quest_IX) | Fetched once and cached |
| Shop listings | [dqnine.wikidot.com/shops](http://dqnine.wikidot.com/shops) | Bundled (`data/shops_bundled.json`) |
| Item details, chests, quests | dqnine.wikidot.com item pages | Fetched on demand and cached |
| Extra recipes (optional) | [Woodus Dragon's Den](https://www.woodus.com/den/games/dq9ds/alchemy_recipes.php) | Manual import of a saved page |

Woodus's `robots.txt` asks automated tools not to fetch its recipe page, so the app never downloads it. To use it anyway, save the page from a browser and load it with **⋮ → Import woodus page...**.

Item names are matched exactly (ignoring case, spacing and accents), because the game has distinct items with near-identical names, like the *Tortoiseshell* ingredient and the *Tortoise shell* armour.

## License

GPL-3.0. See [LICENSE](LICENSE). Uses Qt (LGPL-3.0/GPL) and gumbo-parser (Apache-2.0).

Dragon Quest is a trademark of Square Enix. This is an unofficial fan tool, not affiliated with or endorsed by Square Enix. Game data belongs to its original sources and the community sites listed above.
