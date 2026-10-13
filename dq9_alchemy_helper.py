#!/usr/bin/env python3
"""
DQ9 Alchemy Helper - pick an equipment type, pick an item, see how to get it.

    python -m pip install PyQt5 requests beautifulsoup4 openpyxl
    python dq9_alchemy_helper.py

First launch downloads the enemy list and the weapon list (one request each) and caches them in
~/.dq9_helper/.
Monster locations and item details (price, description, effects) are looked up on demand
(one polite request each, cached forever).
"""
import queue
import sys
import threading

from PyQt5.QtCore import QRectF, QThread, QTimer, Qt, pyqtSignal
from PyQt5.QtGui import QBrush, QColor, QFont, QFontMetrics, QPainter, QPainterPath, QPalette, QPen, QPixmap
from PyQt5.QtWidgets import (
    QApplication, QComboBox, QDialog, QFileDialog, QGraphicsPathItem, QGraphicsPixmapItem, QGraphicsRectItem, QGraphicsScene,
    QGraphicsSimpleTextItem, QGraphicsView, QHBoxLayout, QLabel, QLineEdit, QListWidget,
    QListWidgetItem, QMainWindow, QMessageBox, QPushButton, QSpinBox, QSplitter,
    QTextBrowser, QVBoxLayout, QWidget,
)

import dq9_data as data
from version import __version__
import dq9_tree as tree

BG, PANEL, TEXT, MUTED = "#17181c", "#22242a", "#e6e6e6", "#a0a4ad"
CSS = (f"<style>body{{font-size:13px; color:{TEXT}; background-color:{BG}}} h2{{margin-bottom:0; color:#ffffff}} "
       f"h3{{margin-top:18px; color:#9ec5f5}} h4{{margin:12px 0 4px 0; color:#c9d4e5}} "
       f"th{{background-color:#2d3038; color:{TEXT}}} td{{color:{TEXT}}} code{{font-family:monospace}} "
       f"a{{color:#7fb1ea}} small{{color:{MUTED}}}</style>")

ROBOTS_MSG = (
    "No alchemy recipes are loaded yet.\n\n"
    "The recipe spreadsheet (sheet_recipes.json) should sit in the same folder as this program.\n"
    "If you have the spreadsheet itself, click \"Import spreadsheet...\" and pick it\n"
    "(this needs:  python -m pip install openpyxl).\n\n"
    "woodus.com's robots.txt asks automated tools not to fetch its recipes page, so this app\n"
    "won't. To use that page instead: open it in your browser, save it (Ctrl+S, \"Webpage, HTML\n"
    "only\"), then click \"Import woodus page...\" and pick the saved file.\n\n"
    "Until then you can still browse monster drops and locations."
)


# bubble colours: (fill, border)
COLORS = {
    "item": ("#3b6ea8", "#8fbcf2"), "craft": ("#2b4666", "#5b8fd0"), "base": ("#2c4a30", "#6aa84f"),
    "recipe": ("#3d3558", "#9a88d6"), "monster": ("#6b2d2d", "#e06666"), "shop": ("#5c4a14", "#e0b030"),
    "found": ("#1f5a55", "#4fc1b5"),
    "more": ("#33353b", "#7b7f88"), "unknown": ("#33353b", "#7b7f88"),
}
LEGEND = [("item", "this item"), ("craft", "made by alchemy"), ("base", "base ingredient"),
          ("monster", "monster drop"), ("shop", "sold in shops"), ("found", "quest / chest / other"),
          ("unknown", "no known source")]


SPRITE_PX = 34   # icons are drawn at most this big inside a bubble


class PlusBadge(QGraphicsPathItem):
    """Small '+' circle on a bubble's top-right corner: more monsters drop this item."""
    R = 10

    def __init__(self, node, on_open, parent):
        super().__init__(parent)
        self.node, self.on_open = node, on_open
        path = QPainterPath()
        path.addEllipse(QRectF(-self.R, -self.R, 2 * self.R, 2 * self.R))
        self.setPath(path)
        self.setBrush(QBrush(QColor("#ffd966")))
        self.setPen(QPen(QColor(BG), 2))
        self.setPos(tree.NODE_W - 8, 6)
        self.setZValue(5)
        self.setCursor(Qt.PointingHandCursor)
        txt = QGraphicsSimpleTextItem("+", self)
        f = QFont()
        f.setBold(True)
        f.setPointSize(11)
        txt.setFont(f)
        txt.setBrush(QBrush(QColor("#222222")))
        r = txt.boundingRect()
        txt.setPos(-r.width() / 2, -r.height() / 2)
        self.setToolTip(f"+{node.more} more monster{'s' if node.more > 1 else ''} also drop this item.\n"
                        "Click to see them all.")

    def mousePressEvent(self, event):
        self.on_open(tree.Node("more", "", key=self.node.more_key))

    def mouseDoubleClickEvent(self, event):
        self.mousePressEvent(event)


class Bubble(QGraphicsPathItem):
    """One rounded 'bubble'. Double-click opens its info window."""

    def __init__(self, node, on_open, sprite_fn=None):
        super().__init__()
        self.node, self.on_open = node, on_open
        self.sprite_item = None
        path = QPainterPath()
        path.addRoundedRect(QRectF(0, 0, tree.NODE_W, tree.NODE_H), tree.NODE_H / 2.2, tree.NODE_H / 2.2)
        self.setPath(path)
        fill, border = COLORS.get(node.kind, COLORS["unknown"])
        self.setBrush(QBrush(QColor(fill)))
        self.setPen(QPen(QColor(border), 2))
        self.setPos(node.x, node.y)
        self.setZValue(1)
        self.setCursor(Qt.PointingHandCursor)
        colour = QColor("#f2f2f2")
        title = QFont()
        title.setBold(True)
        title.setPointSize(9)
        small = QFont()
        small.setPointSize(8)
        left = 11
        if node.sprite and sprite_fn:
            has, px = sprite_fn(node.sprite)
            if has:   # reserve the icon's spot now; the picture is filled in once it's downloaded
                self.sprite_item = QGraphicsPixmapItem(self)
                self.sprite_item.setPos(9, (tree.NODE_H - SPRITE_PX) / 2)
                left = 9 + SPRITE_PX + 6
                if px is not None:
                    self.set_sprite(px)
        room = tree.NODE_W - left - 11
        self._line(node.label, title, colour, room, 8 if node.sub else 17, left)
        if node.sub:
            self._line(node.sub, small, QColor("#c3c7d0"), room, 28, left)
        tip = node.tip or (f"{node.label}\n{node.sub}" if node.sub else node.label)
        self.setToolTip(tip + "\n\n(double-click for details)")

        if node.more > 0:
            self.badge = PlusBadge(node, on_open, self)

    def set_sprite(self, px):
        if self.sprite_item is None or px is None or px.isNull():
            return
        if px.height() > SPRITE_PX or px.width() > SPRITE_PX:
            px = px.scaled(SPRITE_PX, SPRITE_PX, Qt.KeepAspectRatio, Qt.SmoothTransformation)
        self.sprite_item.setPixmap(px)
        self.sprite_item.setOffset((SPRITE_PX - px.width()) / 2, (SPRITE_PX - px.height()) / 2)

    def _line(self, text, font, colour, room, y, left=11):
        shown = QFontMetrics(font).elidedText(text, Qt.ElideRight, room)
        item = QGraphicsSimpleTextItem(shown, self)
        item.setFont(font)
        item.setBrush(QBrush(colour))
        item.setPos(left + (room - item.boundingRect().width()) / 2, y - 4)

    def mouseDoubleClickEvent(self, event):
        self.on_open(self.node)


class TreeView(QGraphicsView):
    """Pannable (drag) and zoomable (mouse wheel) canvas that draws a Node tree."""

    def __init__(self, on_open, sprite_fn=None):
        super().__init__()
        self.on_open, self.sprite_fn = on_open, sprite_fn
        self.sprite_bubbles = {}   # sprite key -> bubbles waiting for / showing that icon
        self.scene_ = QGraphicsScene(self)
        self.setScene(self.scene_)
        self.setRenderHints(QPainter.Antialiasing | QPainter.TextAntialiasing)
        self.setBackgroundBrush(QBrush(QColor(BG)))
        self.setDragMode(QGraphicsView.ScrollHandDrag)
        self.setTransformationAnchor(QGraphicsView.AnchorUnderMouse)
        self.message("Loading data...")

    def message(self, text):
        self.scene_.clear()
        self.sprite_bubbles = {}
        t = self.scene_.addText(text)
        t.setDefaultTextColor(QColor(TEXT))
        self.setSceneRect(t.sceneBoundingRect().adjusted(-40, -40, 40, 40))
        self.resetTransform()

    def show_tree(self, root):
        self.scene_.clear()
        width, height = tree.layout(root)
        for n in tree.walk(root):
            for c in n.children:
                self._edge(n, c)
        self.sprite_bubbles = {}
        for n in tree.walk(root):
            b = Bubble(n, self.on_open, self.sprite_fn)
            self.scene_.addItem(b)
            if b.sprite_item is not None:
                self.sprite_bubbles.setdefault(data.skey(n.sprite), []).append(b)
        self.setSceneRect(-30, -30, width + 60, height + 60)
        self.resetTransform()
        self.fit()

    def fit(self):
        """Zoom out to fit a big tree, but never zoom in past 100%."""
        self.fitInView(self.sceneRect(), Qt.KeepAspectRatio)
        if self.transform().m11() > 1:
            self.resetTransform()

    def _edge(self, parent, child):
        x1, y1 = parent.x + tree.NODE_W, parent.y + tree.NODE_H / 2
        x2, y2 = child.x, child.y + tree.NODE_H / 2
        mid = (x1 + x2) / 2
        path = QPainterPath()
        path.moveTo(x1, y1)
        path.cubicTo(mid, y1, mid, y2, x2, y2)
        line = QGraphicsPathItem(path)
        line.setPen(QPen(QColor("#7b8090"), 1.8))
        line.setZValue(0)
        self.scene_.addItem(line)
        if child.edge:   # centred on the middle of the line, on a small backing so it stays readable
            lab = QGraphicsSimpleTextItem(child.edge)
            f = QFont()
            f.setPointSize(9)
            f.setBold(True)
            lab.setFont(f)
            lab.setBrush(QBrush(QColor("#ffd966")))
            w, h = lab.boundingRect().width(), lab.boundingRect().height()
            cx, cy = mid, (y1 + y2) / 2
            lab.setPos(cx - w / 2, cy - h / 2)
            lab.setZValue(3)
            back = QGraphicsRectItem(cx - w / 2 - 5, cy - h / 2 - 1, w + 10, h + 2)
            back.setBrush(QBrush(QColor(BG)))
            back.setPen(QPen(QColor("#7b8090"), 1))
            back.setZValue(2.5)
            self.scene_.addItem(back)
            self.scene_.addItem(lab)

    def wheelEvent(self, event):
        factor = 1.15 if event.angleDelta().y() > 0 else 1 / 1.15
        self.scale(factor, factor)


class InfoDialog(QDialog):
    """The full write-up for one bubble. `render` returns (html, monsters_to_look_up)."""

    def __init__(self, parent, title, render):
        super().__init__(parent)
        self.setWindowTitle(title)
        self.setAttribute(Qt.WA_DeleteOnClose)
        self.render_fn = render
        self.view = QTextBrowser()
        self.view.setOpenLinks(False)
        lay = QVBoxLayout(self)
        lay.addWidget(self.view)
        self.resize(560, 640)
        self.refresh()

    def refresh(self):
        bar = self.view.verticalScrollBar()
        pos = bar.value()
        html, needed = self.render_fn()
        self.view.setHtml(CSS + html)
        QTimer.singleShot(0, lambda: bar.setValue(pos))
        return needed


class LoadWorker(QThread):
    status = pyqtSignal(str)
    ready = pyqtSignal(object, str)   # Registry, note
    failed = pyqtSignal(str)

    def __init__(self, store, force):
        super().__init__()
        self.store, self.force = store, force

    def run(self):
        try:
            self.status.emit("Loading enemy list...")
            enemies = self.store.load_enemies(self.force)
        except Exception as e:  # noqa: BLE001 - surface anything to the user
            self.failed.emit(f"Couldn't load the enemy list:\n{e}")
            return
        try:
            self.status.emit("Loading weapon list...")
            self.store.load_weapons(self.force)
        except Exception:  # noqa: BLE001 - the weapon list adds detail; the app works without it
            pass
        woodus, note = [], ""
        try:
            self.status.emit("Loading alchemy recipes...")
            woodus, note = self.store.load_recipes(self.force)
        except data.RobotsDisallowed:
            note = "robots"
        except Exception as e:  # noqa: BLE001
            note = f"Couldn't load recipes: {e}"
        sheet = self.store.load_sheet_recipes()
        try:
            self.status.emit("Loading item icons...")
            self.store.load_sprite_index(self.force)
        except Exception:  # noqa: BLE001 - icons are optional
            pass
        self.store.register_sprites(data.weapon_sprites(self.store.weapons))
        if note == "robots" and (woodus or sheet):
            note = ""   # woodus.com is optional once the spreadsheet (or a saved copy) is there
        self.status.emit("Matching items across sources...")
        self.ready.emit(data.Registry(enemies, woodus + sheet, self.store.shop_items(),
                                      self.store.weapons), note)


class LocWorker(QThread):
    """Looks up monster locations one at a time in the background."""
    done = pyqtSignal(str)

    def __init__(self, store):
        super().__init__()
        self.store = store
        self.q = queue.Queue()
        self.lock = threading.Lock()
        self.pending, self.empty, self.errors = set(), set(), {}

    def state(self, enemy):
        cached = self.store.cached_locations(enemy)
        if cached is not None:
            return "ok", cached
        with self.lock:
            if enemy.url in self.errors:
                return "error", []
            if enemy.url in self.empty:
                return "unknown", []
        return "loading", []

    def request(self, enemy):
        if self.store.cached_locations(enemy) is not None:
            return
        with self.lock:
            if enemy.url in self.pending or enemy.url in self.errors or enemy.url in self.empty:
                return
            self.pending.add(enemy.url)
        self.q.put(enemy)

    def reset_failures(self):
        with self.lock:
            self.errors.clear()
            self.empty.clear()

    def run(self):
        while True:
            enemy = self.q.get()
            if enemy is None:
                return
            try:
                if not self.store.locations(enemy):
                    with self.lock:
                        self.empty.add(enemy.url)
            except Exception as e:  # noqa: BLE001
                with self.lock:
                    self.errors[enemy.url] = str(e)
            finally:
                with self.lock:
                    self.pending.discard(enemy.url)
                self.done.emit(enemy.url)

    def shutdown(self):
        self.q.put(None)
        self.wait(3000)


class DetailWorker(QThread):
    """Fetches item facts (description, sell price, effects) from the wiki, one at a time."""
    done = pyqtSignal(str)

    def __init__(self, store):
        super().__init__()
        self.store = store
        self.q = queue.Queue()
        self.lock = threading.Lock()
        self.pending, self.failed = set(), set()

    def state(self, name):
        key = data.dkey(name)
        cached = self.store.cached_details(name)
        if cached is not None:
            return ("ok", cached) if cached else ("none", None)
        with self.lock:
            if key in self.failed:
                return "none", None
            if key not in self.pending:
                self.pending.add(key)
                self.q.put(name)
        return "loading", None

    def reset_failures(self):
        with self.lock:
            self.failed.clear()

    def run(self):
        while True:
            name = self.q.get()
            if name is None:
                return
            try:
                self.store.item_details(name)
            except Exception:  # noqa: BLE001 - offline, blocked, etc.: just show nothing
                with self.lock:
                    self.failed.add(data.dkey(name))
            finally:
                with self.lock:
                    self.pending.discard(data.dkey(name))
                self.done.emit(name)

    def shutdown(self):
        self.q.put(None)
        self.wait(3000)


class SpriteWorker(QThread):
    """Downloads item icons to the cache folder, one at a time (drawing happens on the UI thread)."""
    done = pyqtSignal(str)

    def __init__(self, store):
        super().__init__()
        self.store = store
        self.q = queue.Queue()
        self.asked = set()

    def request(self, name):
        key = data.skey(name)
        if key not in self.asked:
            self.asked.add(key)
            self.q.put(name)

    def run(self):
        while True:
            name = self.q.get()
            if name is None:
                return
            try:
                if self.store.fetch_sprite(name):
                    self.done.emit(name)
            except Exception:  # noqa: BLE001 - no icon is fine; the bubble just has no picture
                pass

    def shutdown(self):
        self.q.put(None)
        self.wait(3000)


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle(f"DQ9 Alchemy Helper v{__version__}")
        self.store = data.Store()
        self.registry = None
        self.current = None
        self.dialogs = []
        self.loader = None
        self._robots_notice_shown = False

        self.rerender_timer = QTimer(self)   # batches location results into one re-render
        self.rerender_timer.setSingleShot(True)
        self.rerender_timer.setInterval(150)
        self.rerender_timer.timeout.connect(self.refresh_dialogs)

        self.loc = LocWorker(self.store)
        self.loc.done.connect(lambda _url: self.rerender_timer.start())
        self.loc.start()
        self.details = DetailWorker(self.store)
        self.details.done.connect(self.details_ready)
        self.details.start()
        self.found_wait = set()   # items the tree is waiting on an item page for
        self.redraw_timer = QTimer(self)   # batches item-page results into one tree redraw
        self.redraw_timer.setSingleShot(True)
        self.redraw_timer.setInterval(250)
        self.redraw_timer.timeout.connect(self.draw)
        self.sprites = SpriteWorker(self.store)
        self.sprites.done.connect(self.sprite_ready)
        self.sprites.start()
        self.pixmaps = {}

        self._build_ui()
        self.start_load(False)

    # ------------------------------------------------------------------ UI
    def _build_ui(self):
        self.category = QComboBox()
        self.category.setMinimumWidth(170)
        self.category.currentIndexChanged.connect(self.fill_items)
        self.filter = QLineEdit()
        self.filter.setPlaceholderText("Filter items...")
        self.filter.setClearButtonEnabled(True)
        self.filter.textChanged.connect(self.fill_items)
        self.per_n = QSpinBox()
        self.per_n.setRange(1, 30)
        self.per_n.setValue(1)
        self.per_n.valueChanged.connect(self.draw)
        self.refresh_btn = QPushButton("Refresh data")
        self.refresh_btn.clicked.connect(lambda: self.start_load(True))
        self.import_btn = QPushButton("Import woodus page...")
        self.import_btn.clicked.connect(self.import_recipes)
        self.sheet_btn = QPushButton("Import spreadsheet...")
        self.sheet_btn.clicked.connect(self.import_sheet)

        bar = QHBoxLayout()
        bar.addWidget(QLabel("Equipment type:"))
        bar.addWidget(self.category)
        bar.addWidget(self.filter, 1)
        bar.addWidget(QLabel("Monsters shown:"))
        bar.addWidget(self.per_n)
        bar.addWidget(self.sheet_btn)
        bar.addWidget(self.import_btn)
        bar.addWidget(self.refresh_btn)

        self.items = QListWidget()
        self.items.currentItemChanged.connect(self.select_item)
        self.view = TreeView(self.open_info, self.get_sprite)
        self.prefer = QComboBox()
        self.prefer.addItem("Alchemy recipe", "alchemy")
        self.prefer.addItem("Monster drops", "drops")
        self.prefer.setToolTip("When an item can be crafted AND dropped by monsters, show only this one")
        self.prefer.currentIndexChanged.connect(self.draw)
        self.fit_btn = QPushButton("Fit to window")
        self.fit_btn.clicked.connect(self.view.fit)
        legend = QLabel("  ".join(
            f"<span style='background:{COLORS[k][0]}; border:1px solid {COLORS[k][1]}'>&nbsp;&nbsp;&nbsp;</span> {t}"
            for k, t in LEGEND) + "  &nbsp;|&nbsp; wheel = zoom, drag = move, double-click a bubble for details")
        legend.setTextFormat(Qt.RichText)
        right = QWidget()
        rlay = QVBoxLayout(right)
        rlay.setContentsMargins(0, 0, 0, 0)
        top = QHBoxLayout()
        top.addWidget(legend, 1)
        top.addWidget(QLabel("Show:"))
        top.addWidget(self.prefer)
        top.addWidget(self.fit_btn)
        rlay.addLayout(top)
        rlay.addWidget(self.view, 1)

        split = QSplitter(Qt.Horizontal)
        split.addWidget(self.items)
        split.addWidget(right)
        split.setStretchFactor(0, 1)
        split.setStretchFactor(1, 3)

        root = QWidget()
        lay = QVBoxLayout(root)
        lay.addLayout(bar)
        lay.addWidget(split, 1)
        self.setCentralWidget(root)
        self.statusBar().showMessage("Starting...")

    # ------------------------------------------------------------------ loading
    def start_load(self, force):
        if self.loader and self.loader.isRunning():
            return
        if force:
            self.loc.reset_failures()
            self.details.reset_failures()
        self.refresh_btn.setEnabled(False)
        self.loader = LoadWorker(self.store, force)
        self.loader.status.connect(self.statusBar().showMessage)
        self.loader.ready.connect(self.on_ready)
        self.loader.failed.connect(self.on_failed)
        self.loader.start()

    def on_failed(self, msg):
        self.refresh_btn.setEnabled(True)
        self.statusBar().showMessage("Load failed")
        self.view.message("Couldn't load data:\n" + msg)

    def on_ready(self, registry, note):
        self.refresh_btn.setEnabled(True)
        self.set_registry(registry)
        if note == "robots":
            self.statusBar().showMessage("Recipes not loaded: import a saved copy of the recipes page")
            if not self._robots_notice_shown:
                self._robots_notice_shown = True
                QMessageBox.information(self, "Recipes need a saved copy", ROBOTS_MSG)
        elif note:
            self.statusBar().showMessage(note)
        else:
            self.statusBar().showMessage(
                f"{len(registry.enemies)} monsters, {registry.recipe_count} recipes loaded", 6000)

    def set_registry(self, registry):
        self.registry = registry
        keep = self.category.currentText()
        self.category.blockSignals(True)
        self.category.clear()
        self.category.addItems(registry.categories())
        idx = self.category.findText(keep)
        self.category.setCurrentIndex(max(idx, 0))
        self.category.blockSignals(False)
        self.fill_items()

    def import_recipes(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Open the saved recipes page", "", "HTML files (*.html *.htm);;All files (*)")
        if not path:
            return
        try:
            recipes = self.store.import_recipes_file(path)
        except Exception as e:  # noqa: BLE001
            QMessageBox.warning(self, "Couldn't read that file", str(e))
            return
        self.rebuild_registry()
        self.statusBar().showMessage(f"Imported {len(recipes)} recipes from the woodus page", 6000)

    def import_sheet(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Open the recipes spreadsheet", "", "Excel files (*.xlsx *.xlsm);;All files (*)")
        if not path:
            return
        try:
            recipes = self.store.import_sheet_file(path)
        except Exception as e:  # noqa: BLE001
            QMessageBox.warning(self, "Couldn't read that file", str(e))
            return
        self.rebuild_registry()
        self.statusBar().showMessage(f"Imported {len(recipes)} recipes from the spreadsheet", 6000)

    def rebuild_registry(self):
        """Re-merge every recipe source we have (no network) after an import."""
        if self.registry:
            self.set_registry(data.Registry(self.registry.enemies, self.store.cached_recipes(),
                                            self.store.shop_items(), self.store.weapons))

    # ------------------------------------------------------------------ list + detail
    def fill_items(self, *_):
        self.items.blockSignals(True)
        self.items.clear()
        if self.registry:
            for info in self.registry.items_in(self.category.currentText(), self.filter.text()):
                tags = [t for t, ok in (("alchemy", info.recipes), ("drop", info.droppers),
                                        ("found", info.found)) if ok]
                it = QListWidgetItem(info.name + (f"   ({', '.join(tags)})" if tags else ""))
                it.setData(Qt.UserRole, info.key)
                self.items.addItem(it)
        self.items.blockSignals(False)
        self.current = None
        self.view.message("Select an item on the left.")

    def select_item(self, cur, _prev):
        self.current = self.registry.items.get(cur.data(Qt.UserRole)) if cur and self.registry else None
        self.draw()

    def get_sprite(self, name):
        """(has_icon, pixmap or None). Starts the download if the picture isn't cached yet."""
        if not self.store.has_sprite(name):
            return False, None
        key = data.skey(name)
        px = self.pixmaps.get(key)
        if px is None:
            path = self.store.sprite_path(name)
            if path:
                px = QPixmap(str(path))
                self.pixmaps[key] = px
            else:
                self.sprites.request(name)
        return True, px

    def sprite_ready(self, name):
        key = data.skey(name)
        path = self.store.sprite_path(name)
        if not path:
            return
        px = self.pixmaps.setdefault(key, QPixmap(str(path)))
        for b in self.view.sprite_bubbles.get(key, []):
            b.set_sprite(px)

    def found_for(self, name):
        """Where-to-find lines from an item's wiki page. Starts the look-up if it isn't cached;
        the tree is redrawn when it arrives."""
        state, det = self.details.state(name)
        if state == "loading":
            self.found_wait.add(data.dkey(name))
            return []
        return data.details_found(det)

    def details_ready(self, name):
        self.rerender_timer.start()
        key = data.dkey(name)
        if key in self.found_wait:
            self.found_wait.discard(key)
            self.redraw_timer.start()

    def draw(self, *_):
        if not (self.registry and self.current):
            return
        self.found_wait.clear()
        root = tree.build_tree(self.registry, self.current, self.per_n.value(), self.store.shops(),
                               self.prefer.currentData(), self.found_for)
        self.view.show_tree(root)

    # ------------------------------------------------------------------ double-click windows
    def open_info(self, node):
        reg = self.registry
        if node.enemy:
            enemy = node.enemy
            title = enemy.name
            render = lambda: data.render_enemy_html(enemy, self.loc.state)   # noqa: E731
        else:
            info = reg.items.get(node.key) if node.key else None
            title = info.name if info else node.label
            if info is None:
                text = data.esc(node.tip or "No monster drops this and no recipe makes it.")
                render = lambda: (f"<h2>{data.esc(node.label)}</h2><p>{text}</p>", [])   # noqa: E731
            else:
                render = lambda: data.render_item_html(   # noqa: E731
                    reg, info, max(self.per_n.value(), 10), self.loc.state, self.store.shops(),
                    self.details.state)
        dlg = InfoDialog(self, title, render)
        self.dialogs.append(dlg)
        dlg.destroyed.connect(lambda _=None, d=dlg: self.dialogs.remove(d) if d in self.dialogs else None)
        dlg.show()
        self.request_locations(dlg)

    def request_locations(self, dlg):
        for enemy in dlg.render_fn()[1]:
            self.loc.request(enemy)

    def refresh_dialogs(self):
        for dlg in list(self.dialogs):
            for enemy in dlg.refresh():
                self.loc.request(enemy)

    def closeEvent(self, event):
        for dlg in list(self.dialogs):
            dlg.close()
        self.loc.shutdown()
        self.details.shutdown()
        self.sprites.shutdown()
        super().closeEvent(event)


def apply_dark(app):
    app.setStyle("Fusion")
    pal = QPalette()
    for role, col in ((QPalette.Window, PANEL), (QPalette.WindowText, TEXT), (QPalette.Base, BG),
                      (QPalette.AlternateBase, PANEL), (QPalette.Text, TEXT), (QPalette.Button, "#2d3038"),
                      (QPalette.ButtonText, TEXT), (QPalette.ToolTipBase, "#2d3038"),
                      (QPalette.ToolTipText, TEXT), (QPalette.Highlight, "#3b6ea8"),
                      (QPalette.HighlightedText, "#ffffff"), (QPalette.Link, "#7fb1ea"),
                      (QPalette.PlaceholderText, MUTED)):
        pal.setColor(role, QColor(col))
    for role in (QPalette.Text, QPalette.ButtonText, QPalette.WindowText):
        pal.setColor(QPalette.Disabled, role, QColor("#6b6f78"))
    app.setPalette(pal)
    app.setStyleSheet("QToolTip{color:%s; background:#2d3038; border:1px solid #555;}"
                      "QTextBrowser{background:%s; color:%s;}" % (TEXT, BG, TEXT))


def main():
    app = QApplication(sys.argv)
    apply_dark(app)
    win = MainWindow()
    win.resize(1150, 720)
    win.show()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()
