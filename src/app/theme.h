// Dark theme colours shared by every screen.
#pragma once

#include <QColor>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>

class QApplication;

namespace theme {

inline const QString BG = QStringLiteral("#17181c");
inline const QString PANEL = QStringLiteral("#22242a");
inline const QString TEXT = QStringLiteral("#e6e6e6");
inline const QString MUTED = QStringLiteral("#a0a4ad");

// CSS put in front of every info page.
QString css();

// bubble colours: kind -> (fill, border)
QPair<QColor, QColor> bubbleColors(const QString &kind);

// (kind, meaning) in legend order
const QList<QPair<QString, QString>> &legend();
QString legendHtml();

void applyDark(QApplication &app);

} // namespace theme
