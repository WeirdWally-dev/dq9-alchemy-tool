// Pannable, zoomable canvas that draws a recipe tree. Drag to move, pinch or wheel to zoom,
// tap a bubble to open it.
#pragma once

#include "core/tree.h"

#include <QGraphicsView>
#include <QHash>
#include <QPixmap>
#include <QPoint>

#include <functional>

class QGraphicsPixmapItem;
class QGraphicsScene;

class TreeView : public QGraphicsView {
    Q_OBJECT
public:
    // (has an icon, pixmap or null while it downloads)
    using SpriteFn = std::function<QPair<bool, QPixmap>(const QString &)>;

    explicit TreeView(SpriteFn spriteFn, QWidget *parent = nullptr);

    void message(const QString &text);
    void showTree(const dq9::NodePtr &root);
    void setSprite(const QString &name, const QPixmap &px);

public slots:
    void fit();
    void zoomIn();
    void zoomOut();

signals:
    void nodeTapped(const dq9::NodePtr &node);
    void moreTapped(const QString &itemKey);

protected:
    bool viewportEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void addEdge(const dq9::Node &parent, const dq9::Node &child);
    void addBubble(int index, const dq9::NodePtr &node);
    void applySprite(QGraphicsPixmapItem *item, const QPixmap &px);
    void zoomBy(double factor);

    SpriteFn m_spriteFn;
    QGraphicsScene *m_scene;
    QList<dq9::NodePtr> m_nodes;
    QHash<QString, QList<QGraphicsPixmapItem *>> m_spriteItems;   // skey -> icons waiting for / showing it
    QPoint m_pressPos;
    bool m_pinching = false;
};
