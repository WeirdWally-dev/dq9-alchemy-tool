#include "app/theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>

namespace theme {

QString css()
{
    return QStringLiteral(
               "<style>body{font-size:14px; color:%1; background-color:%2} h2{margin-bottom:0; color:#ffffff} "
               "h3{margin-top:18px; color:#9ec5f5} h4{margin:12px 0 4px 0; color:#c9d4e5} "
               "th{background-color:#2d3038; color:%1} td{color:%1} code{font-family:monospace} "
               "a{color:#7fb1ea} small{color:%3}</style>")
        .arg(TEXT, BG, MUTED);
}

QPair<QColor, QColor> bubbleColors(const QString &kind)
{
    static const QHash<QString, QPair<QString, QString>> colors = {
        {"item", {"#3b6ea8", "#8fbcf2"}},    {"craft", {"#2b4666", "#5b8fd0"}}, {"base", {"#2c4a30", "#6aa84f"}},
        {"recipe", {"#3d3558", "#9a88d6"}},  {"monster", {"#6b2d2d", "#e06666"}}, {"shop", {"#5c4a14", "#e0b030"}},
        {"found", {"#1f5a55", "#4fc1b5"}},   {"more", {"#33353b", "#7b7f88"}},  {"unknown", {"#33353b", "#7b7f88"}},
    };
    const auto c = colors.value(kind, colors.value(QStringLiteral("unknown")));
    return {QColor(c.first), QColor(c.second)};
}

const QList<QPair<QString, QString>> &legend()
{
    static const QList<QPair<QString, QString>> l = {
        {"item", "this item"},       {"craft", "made by alchemy"}, {"base", "base ingredient"},
        {"monster", "monster drop"}, {"shop", "sold in shops"},    {"found", "quest / chest / other"},
        {"unknown", "no known source"},
    };
    return l;
}

QString legendHtml()
{
    QString rows;
    for (const auto &[kind, text] : legend()) {
        const auto [fill, border] = bubbleColors(kind);
        rows += QStringLiteral("<tr><td style='background-color:%1; border:1px solid %2'>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;</td>"
                               "<td>&nbsp;%3</td></tr>")
                    .arg(fill.name(), border.name(), text);
    }
    return QStringLiteral("<h2>How to read the tree</h2><table cellspacing='4'>%1</table>"
                          "<h3>Moving around</h3><p>Drag to move, pinch (or the mouse wheel) to zoom, "
                          "<b>Fit</b> to see the whole tree.</p>"
                          "<p>Tap a bubble for its details. A yellow <b>+</b> badge means more monsters drop that item; "
                          "tap it to see them all.</p>"
                          "<p>Numbers on the lines are how many you need (x3), or how you get it (drop, buy, get).</p>")
        .arg(rows);
}

void applyDark(QApplication &app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QPalette pal;
    const QList<QPair<QPalette::ColorRole, QString>> roles = {
        {QPalette::Window, PANEL},        {QPalette::WindowText, TEXT},      {QPalette::Base, BG},
        {QPalette::AlternateBase, PANEL}, {QPalette::Text, TEXT},            {QPalette::Button, "#2d3038"},
        {QPalette::ButtonText, TEXT},     {QPalette::ToolTipBase, "#2d3038"}, {QPalette::ToolTipText, TEXT},
        {QPalette::Highlight, "#3b6ea8"}, {QPalette::HighlightedText, "#ffffff"}, {QPalette::Link, "#7fb1ea"},
        {QPalette::PlaceholderText, MUTED},
    };
    for (const auto &[role, col] : roles)
        pal.setColor(role, QColor(col));
    for (auto role : {QPalette::Text, QPalette::ButtonText, QPalette::WindowText})
        pal.setColor(QPalette::Disabled, role, QColor("#6b6f78"));
    app.setPalette(pal);
    app.setStyleSheet(QStringLiteral(
        "QToolTip{color:%1; background:#2d3038; border:1px solid #555;}"
        "QTextBrowser{background:%2; color:%1; border:none;}"
        "QListWidget{background:%2; border:none;} QListWidget::item{padding:10px 6px; border-bottom:1px solid #2a2c32;}"
        "QPushButton, QToolButton{padding:8px 12px; min-height:24px;}"
        "QToolButton:checked{background:#3b6ea8; color:#ffffff;}"
        "QComboBox, QLineEdit, QSpinBox{padding:6px; min-height:24px;}"
        "#pageHeader{background:#1d1f24; border-bottom:1px solid #2d3038;}"
        "#pageTitle{font-size:17px; font-weight:bold; color:#ffffff;}"
        "#muted{color:%3;}")
                          .arg(TEXT, BG, MUTED));
}

} // namespace theme
