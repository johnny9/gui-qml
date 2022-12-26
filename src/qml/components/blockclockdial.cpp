#include "blockclockdial.h"

#include <QPainterPath>
#include <QPen>
#include <QColor>
#include <QtMath>

BlockClockDial::BlockClockDial(QQuickItem *parent)
: QQuickPaintedItem(parent)
{
}

void BlockClockDial::paint(QPainter * painter)
{
    if (width() <= 0 || height() <= 0)
        return;

    QPen pen(QColor("yellow"));
    pen.setWidth(4);
    pen.setCapStyle(Qt::RoundCap);
    painter->setPen(pen);

    const QRectF bounds = boundingRect();
    const qreal smallest = qMin(bounds.width(), bounds.height());
    QRectF rect = QRectF(pen.widthF() / 2.0 + 1, pen.widthF() / 2.0 + 1, smallest - pen.widthF() - 2, smallest - pen.widthF() - 2);
    rect.moveCenter(bounds.center());

    // Make sure the arc is aligned to whole pixels.
    if (rect.x() - int(rect.x()) > 0)
        rect.setX(qCeil(rect.x()));
    if (rect.y() - int(rect.y()) > 0)
        rect.setY(qCeil(rect.y()));
    if (rect.width() - int(rect.width()) > 0)
        rect.setWidth(qFloor(rect.width()));
    if (rect.height() - int(rect.height()) > 0)
        rect.setHeight(qFloor(rect.height()));

    painter->setRenderHint(QPainter::Antialiasing);

    int numberOfBlocks = 30;
    for (int i = 0; i < numberOfBlocks; i++) {
        if (numberOfBlocks - i == 6) {
            QPen pen(QColor("orange"));
            pen.setWidth(4);
            pen.setCapStyle(Qt::RoundCap);
            painter->setPen(pen);
        }
        if (numberOfBlocks - i == 1) {
            QPen pen(QColor("red"));
            pen.setWidth(4);
            pen.setCapStyle(Qt::RoundCap);
            painter->setPen(pen);
        }


        const qreal startAngle = 90 + (-8 * i);
        const qreal spanAngle = -5;
        QPainterPath path;
        path.arcMoveTo(rect, startAngle);
        path.arcTo(rect, startAngle, spanAngle);
        painter->drawPath(path);
    }
}
