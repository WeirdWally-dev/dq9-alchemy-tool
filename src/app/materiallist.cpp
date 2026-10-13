#include "app/materiallist.h"

#include "core/text.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

using namespace dq9;

namespace {
constexpr int ROLE_KEY = Qt::UserRole;
constexpr int ROLE_NAME = Qt::UserRole + 1;
} // namespace

MaterialListPanel::MaterialListPanel(Store &store, SpriteFn spriteFn, QWidget *parent)
    : QWidget(parent), m_store(store), m_spriteFn(std::move(spriteFn))
{
    choices = store.loadListChoices();

    m_count = new QSpinBox;
    m_count->setRange(1, 99);
    m_count->setToolTip(tr("How many of the chosen item to make"));
    connect(m_count, &QSpinBox::valueChanged, this, [this] { emit changed(); });
    m_recipe = new QComboBox;
    m_recipe->setToolTip(tr("The chosen item has more than one recipe"));
    m_recipe->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_recipe->setMinimumContentsLength(10);
    connect(m_recipe, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_filling)
            emit changed();
    });
    m_recipeLabel = new QLabel(tr("Recipe:"));
    auto *resetBtn = new QPushButton(tr("Reset ticks"));
    resetBtn->setToolTip(tr("Forget every break-down / keep-whole choice"));
    connect(resetBtn, &QPushButton::clicked, this, &MaterialListPanel::reset);
    auto *copyBtn = new QPushButton(tr("Copy list"));
    connect(copyBtn, &QPushButton::clicked, this, &MaterialListPanel::copy);
    m_title = new QLabel;
    m_title->setTextFormat(Qt::RichText);
    m_title->setWordWrap(true);

    m_tree = new QTreeWidget;
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Qty"), tr("Item and where to get it")});
    m_tree->setRootIsDecorated(false);
    m_tree->setIconSize(QSize(28, 28));
    m_tree->setWordWrap(true);
    m_tree->setTextElideMode(Qt::ElideNone);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(true);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_tree, &QTreeWidget::itemChanged, this, &MaterialListPanel::itemChanged);
    connect(m_tree, &QTreeWidget::itemClicked, this, &MaterialListPanel::itemClicked);
    m_notes = new QLabel;
    m_notes->setWordWrap(true);
    m_notes->setObjectName(QStringLiteral("muted"));

    auto *controls = new QGridLayout;
    controls->addWidget(new QLabel(tr("Make:")), 0, 0);
    controls->addWidget(m_count, 0, 1);
    controls->addWidget(resetBtn, 0, 2);
    controls->addWidget(copyBtn, 0, 3);
    controls->addWidget(m_recipeLabel, 1, 0);
    controls->addWidget(m_recipe, 1, 1, 1, 3);
    controls->setColumnStretch(1, 1);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(8, 6, 8, 6);
    lay->addWidget(m_title);
    lay->addLayout(controls);
    lay->addWidget(m_tree, 1);
    lay->addWidget(m_notes);
}

int MaterialListPanel::count() const { return m_count->value(); }
int MaterialListPanel::recipeIndex() const { return m_recipe->currentData().toInt(); }

void MaterialListPanel::setItem(const ItemInfo *info)
{
    // A new item was chosen: offer its recipes, start from 1.
    m_filling = true;
    m_recipe->clear();
    if (info) {
        for (int n = 0; n < info->recipes.size(); ++n) {
            QStringList names;
            for (const auto &i : info->recipes[n].ingredients)
                names << i.name;
            m_recipe->addItem(QStringLiteral("%1: %2").arg(n + 1).arg(names.join(QStringLiteral(", "))).left(70), n);
        }
    }
    m_recipe->setVisible(m_recipe->count() > 1);
    m_recipeLabel->setVisible(m_recipe->count() > 1);
    m_filling = false;
}

void MaterialListPanel::message(const QString &text)
{
    m_tree->clear();
    m_rowsBySprite.clear();
    m_title->setText(esc(text));
    m_notes->clear();
    m_last.reset();
}

void MaterialListPanel::showList(const MaterialList &ml)
{
    m_last = ml;
    m_filling = true;
    m_tree->clear();
    m_rowsBySprite.clear();
    m_title->setText(QStringLiteral("<b>To make %1 &times; %2</b>").arg(ml.count).arg(esc(ml.target)));
    QFont bold;
    bold.setBold(true);

    auto group = [&](const QString &text) {
        auto *g = new QTreeWidgetItem(QStringList{text, QString()});
        m_tree->addTopLevelItem(g);
        g->setFirstColumnSpanned(true);
        g->setFont(0, bold);
        g->setFlags(Qt::ItemIsEnabled);
        g->setExpanded(true);
        return g;
    };

    auto *mats = group(tr("Gather / buy  (%1 items)").arg(ml.materials.size()));
    for (const auto &r : ml.materials)
        addRow(mats, r);
    if (!ml.crafted.isEmpty()) {
        auto *done = group(tr("Craft along the way, in this order"));
        for (const auto &r : ml.crafted)
            addRow(done, r);
        auto *final = new QTreeWidgetItem(done, QStringList{QString::number(ml.count), ml.target + QStringLiteral("\nthe item you chose")});
        final->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
        final->setFlags(Qt::ItemIsEnabled);
    }
    m_notes->setText(ml.notes.join(QStringLiteral("  ")) + (ml.notes.isEmpty() ? QString() : QStringLiteral("  "))
                     + tr("Tick a craftable item to break it down, untick it to list it whole. Tap a row for details."));
    m_filling = false;
}

void MaterialListPanel::addRow(QTreeWidgetItem *parent, const ListRow &r)
{
    const QString text = r.how.isEmpty() ? r.name : r.name + QLatin1Char('\n') + r.how;
    auto *it = new QTreeWidgetItem(parent, QStringList{QString::number(r.qty), text});
    it->setData(0, ROLE_KEY, r.key);
    it->setData(0, ROLE_NAME, r.name);
    it->setTextAlignment(0, Qt::AlignRight | Qt::AlignVCenter);
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (r.craftable && !r.loop) {
        flags |= Qt::ItemIsUserCheckable;
        it->setCheckState(1, r.expanded ? Qt::Checked : Qt::Unchecked);
        it->setToolTip(1, tr("Ticked: break this down into its ingredients (they're added to the totals).\n"
                             "Unticked: list the item itself (you have it, buy it or farm it) and ignore its recipe."));
    } else if (r.loop) {
        it->setToolTip(1, tr("Its recipe needs an item further up the list, so it can't be broken down here."));
    }
    it->setFlags(flags);
    const auto [has, px] = m_spriteFn(r.name);
    if (has) {
        m_rowsBySprite[skey(r.name)] << it;
        if (!px.isNull())
            it->setIcon(1, QIcon(px));
    }
}

void MaterialListPanel::setSprite(const QString &name, const QPixmap &px)
{
    m_filling = true;   // an icon change also fires itemChanged; it's not a tick
    for (auto *it : m_rowsBySprite.value(skey(name)))
        it->setIcon(1, QIcon(px));
    m_filling = false;
}

void MaterialListPanel::itemChanged(QTreeWidgetItem *it, int column)
{
    const QString key = it->data(0, ROLE_KEY).toString();
    if (m_filling || column != 1 || key.isEmpty() || !(it->flags() & Qt::ItemIsUserCheckable))
        return;
    choices[key] = it->checkState(1) == Qt::Checked;
    m_store.saveListChoices(choices);
    m_justToggled = true;   // the same tap also arrives as a click; don't open the item for it
    QTimer::singleShot(0, this, [this] {
        m_justToggled = false;
        emit changed();   // rebuild after Qt finishes this tap
    });
}

void MaterialListPanel::itemClicked(QTreeWidgetItem *it, int)
{
    if (m_justToggled)
        return;
    const QString name = it->data(0, ROLE_NAME).toString();
    if (!name.isEmpty())
        emit openItem(it->data(0, ROLE_KEY).toString(), name);
}

void MaterialListPanel::reset()
{
    choices.clear();
    m_store.saveListChoices(choices);
    emit changed();
}

void MaterialListPanel::copy()
{
    if (m_last) {
        QApplication::clipboard()->setText(materialListText(*m_last));
        emit copied();
    }
}
