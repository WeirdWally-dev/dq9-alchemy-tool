#include "app/treeview.h"

#include "app/theme.h"
#include "core/text.h"

#include <QFontMetrics>
#include <QGestureEvent>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsTextItem>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPinchGesture>
#include <QScrollBar>
#include <QWheelEvent>

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
    setScene(m_scene);
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    setBackgroundBrush(QColor(theme::BG));
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setFrameShape(QFrame::NoFrame);
    viewport()->grabGesture(Qt::PinchGesture);
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
    setSceneRect(t->sceneBoundingRect().adjusted(-20, -20, 20, 20));
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
    setSceneRect(-30, -30, width + 60, height + 60);
    fit();
}

void TreeView::fit()
{
    resetTransform();
    fitInView(sceneRect(), Qt::KeepAspectRatio);
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

void TreeView::zoomBy(double factor)
{
    const double now = transform().m11();
    const double target = std::clamp(now * factor, MIN_SCALE, MAX_SCALE);
    const auto anchor = transformationAnchor();
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    scale(target / now, target / now);
    setTransformationAnchor(anchor);
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
    if (event->type() == QEvent::Gesture) {
        auto *ge = static_cast<QGestureEvent *>(event);
        if (auto *pinch = static_cast<QPinchGesture *>(ge->gesture(Qt::PinchGesture))) {
            if (pinch->state() == Qt::GestureStarted) {
                m_pinching = true;
                setDragMode(QGraphicsView::NoDrag);
            }
            if (pinch->changeFlags() & QPinchGesture::ScaleFactorChanged) {
                const double now = transform().m11();
                const double target = std::clamp(now * pinch->scaleFactor(), MIN_SCALE, MAX_SCALE);
                const QPointF centre = mapToScene(viewport()->mapFromGlobal(pinch->centerPoint().toPoint()));
                const auto anchor = transformationAnchor();
                setTransformationAnchor(QGraphicsView::NoAnchor);
                scale(target / now, target / now);
                centerOn(centre);
                setTransformationAnchor(anchor);
            }
            if (pinch->state() == Qt::GestureFinished || pinch->state() == Qt::GestureCanceled) {
                m_pinching = false;
                setDragMode(QGraphicsView::ScrollHandDrag);
            }
            ge->accept(pinch);
            return true;
        }
    }
    return QGraphicsView::viewportEvent(event);
}

void TreeView::mousePressEvent(QMouseEvent *event)
{
    m_pressPos = event->position().toPoint();
    QGraphicsView::mousePressEvent(event);
}

void TreeView::mouseReleaseEvent(QMouseEvent *event)
{
    QGraphicsView::mouseReleaseEvent(event);
    const QPoint pos = event->position().toPoint();
    if (m_pinching || (pos - m_pressPos).manhattanLength() > 14)
        return;   // that was a drag, not a tap
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
    const double now = transform().m11();
    const double target = std::clamp(now * factor, MIN_SCALE, MAX_SCALE);
    scale(target / now, target / now);
}
