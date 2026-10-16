#include "scope_video.hpp"

#include <QFile>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>
#include <array>
#include <cmath>

namespace blokkily {
namespace {

const QColor background{0x0e, 0x0f, 0x12};
const QColor panel{0x18, 0x1a, 0x1f};
const QColor line{0x2a, 0x2e, 0x37};
const QColor muted{0x86, 0x8b, 0x98};
const QColor ink{0xe8, 0xe9, 0xed};

float sample_at(const std::vector<float>& samples, std::int64_t index) {
    return index >= 0 && index < static_cast<std::int64_t>(samples.size())
               ? samples[static_cast<std::size_t>(index)] : 0.0F;
}

// Fits a lane's peak to 90% of its half-height, but never more than 24 dB
// of gain, so a near-silent lane stays a flat line rather than noise.
float fitted_scale(const std::vector<float>& samples) {
    float peak = 0.0F;
    for (const float s : samples) peak = std::max(peak, std::abs(s));
    return peak <= 0.0F ? 1.0F : std::min(0.9F / peak, 16.0F);
}

void draw_lane(QPainter& painter, const QRectF& area, const ScopeLane& lane,
               std::int64_t time_samples, int window, std::vector<float>& held, bool master) {
    painter.setPen(QPen(line, 1));
    painter.setBrush(panel);
    painter.drawRoundedRect(area, 6, 6);
    const QRectF inner = area.adjusted(8, 8, -8, -8);
    painter.setPen(QPen(line, 1, Qt::DashLine));
    painter.drawLine(QPointF(inner.left(), inner.center().y()),
                     QPointF(inner.right(), inner.center().y()));

    const auto trigger = scope_trigger(lane.samples, time_samples, window, held);
    const auto start = trigger - window / 2;
    held.resize(static_cast<std::size_t>(window));
    for (int i = 0; i < window; ++i)
        held[static_cast<std::size_t>(i)] = sample_at(lane.samples, start + i);

    const int points = std::max(2, static_cast<int>(inner.width()));
    QPainterPath path;
    const double half = inner.height() / 2.0;
    for (int x = 0; x < points; ++x) {
        const double position = static_cast<double>(x) * (window - 1) / (points - 1);
        const auto index = static_cast<std::size_t>(position);
        const double fraction = position - static_cast<double>(index);
        const double a = held[index];
        const double b = held[std::min(index + 1, held.size() - 1)];
        const double value = std::clamp((a + (b - a) * fraction) * lane.scale, -1.0, 1.0);
        const QPointF point(inner.left() + x, inner.center().y() - value * half);
        if (x == 0) path.moveTo(point);
        else path.lineTo(point);
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(lane.color, master ? 2.5 : 2.0, Qt::SolidLine, Qt::RoundCap,
                        Qt::RoundJoin));
    painter.drawPath(path);

    QFont font(QStringLiteral("JetBrains Mono"));
    font.setPixelSize(master ? 15 : 13);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(master ? ink : muted);
    painter.drawText(area.adjusted(12, 8, -12, -8), Qt::AlignLeft | Qt::AlignTop,
                     lane.name.toUpper());
}

// Columns for `count` lanes in a `width` x `height` area: as square as the
// lanes allow, wider than tall.
int lane_columns(int count, double width, double height) {
    int best = 1;
    double best_score = 1e9;
    for (int columns = 1; columns <= count; ++columns) {
        const int rows = (count + columns - 1) / columns;
        const double aspect = (width / columns) / (height / rows);
        const double score = std::abs(std::log(aspect / 2.2)) + 0.05 * (columns * rows - count);
        if (score < best_score) { best_score = score; best = columns; }
    }
    return best;
}

} // namespace

QString find_ffmpeg() {
    const auto configured = qEnvironmentVariable("BLOKKILY_FFMPEG");
    if (!configured.isEmpty()) return configured;
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

QColor scope_lane_color(std::size_t track) {
    static const std::array<QColor, 8> colors{
        QColor(0xc8, 0xff, 0x3d), QColor(0x4d, 0x7c, 0xff), QColor(0xff, 0xb3, 0x40),
        QColor(0xff, 0x6b, 0xc1), QColor(0x3d, 0xe0, 0xd0), QColor(0xb0, 0x8b, 0xff),
        QColor(0xff, 0x7a, 0x4d), QColor(0x8b, 0xe8, 0x6b)};
    return colors[track % colors.size()];
}

std::int64_t scope_trigger(const std::vector<float>& samples, std::int64_t centre, int window,
                           const std::vector<float>& previous) {
    const std::int64_t reach = window / 2;
    // The rising crossings within reach, nearest the centre first.
    std::vector<std::int64_t> crossings;
    for (std::int64_t i = centre - reach; i <= centre + reach; ++i)
        if (sample_at(samples, i - 1) <= 0.0F && sample_at(samples, i) > 0.0F)
            crossings.push_back(i);
    if (crossings.empty()) return centre;
    std::ranges::sort(crossings, [centre](auto a, auto b) {
        return std::abs(a - centre) < std::abs(b - centre);
    });
    if (crossings.size() > 16) crossings.resize(16);
    if (previous.size() != static_cast<std::size_t>(window)) return crossings.front();

    double previous_energy = 0.0;
    constexpr int stride = 4;
    for (int i = 0; i < window; i += stride)
        previous_energy += static_cast<double>(previous[static_cast<std::size_t>(i)]) *
                           previous[static_cast<std::size_t>(i)];
    if (previous_energy <= 1e-12) return crossings.front();
    std::int64_t best = crossings.front();
    double best_score = -1e30;
    for (const auto candidate : crossings) {
        const auto start = candidate - window / 2;
        double dot = 0.0;
        double energy = 0.0;
        for (int i = 0; i < window; i += stride) {
            const double s = sample_at(samples, start + i);
            dot += s * previous[static_cast<std::size_t>(i)];
            energy += s * s;
        }
        // Correlation with what was shown, less a little for straying from
        // the playhead, so the picture follows the song and does not lag it.
        const double correlation = dot / std::sqrt(std::max(energy, 1e-12) * previous_energy);
        const double score = correlation -
                             0.25 * static_cast<double>(std::abs(candidate - centre)) / reach;
        if (score > best_score) { best_score = score; best = candidate; }
    }
    return best;
}

void draw_scope_frame(QImage& image, const std::vector<ScopeLane>& lanes,
                      const ScopeLane& master, std::int64_t time_samples, int window,
                      std::vector<std::vector<float>>& held) {
    held.resize(lanes.size() + 1);
    image.fill(background);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const double margin = 12.0;
    const double gap = 8.0;
    const QRectF all(margin, margin, image.width() - 2 * margin, image.height() - 2 * margin);
    // The master takes the bottom third (all of it when there are no lanes).
    const double master_height = lanes.empty() ? all.height() : std::round(all.height() * 0.32);
    const QRectF master_area(all.left(), all.bottom() - master_height, all.width(), master_height);
    if (!lanes.empty()) {
        const QRectF grid(all.left(), all.top(), all.width(), all.height() - master_height - gap);
        const int count = static_cast<int>(lanes.size());
        const int columns = lane_columns(count, grid.width(), grid.height());
        const int rows = (count + columns - 1) / columns;
        const double cell_w = (grid.width() - gap * (columns - 1)) / columns;
        const double cell_h = (grid.height() - gap * (rows - 1)) / rows;
        for (int i = 0; i < count; ++i) {
            const int row = i / columns;
            const int column = i % columns;
            // The last row's lanes share its whole width.
            const int in_row = row == rows - 1 ? count - row * columns : columns;
            const double width = (grid.width() - gap * (in_row - 1)) / in_row;
            const QRectF area(grid.left() + column * (width + gap),
                              grid.top() + row * (cell_h + gap), in_row == columns ? cell_w : width,
                              cell_h);
            draw_lane(painter, area, lanes[static_cast<std::size_t>(i)], time_samples, window,
                      held[static_cast<std::size_t>(i)], false);
        }
    }
    draw_lane(painter, master_area, master, time_samples, window, held.back(), true);
}

std::optional<ScopeVideoReport> render_scope_video(
    const QString& ffmpeg, const std::filesystem::path& output, const std::filesystem::path& audio,
    double sample_rate, std::vector<ScopeLane> lanes, ScopeLane master,
    const ScopeVideoOptions& options,
    const std::function<void(int frame, int frames)>& progress, std::string* error) {
    const auto fail = [error](const QString& message) -> std::optional<ScopeVideoReport> {
        if (error != nullptr) *error = message.toStdString();
        return std::nullopt;
    };
    if (ffmpeg.isEmpty()) return fail(QStringLiteral("ffmpeg was not found on PATH"));
    if (sample_rate <= 0.0 || options.fps <= 0 || options.width < 64 || options.height < 64)
        return fail(QStringLiteral("invalid video settings"));
    // H.264 in 4:2:0 wants even dimensions.
    const int width = options.width & ~1;
    const int height = options.height & ~1;
    for (auto& lane : lanes) lane.scale = fitted_scale(lane.samples);
    master.scale = fitted_scale(master.samples);
    const auto length = master.samples.size();
    const int frames = std::max(1, static_cast<int>(std::ceil(
        static_cast<double>(length) / sample_rate * options.fps)));
    const int window = std::max(16, static_cast<int>(std::lround(options.window_seconds * sample_rate)));

    std::error_code failure;
    if (const auto parent = output.parent_path(); !parent.empty())
        std::filesystem::create_directories(parent, failure);
    const auto log_path = QString::fromStdString((output.string() + ".ffmpeg.log"));
    QProcess process;
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(log_path);
    process.start(ffmpeg, {
        QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"), QStringLiteral("-y"),
        QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-pix_fmt"),
        // QImage::Format_RGB32 is 0xffRRGGBB per pixel: B, G, R, 0xff in
        // little-endian memory.
        Q_BYTE_ORDER == Q_LITTLE_ENDIAN ? QStringLiteral("bgr0") : QStringLiteral("0rgb"),
        QStringLiteral("-s"), QStringLiteral("%1x%2").arg(width).arg(height),
        QStringLiteral("-r"), QString::number(options.fps),
        QStringLiteral("-i"), QStringLiteral("pipe:0"),
        QStringLiteral("-i"), QString::fromStdString(audio.string()),
        QStringLiteral("-map"), QStringLiteral("0:v"), QStringLiteral("-map"), QStringLiteral("1:a"),
        QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"),
        QStringLiteral("medium"), QStringLiteral("-crf"), QStringLiteral("18"),
        QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
        QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-b:a"), QStringLiteral("320k"),
        QStringLiteral("-movflags"), QStringLiteral("+faststart"),
        QString::fromStdString(output.string())});
    if (!process.waitForStarted(10000))
        return fail(QStringLiteral("ffmpeg did not start: %1").arg(process.errorString()));

    QImage image(width, height, QImage::Format_RGB32);
    std::vector<std::vector<float>> held;
    bool written = true;
    for (int frame = 0; frame < frames && written; ++frame) {
        // The frame shows what is heard at its middle.
        const auto time = static_cast<std::int64_t>(std::llround(
            (frame + 0.5) * sample_rate / options.fps));
        draw_scope_frame(image, lanes, master, time, window, held);
        const auto bytes = image.sizeInBytes();
        written = process.write(reinterpret_cast<const char*>(image.constBits()), bytes) == bytes;
        while (written && process.bytesToWrite() > 0)
            written = process.waitForBytesWritten(60000);
        if (progress) progress(frame + 1, frames);
    }
    process.closeWriteChannel();
    const bool finished = process.waitForFinished(600000);
    if (!written || !finished || process.exitStatus() != QProcess::NormalExit ||
        process.exitCode() != 0) {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(5000);
        }
        QFile log(log_path);
        QString detail = log.open(QIODevice::ReadOnly)
                             ? QString::fromUtf8(log.readAll()).trimmed().section('\n', -1)
                             : QString();
        return fail(QStringLiteral("ffmpeg failed%1").arg(detail.isEmpty() ? QString()
                                                                          : " · " + detail));
    }
    QFile::remove(log_path);
    return ScopeVideoReport{frames, static_cast<double>(frames) / options.fps};
}

} // namespace blokkily
