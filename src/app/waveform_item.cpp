#include "waveform_item.hpp"

#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cmath>

WaveformItem::WaveformItem(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(false);
}

void WaveformItem::setPeaks(const QVariantList& peaks) {
    if (peaks == peaks_) return;
    peaks_ = peaks;
    emit peaksChanged();
    update();
}

void WaveformItem::setColor(const QColor& color) {
    if (color == color_) return;
    color_ = color;
    emit colorChanged();
    update();
}

void WaveformItem::paint(QPainter* painter) {
    const auto buckets = static_cast<int>(peaks_.size() / 2);
    const double w = width();
    const double h = height();
    if (buckets > 0 && w > 0.0 && h > 0.0) {
        QPen pen(color_);
        pen.setWidthF(std::max(1.0, w / buckets));
        pen.setCapStyle(Qt::FlatCap);
        painter->setPen(pen);
        const double middle = h / 2.0;
        const double half = h / 2.0 - 1.0;
        for (int bucket = 0; bucket < buckets; ++bucket) {
            const double low = std::clamp(peaks_.at(2 * bucket).toDouble(), -1.0, 1.0);
            const double high = std::clamp(peaks_.at(2 * bucket + 1).toDouble(), -1.0, 1.0);
            const double x = (bucket + 0.5) * w / buckets;
            // At least a pixel, so silence still reads as a line through the
            // clip rather than as nothing.
            const double top = middle - high * half;
            const double bottom = std::max(top + 1.0, middle - low * half);
            painter->drawLine(QPointF(x, top), QPointF(x, bottom));
        }
    }
}
