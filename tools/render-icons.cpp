// SPDX-License-Identifier: MIT
// Rebuild checked-in PNG/ICO assets from the SVG sources using Qt's renderer.
#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QTextStream>

namespace {
QImage render(const QByteArray& svg, int size) {
    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) qFatal("Invalid SVG");
    // Render at 4x and downsample so tiny Windows icons retain smooth edges.
    QImage image(size * 4, size * 4, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter);
    painter.end();
    return image.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}
void save(const QImage& image, const QString& path) {
    if (!image.save(path)) qFatal("Cannot write PNG");
}
void ico(const QByteArray& svg, const QString& path) {
    const QList<int> sizes{16, 24, 32, 48, 64, 128, 256};
    QList<QByteArray> frames;
    for (int size : sizes) {
        QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
        if (!render(svg, size).save(&buffer, "PNG")) qFatal("Cannot encode icon frame");
        frames.append(png);
    }
    QFile file(path); if (!file.open(QIODevice::WriteOnly)) qFatal("Cannot write ICO");
    QDataStream stream(&file); stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint16(0) << quint16(1) << quint16(sizes.size());
    quint32 offset = 6 + 16 * sizes.size();
    for (int i = 0; i < sizes.size(); ++i) {
        const quint8 size = sizes[i] == 256 ? 0 : quint8(sizes[i]);
        stream << size << size << quint8(0) << quint8(0) << quint16(1) << quint16(32)
               << quint32(frames[i].size()) << offset;
        offset += frames[i].size();
    }
    for (const auto& frame : frames) stream.writeRawData(frame.constData(), frame.size());
    if (stream.status() != QDataStream::Ok) qFatal("ICO write failed");
}
}
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (app.arguments().size() != 2) return 1;
    const QDir root(app.arguments()[1]);
    QByteArray remote;
    for (const QString name : {QStringLiteral("remote"), QStringLiteral("server")}) {
        QFile file(root.filePath("assets/icons/" + name + ".svg"));
        if (!file.open(QIODevice::ReadOnly)) qFatal("Cannot read SVG");
        const auto svg = file.readAll();
        save(render(svg, 512), root.filePath("assets/icons/" + name + ".png"));
        ico(svg, root.filePath("assets/icons/" + name + ".ico"));
        if (name == "remote") remote = svg;
    }
    // Remove only the source background, then inset the symbol into Android's
    // adaptive-icon safe area. Background color is a separate Android resource.
    const auto original = remote;
    remote.replace("<rect width=\"256\" height=\"256\" rx=\"56\" fill=\"#132331\"/>", "");
    remote.replace("  <g transform=\"rotate(-12 128 128)\">", "  <g transform=\"translate(128 128) scale(0.78) translate(-128 -128) rotate(-12 128 128)\">");
    const QList<QString> densities{"mdpi", "hdpi", "xhdpi", "xxhdpi", "xxxhdpi"};
    const QList<int> foregroundSizes{108, 162, 216, 324, 432};
    const QList<int> legacySizes{48, 72, 96, 144, 192};
    for (int i = 0; i < densities.size(); ++i) {
        const auto directory = root.filePath("android/res/mipmap-" + densities[i]);
        if (!QDir().mkpath(directory)) qFatal("Cannot create Android resource directory");
        auto foreground = render(remote, foregroundSizes[i]);
        save(foreground, directory + "/ic_launcher_foreground.png");
        foreground = foreground.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < foreground.height(); ++y) for (int x = 0; x < foreground.width(); ++x) {
            const auto pixel = foreground.pixel(x, y);
            const auto distance = qMax(qMax(qAbs(qRed(pixel) - 19), qAbs(qGreen(pixel) - 35)), qAbs(qBlue(pixel) - 49));
            const auto alpha = qRound(qAlpha(pixel) * qBound(0.0, distance / 100.0, 1.0));
            foreground.setPixel(x, y, qRgba(255, 255, 255, alpha));
        }
        save(foreground, directory + "/ic_launcher_monochrome.png");
        save(render(original, legacySizes[i]), directory + "/ic_launcher.png");
    }
    QTextStream(stdout) << "Rendered remote/server PNG and multi-size ICO; Android adaptive and legacy assets.\n";
}
