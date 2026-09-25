#pragma once

#include <QColor>
#include <QQuickPaintedItem>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// Draws an audio clip's overview: one vertical stroke per bucket, from its
// minimum to its maximum sample, the way every DAW shows a clip. `peaks` is
// min, max pairs flattened (AppController::clipPeaks), already shaped by the
// clip's gain and fades, so a change to what is heard is seen.
class WaveformItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariantList peaks READ peaks WRITE setPeaks NOTIFY peaksChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    // How many buckets the overview holds, so a check can tell a waveform
    // from an empty item.
    Q_PROPERTY(int buckets READ buckets NOTIFY peaksChanged)

public:
    explicit WaveformItem(QQuickItem* parent = nullptr);

    QVariantList peaks() const { return peaks_; }
    void setPeaks(const QVariantList& peaks);
    QColor color() const { return color_; }
    void setColor(const QColor& color);
    int buckets() const noexcept { return static_cast<int>(peaks_.size() / 2); }

    void paint(QPainter* painter) override;

signals:
    void peaksChanged();
    void colorChanged();

private:
    QVariantList peaks_;
    QColor color_{0x0e, 0x0f, 0x12};
};
