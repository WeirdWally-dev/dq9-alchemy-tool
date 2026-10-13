// The tree flattened into one shopping list: everything to gather or buy, added up across the
// whole recipe chain, plus what gets crafted along the way. Each craftable row has a tick box:
// ticked = break it down into its ingredients, unticked = list the item itself. Choices are per
// item and remembered between runs.
#pragma once

#include "core/store.h"
#include "core/tree.h"

#include <QHash>
#include <QPixmap>
#include <QWidget>

#include <functional>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;

class MaterialListPanel : public QWidget {
    Q_OBJECT
public:
    using SpriteFn = std::function<QPair<bool, QPixmap>(const QString &)>;

    MaterialListPanel(dq9::Store &store, SpriteFn spriteFn, QWidget *parent = nullptr);

    QHash<QString, bool> choices;

    void setItem(const dq9::ItemInfo *info);
    void message(const QString &text);
    void showList(const dq9::MaterialList &ml);
    void setSprite(const QString &name, const QPixmap &px);
    int count() const;
    int recipeIndex() const;

signals:
    void changed();
    void openItem(const QString &key, const QString &name);
    void copied();

private:
    void addRow(QTreeWidgetItem *parent, const dq9::ListRow &r);
    void itemChanged(QTreeWidgetItem *it, int column);
    void itemClicked(QTreeWidgetItem *it, int column);
    void reset();
    void copy();

    dq9::Store &m_store;
    SpriteFn m_spriteFn;
    QSpinBox *m_count;
    QComboBox *m_recipe;
    QLabel *m_recipeLabel;
    QLabel *m_title;
    QLabel *m_notes;
    QTreeWidget *m_tree;
    QHash<QString, QList<QTreeWidgetItem *>> m_rowsBySprite;
    std::optional<dq9::MaterialList> m_last;
    bool m_filling = false;
    bool m_justToggled = false;
};
