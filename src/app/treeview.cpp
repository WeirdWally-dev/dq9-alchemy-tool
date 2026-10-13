#include "app/treeview.h"

#include "app/theme.h"
#include "core/text.h"

#include <QFontMetrics>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsTextItem>
#include <QMouseEvent>
#include <QPainterPath>
#include <QScrollBar>
#include <QTouchEvent>
#include <QVariantAnimation>
#include <QWheelEvent>
#ifdef DQ9_GL_VIEWPORT
#include <QOpenGLWidget>
#include <QSurfaceFormat>
#endif

using namespace dq9;

namespace {
constexpr int SPRITE_PX = 34;   // icons are drawn at most this big inside a bubble
constexpr int ROLE_NODE = 0;    // item data: index into m_nodes
constexpr int ROLE_BADGE = 1;   // item data: true on the "+" badge
constexpr double MIN_SCALE = 0.08, MAX_SCALE = 4.0;

QFont pixelFont(int px, bool bold)
{
    QFont f;
    f.setPixelSize(px);
    f.setBold(bold);
    return f;
}
} // namespace

TreeView::TreeView(SpriteFn spriteFn, QWidget *parent)
    : QGraphicsView(parent), m_spriteFn(std::move(spriteFn)), m_scene(new QGraphicsScene(this))
{
#ifdef DQ9_GL_VIEWPORT
    // Draw with the GPU: repainting the whole tree on the CPU at a phone's resolution can't keep up
    // with a pinch, and the GL surface is synced to the display (no tearing).
    auto *gl = new QOpenGLWidget;
    QSurfaceFormat fmt = gl->format();
    fmt.setSamples(4);   // antialiased edges
    gl->setFormat(fmt);
    setViewport(gl);
#endif
    setScene(m_scene);
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    setBackgroundBrush(QColor(theme::BG));
    setDragMode(QGraphicsView::ScrollHandDrag);   // mouse; touch is handled in handleTouch()
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);   // a zoom changes everything anyway
    setOptimizationFlags(QGraphicsView::DontSavePainterState | QGraphicsView::DontAdjustForAntialiasing);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport()->setAttribute(Qt::WA_AcceptTouchEvents);
    m_zoomAnim = new QVariantAnimation(this);
    m_zoomAnim->setDuration(180);
    m_zoomAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_zoomAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        const double target = v.toDouble();
        const QPointF mid(viewport()->width() / 2.0, viewport()->height() / 2.0);
        zoomAround(mid, mid, target / currentScale());
    });
    message(QStringLiteral("Loading data..."));
}

void TreeView::message(const QString &text)
{
    m_scene->clear();
    m_nodes.clear();
    m_spriteItems.clear();
    auto *t = m_scene->addText(text, pixelFont(15, false));
    t->setDefaultTextColor(QColor(theme::TEXT));
    t->setTextWidth(std::max(220, viewport()->width() - 60));
    m_treeRect = t->sceneBoundingRect().adjusted(-20, -20, 20, 20);
    setSceneRect(m_treeRect);
    resetTransform();
    centerOn(t);
}

void TreeView::showTree(const NodePtr &root)
{
    m_scene->clear();
    m_spriteItems.clear();
    const auto [width, height] = dq9::layout(root);
    m_nodes = dq9::walk(root);
    for (const auto &n : m_nodes)
        for (const auto &c : n->children)
            addEdge(*n, *c);
    for (int i = 0; i < m_nodes.size(); ++i)
        addBubble(i, m_nodes[i]);
    m_treeRect = QRectF(-30, -30, width + 60, height + 60);
    // Room to pan the tree's edges to the middle of the screen, at any zoom.
    const double pad = 2000;
    setSceneRect(m_treeRect.adjusted(-pad, -pad, pad, pad));
    fit();
}

void TreeView::fit()
{
    m_zoomAnim->stop();
    resetTransform();
    fitInView(m_treeRect, Qt::KeepAspectRatio);
    if (transform().m11() > 1)   // zoom out to fit a big tree, but never zoom in past 100%
        resetTransform();
    // On a narrow phone a big tree fits as a speck, so start the chosen item readable instead.
    if (transform().m11() < 0.45 && !m_nodes.isEmpty()) {
        constexpr double s = 0.65;
        resetTransform();
        scale(s, s);
        const auto &root = m_nodes.first();   // the chosen item near the left edge, vertically centred
        centerOn(root->x - 16 / s + viewport()->width() / 2.0 / s, root->y + NODE_H / 2);
    }
}

void TreeView::zoomIn() { zoomBy(1.25); }
void TreeView::zoomOut() { zoomBy(1 / 1.25); }

double TreeView::currentScale() const
{
    return transform().m11();
}

void TreeView::zoomBy(double factor)
{
    // Chain onto a running animation so quick repeated taps keep zooming.
    const double from = currentScale();
    const double base = m_zoomAnim->state() == QAbstractAnimation::Running ? m_zoomAnim->endValue().toDouble() : from;
    m_zoomAnim->stop();
    m_zoomAnim->setStartValue(from);
    m_zoomAnim->setEndValue(std::clamp(base * factor, MIN_SCALE, MAX_SCALE));
    m_zoomAnim->start();
}

void TreeView::scrollByPixels(const QPointF &delta)
{
    // Scroll bars are whole pixels; carry the fractions so slow drags don't drift or stall.
    m_scrollRemainder += delta;
    const int dx = int(std::lround(m_scrollRemainder.x()));
    const int dy = int(std::lround(m_scrollRemainder.y()));
    m_scrollRemainder -= QPointF(dx, dy);
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + dx);
    verticalScrollBar()->setValue(verticalScrollBar()->value() + dy);
}

void TreeView::zoomAround(const QPointF &fromView, const QPointF &toView, double factor)
{
    const double now = currentScale();
    const double target = std::clamp(now * factor, MIN_SCALE, MAX_SCALE);
    const QPointF scenePt = viewportTransform().inverted().map(fromView);
    const auto anchor = transformationAnchor();
    setTransformationAnchor(QGraphicsView::NoAnchor);
    scale(target / now, target / now);
    setTransformationAnchor(anchor);
    scrollByPixels(viewportTransform().map(scenePt) - toView);
}

void TreeView::addEdge(const Node &parent, const Node &child)
{
    const double x1 = parent.x + NODE_W, y1 = parent.y + NODE_H / 2;
    const double x2 = child.x, y2 = child.y + NODE_H / 2;
    const double mid = (x1 + x2) / 2;
    QPainterPath path;
    path.moveTo(x1, y1);
    path.cubicTo(mid, y1, mid, y2, x2, y2);
    auto *line = m_scene->addPath(path, QPen(QColor("#7b8090"), 1.8));
    line->setZValue(0);
    if (!child.edge.isEmpty()) {   // centred on the middle of the line, on a small backing so it stays readable
        auto *lab = new QGraphicsSimpleTextItem(child.edge);
        lab->setFont(pixelFont(12, true));
        lab->setBrush(QColor("#ffd966"));
        const QRectF r = lab->boundingRect();
        const double cx = mid, cy = (y1 + y2) / 2;
        lab->setPos(cx - r.width() / 2, cy - r.height() / 2);
        lab->setZValue(3);
        auto *back = m_scene->addRect(cx - r.width() / 2 - 5, cy - r.height() / 2 - 1, r.width() + 10, r.height() + 2,
                                      QPen(QColor("#7b8090"), 1), QColor(theme::BG));
        back->setZValue(2.5);
        m_scene->addItem(lab);
    }
}

void TreeView::addBubble(int index, const NodePtr &node)
{
    QPainterPath path;
    path.addRoundedRect(QRectF(0, 0, NODE_W, NODE_H), NODE_H / 2.2, NODE_H / 2.2);
    const auto [fill, border] = theme::bubbleColors(node->kind);
    auto *bubble = m_scene->addPath(path, QPen(border, 2), fill);
    bubble->setPos(node->x, node->y);
    bubble->setZValue(1);
    bubble->setData(ROLE_NODE, index);
    const QString tip = node->tip.isEmpty() ? (node->sub.isEmpty() ? node->label : node->label + QLatin1Char('\n') + node->sub) : node->tip;
    bubble->setToolTip(tip + tr("\n\n(click for details)"));
    bubble->setCursor(Qt::PointingHandCursor);

    double left = 11;
    if (!node->sprite.isEmpty() && m_spriteFn) {
        const auto [has, px] = m_spriteFn(node->sprite);
        if (has) {   // reserve the icon's spot now; the picture is filled in once it's downloaded
            auto *icon = new QGraphicsPixmapItem(bubble);
            icon->setPos(9, (NODE_H - SPRITE_PX) / 2);
            left = 9 + SPRITE_PX + 6;
            m_spriteItems[skey(node->sprite)] << icon;
            if (!px.isNull())
                applySprite(icon, px);
        }
    }
    const double room = NODE_W - left - 11;
    auto line = [&](const QString &text, const QFont &font, const QColor &colour, double y) {
        auto *item = new QGraphicsSimpleTextItem(QFontMetrics(font).elidedText(text, Qt::ElideRight, int(room)), bubble);
        item->setFont(font);
        item->setBrush(colour);
        item->setPos(left + (room - item->boundingRect().width()) / 2, y - 4);
    };
    line(node->label, pixelFont(13, true), QColor("#f2f2f2"), node->sub.isEmpty() ? 17 : 8);
    if (!node->sub.isEmpty())
        line(node->sub, pixelFont(11, false), QColor("#c3c7d0"), 29);

    if (node->more > 0) {   // "+" badge: more monsters drop this item
        constexpr double R = 12;
        QPainterPath circle;
        circle.addEllipse(QRectF(-R, -R, 2 * R, 2 * R));
        auto *badge = new QGraphicsPathItem(circle, bubble);
        badge->setBrush(QColor("#ffd966"));
        badge->setPen(QPen(QColor(theme::BG), 2));
        badge->setPos(NODE_W - 8, 6);
        badge->setZValue(5);
        badge->setData(ROLE_NODE, index);
        badge->setData(ROLE_BADGE, true);
        auto *plus = new QGraphicsSimpleTextItem(QStringLiteral("+"), badge);
        plus->setFont(pixelFont(16, true));
        plus->setBrush(QColor("#222222"));
        const QRectF r = plus->boundingRect();
        plus->setPos(-r.width() / 2, -r.height() / 2);
    }
}

void TreeView::applySprite(QGraphicsPixmapItem *item, const QPixmap &px)
{
    if (!item || px.isNull())
        return;
    QPixmap shown = px;
    if (px.height() > SPRITE_PX || px.width() > SPRITE_PX)
        shown = px.scaled(SPRITE_PX, SPRITE_PX, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    item->setPixmap(shown);
    item->setOffset((SPRITE_PX - shown.width()) / 2.0, (SPRITE_PX - shown.height()) / 2.0);
}

void TreeView::setSprite(const QString &name, const QPixmap &px)
{
    for (auto *item : m_spriteItems.value(skey(name)))
        applySprite(item, px);
}

bool TreeView::viewportEvent(QEvent *event)
{
    switch (event->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
        return handleTouch(static_cast<QTouchEvent *>(event));
    default:
        return QGraphicsView::viewportEvent(event);
    }
}

bool TreeView::handleTouch(QTouchEvent *event)
{
    QList<QPointF> pts;
    for (const auto &p : event->points())
        if (p.state() != QEventPoint::Released)
            pts << p.position();

    if (event->type() == QEvent::TouchBegin) {
        m_zoomAnim->stop();
        m_touchStart = pts.value(0);
        m_touchMoved = false;
        m_scrollRemainder = {};
    }
    if (event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel) {
        if (event->type() == QEvent::TouchEnd && !m_touchMoved && !event->points().isEmpty())
            tapAt(event->points().first().position().toPoint());
        m_touchCount = 0;
        event->accept();
        return true;
    }

    if (pts.size() != m_touchCount) {   // a finger went down or up: start over, no jump
        // The event that adds a finger can carry a stale position for the finger already down,
        // so the baseline is taken from the next event instead.
        m_touchCount = int(pts.size());
        m_lastSpan = 0;
        if (m_touchCount >= 2)
            m_touchMoved = true;   // a pinch is never a tap
        else if (m_touchCount == 1)
            m_lastCenter = pts[0];
        event->accept();
        return true;
    }

    if (m_touchCount >= 2) {
        const QPointF center = (pts[0] + pts[1]) / 2;
        const double span = QLineF(pts[0], pts[1]).length();
        if (m_lastSpan > 10 && span > 10) {
            // Zoom and follow the fingers in one step. A single event never zooms more than 2x,
            // so one glitchy touch report can't throw the view across the tree.
            const double factor = std::clamp(span / m_lastSpan, 0.5, 2.0);
            zoomAround(m_lastCenter, center, factor);
        }
        m_lastCenter = center;
        m_lastSpan = span;
    } else if (m_touchCount == 1) {
        if (!m_touchMoved && (pts[0] - m_touchStart).manhattanLength() > 12) {
            m_touchMoved = true;   // past the tap threshold: catch up on the movement so far
            m_lastCenter = m_touchStart;
        }
        if (m_touchMoved)
            scrollByPixels(m_lastCenter - pts[0]);
        m_lastCenter = pts[0];
    }
    event->accept();
    return true;
}

void TreeView::mousePressEvent(QMouseEvent *event)
{
    m_pressPos = event->position().toPoint();
    QGraphicsView::mousePressEvent(event);
}

void TreeView::mouseReleaseEvent(QMouseEvent *event)
{
    QGraphicsView::mouseReleaseEvent(event);
    if (event->source() != Qt::MouseEventNotSynthesized)
        return;   // touch is handled in handleTouch()
    const QPoint pos = event->position().toPoint();
    if ((pos - m_pressPos).manhattanLength() > 14)
        return;   // that was a drag, not a click
    tapAt(pos);
}

void TreeView::tapAt(const QPoint &pos)
{
    // Topmost bubble or badge under the finger (badges sit above bubbles).
    for (QGraphicsItem *it : items(pos)) {
        for (QGraphicsItem *p = it; p; p = p->parentItem()) {
            const QVariant idx = p->data(ROLE_NODE);
            if (!idx.isValid())
                continue;
            const int i = idx.toInt();
            if (i < 0 || i >= m_nodes.size())
                return;
            if (p->data(ROLE_BADGE).toBool())
                emit moreTapped(m_nodes[i]->moreKey);
            else
                emit nodeTapped(m_nodes[i]);
            return;
        }
    }
}

void TreeView::wheelEvent(QWheelEvent *event)
{
    const double factor = event->angleDelta().y() > 0 ? 1.15 : 1 / 1.15;
    const QPointF at = event->position();
    zoomAround(at, at, factor);   // the point under the mouse stays put
}
