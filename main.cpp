// =========================================================
//  ascii-maker — Imagen / GIF / video -> ASCII con color RGBA
//  - Un caracter por pixel (rejilla 1:1) en imagenes fijas
//  - GIF y video -> ASCII animado, exportado a WebP animado
//  - Clic en la vista ASCII para seleccionar/editar celdas
//  - Exportar a: WebP (fijo o animado), PNG, TXT, HTML
// =========================================================

#include <QApplication>
#include <QMainWindow>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QDockWidget>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QColorDialog>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QSplitter>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QSlider>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QScrollArea>
#include <QPainter>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QtMath>
#include <QVector>
#include <QStyleOptionGraphicsItem>
#include <algorithm>
#include <QMovie>
#include <QTimer>
#include <QProcess>
#include <QStandardPaths>
#include <QEventLoop>
#include <QUrl>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QVideoFrame>

// ---------------------------------------------------------------------
struct AsciiCell {
    char    ch = ' ';
    quint8  r = 0, g = 0, b = 0, a = 255;
    bool    manual = false;    // editada a mano por el usuario
};

// ---------------------------------------------------------------------
// Rejilla tal y como se dibuja (preview, PNG y HTML).
// Un carácter monoespaciado no es cuadrado: ocupa bastante más alto que
// ancho. Por eso viewGrid() promedia las filas de la rejilla 1:1 en vez de
// usar una fila por píxel, que es lo que dejaba el arte estirado.
struct AsciiGrid {
    int                cols = 0, rows = 0;
    QVector<AsciiCell> cells;
    bool isNull() const { return cols <= 0 || rows <= 0; }
};

// =====================================================================
// AsciiMaker
// =====================================================================
class AsciiMaker {
public:
    QString ramp           = QStringLiteral(" .:-=+*#%@");
    bool    invert         = false;
    bool    autoLevels     = false;
    double  brightness     = 1.0;
    double  contrast       = 1.0;
    bool    alphaAsSpace   = false;
    int     alphaThreshold = 128;
    double  scale          = 1.0;
    // Cuánto más alto es un carácter que ancho. Un carácter no es un
    // cuadrado, así que sin esto el arte sale estirado. Se mide el valor
    // real de la fuente (ver MainWindow::aspectoDeFuente).
    double  aspecto        = 2.0;

    // Tope de tamaño del arte, en caracteres. 0 = sin tope. Se aplican
    // reescalando la imagen.
    int     limiteCols     = 0;
    int     limiteRows     = 0;
    // Al aplicar los topes, ¿se mantiene la proporción de la original?
    // Sí (por defecto): los topes se linkedan por el más restrictivo y la
    // imagen solo se encoge. No: cada tope manda por su lado y el arte se
    // deforma hasta clavarse en la rejilla pedida. Un tope sin poner no
    // restringe su eje, así que desmarcado también deforma con uno solo.
    bool    conservarProporcion = true;

    QImage source, adjusted;
    int    cols = 0, rows = 0;
    QVector<AsciiCell> cells;

    // Filas de la rejilla de dibujo de una rejilla de `rows` filas.
    int viewRows(int rows) const {
        if (rows <= 0) return 0;
        return qMax(1, int(std::lround(rows / qMax(1.0, aspecto))));
    }

    // Devuelve la celda (x, y) de la rejilla 1:1 que se ve en esa celda
    // del dibujo. Si el bloque tiene ediciones manuales gana la primera.
    QPair<int, int> viewToSource(const AsciiGrid &base, int cx, int cy) const {
        if (base.isNull()) return {-1, -1};
        const int vr = viewRows(base.rows);
        if (vr >= base.rows) return {cx, cy};
        const int y0 = qBound(0, int(qint64(cy)  * base.rows / vr),
                              base.rows - 1);
        const int y1 = qBound(y0 + 1, int(qint64(cy + 1) * base.rows / vr),
                              base.rows);
        for (int y = y0; y < y1; ++y)
            if (base.cells[y * base.cols + cx].manual) return {cx, y};
        return {cx, y0};
    }

    // Inversa: la celda del dibujo donde acaba cayendo un píxel 1:1.
    // El ancho no cambia; solo se reparte el alto.
    QPair<int, int> sourceToView(const AsciiGrid &base, int sx, int sy) const {
        if (base.isNull()) return {-1, -1};
        const int vr = viewRows(base.rows);
        if (vr >= base.rows) return {sx, sy};
        return {sx, qBound(0, int(qint64(sy) * vr / base.rows), vr - 1)};
    }

    // Rejilla de dibujo a partir de una rejilla 1:1 cualquiera (la de la
    // imagen fija actual, o la de un frame de la animación). Promedia las
    // filas para que el alto del carácter no estire la imagen. Las
    // ediciones manuales no se promedian.
    AsciiGrid construirVista(const AsciiGrid &base) const {
        AsciiGrid v;
        v.cols  = base.cols;
        v.rows  = viewRows(base.rows);
        v.cells = base.cells;

        if (v.isNull() || v.rows >= base.rows) return v;

        v.cells.resize(qint64(v.cols) * v.rows);
        for (int y = 0; y < v.rows; ++y) {
            const int y0 = qBound(0, int(qint64(y)     * base.rows / v.rows),
                                  base.rows - 1);
            const int y1 = qBound(y0 + 1,
                                  int(qint64(y + 1) * base.rows / v.rows),
                                  base.rows);
            for (int x = 0; x < v.cols; ++x) {
                const AsciiCell *manual = nullptr;
                int sr = 0, sg = 0, sb = 0, sa = 0, n = 0;
                for (int yy = y0; yy < y1; ++yy) {
                    const AsciiCell &c = base.cells[yy * base.cols + x];
                    if (c.manual) { if (!manual) manual = &c; continue; }
                    sr += c.r; sg += c.g; sb += c.b; sa += c.a; ++n;
                }
                AsciiCell dst;
                if (manual) {
                    dst = *manual;
                } else if (n > 0) {
                    dst.r = quint8((sr + n / 2) / n);
                    dst.g = quint8((sg + n / 2) / n);
                    dst.b = quint8((sb + n / 2) / n);
                    dst.a = quint8((sa + n / 2) / n);
                    dst.ch = caracterPara(dst.r, dst.g, dst.b);
                    if (alphaAsSpace && tieneAlpha_ &&
                        dst.a < alphaThreshold) dst.ch = ' ';
                }
                v.cells[qint64(y) * v.cols + x] = dst;
            }
        }
        return v;
    }

    void setSource(const QImage &img) {
        source = img.convertToFormat(QImage::Format_ARGB32);
        rebuild(true);
    }

    // Igual que setSource pero sin arrastrar las ediciones manuales del
    // frame anterior: es lo que se usa al convertir cada frame.
    void setFrameSource(const QImage &img) {
        source = img.convertToFormat(QImage::Format_ARGB32);
        rebuild(false);
    }

    void rebuild(bool conservarManuales = true) {
        // Conservamos las ediciones manuales por índice si el tamaño no cambió.
        const QVector<AsciiCell> old = cells;
        const int oldCols = cols, oldRows = rows;

        cells.clear();
        cols = rows = 0;
        adjusted = QImage();
        if (source.isNull() || ramp.isEmpty()) return;

        QImage work = source;
        if (std::abs(scale - 1.0) > 0.001) {
            int w = qMax(1, int(std::lround(work.width()  * scale)));
            int h = qMax(1, int(std::lround(work.height() * scale)));
            work = work.scaled(w, h, Qt::IgnoreAspectRatio,
                               Qt::SmoothTransformation);
        }

        // Topes de tamaño. Van despues de la escala, asi que 1 caracter sigue
        // valiendo 1 pixel de la imagen ya reescalada.
        //
        // Con conservarProporcion los dos topes se.linkedan por el mas
        // restrictivo y la imagen solo se encoge. Sin ella, cada tope manda
        // por su lado y el arte se deforma hasta clavarse en la rejilla
        // pedida. Un tope sin poner no restringe su eje, asi que
        // basta uno solo para deformar.
        if (limiteCols > 0 || limiteRows > 0) {
            const int w = work.width(), h = work.height();
            if (!conservarProporcion) {
                const int nw = limiteCols > 0 ? limiteCols : w;
                const int nh = limiteRows > 0 ? limiteRows : h;
                if (nw != w || nh != h)
                    work = work.scaled(nw, nh, Qt::IgnoreAspectRatio,
                                       Qt::SmoothTransformation);
            } else {
                double f = 1.0;
                if (limiteCols > 0) f = std::min(f, double(limiteCols) / w);
                if (limiteRows > 0) f = std::min(f, double(limiteRows) / h);
                if (f < 1.0)
                    work = work.scaled(qMax(1, int(w * f)),
                                       qMax(1, int(h * f)),
                                       Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation);
            }
        }

        adjusted = work;
        if (autoLevels) adjusted = aplicarAutoNiveles(adjusted);

        if (std::abs(brightness - 1.0) > 0.001 ||
            std::abs(contrast   - 1.0) > 0.001) {
            auto adj = [&](int v) {
                double f = v / 255.0;
                f *= brightness;
                f = (f - 0.5) * contrast + 0.5;
                return int(qBound(0.0, f, 1.0) * 255.0 + 0.5);
            };
            for (int y = 0; y < adjusted.height(); ++y) {
                QRgb *line = reinterpret_cast<QRgb *>(adjusted.scanLine(y));
                for (int x = 0; x < adjusted.width(); ++x) {
                    QRgb p = line[x];
                    line[x] = qRgba(adj(qRed(p)), adj(qGreen(p)),
                                    adj(qBlue(p)), qAlpha(p));
                }
            }
        }

        cols = adjusted.width();
        rows = adjusted.height();
        tieneAlpha_ = adjusted.hasAlphaChannel();
        if (cols <= 0 || rows <= 0) return;

        cells.reserve(cols * rows);

        for (int y = 0; y < rows; ++y) {
            const QRgb *line =
                reinterpret_cast<const QRgb *>(adjusted.constScanLine(y));
            for (int x = 0; x < cols; ++x) {
                QRgb p = line[x];
                int r = qRed(p), g = qGreen(p), b = qBlue(p), a = qAlpha(p);

                char c = caracterPara(r, g, b);
                if (alphaAsSpace && tieneAlpha_ && a < alphaThreshold) c = ' ';

                AsciiCell cell;
                cell.ch = c;
                cell.r = quint8(r); cell.g = quint8(g);
                cell.b = quint8(b); cell.a = quint8(a);
                cells.append(cell);
            }
        }

        // Reaplicar ediciones manuales
        if (conservarManuales && oldCols == cols && oldRows == rows &&
            old.size() == cells.size()) {
            for (int i = 0; i < cells.size(); ++i) {
                if (old[i].manual) {
                    AsciiCell &nuevo = cells[i];
                    const AsciiCell &viejo = old[i];
                    nuevo.ch = viejo.ch;
                    nuevo.r = viejo.r; nuevo.g = viejo.g;
                    nuevo.b = viejo.b; nuevo.a = viejo.a;
                    nuevo.manual = true;
                }
            }
        }
    }

    QString renderToText(const AsciiGrid &base) const {
        QString out;
        out.reserve(qint64(base.cols) * (base.rows + 1));
        for (int y = 0; y < base.rows; ++y) {
            const int off = y * base.cols;
            for (int x = 0; x < base.cols; ++x)
                out.append(QChar::fromLatin1(base.cells[off + x].ch));
            out.append(QLatin1Char('\n'));
        }
        return out;
    }

    QString renderToHtml(const AsciiGrid &base, const QColor &fondo,
                         double fontPx) const {
        const AsciiGrid g = construirVista(base);
        const int cols = g.cols, rows = g.rows;
        QString out;
        out.reserve(qint64(cols) * rows * 24 + 256);
        out += QStringLiteral(
            "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
            "<title>ASCII</title><style>"
            "body{margin:0;background:%1;}"
            "pre{font-family:'DejaVu Sans Mono','Noto Sans Mono',Consolas,monospace;"
            "font-size:%2px;line-height:1;letter-spacing:0;"
            "margin:0;padding:8px;white-space:pre;}"
            "</style></head><body><pre>")
            .arg(fondo.name()).arg(QString::number(fontPx, 'f', 2));
        for (int y = 0; y < rows; ++y) {
            const int off = y * cols;
            for (int x = 0; x < cols; ++x) {
                const AsciiCell &c = g.cells[off + x];
                if (c.ch == ' ') { out += QLatin1Char(' '); continue; }
                out += QStringLiteral(
                    "<span style=\"color:#%1%2%3\">%4</span>")
                    .arg(c.r, 2, 16, QLatin1Char('0'))
                    .arg(c.g, 2, 16, QLatin1Char('0'))
                    .arg(c.b, 2, 16, QLatin1Char('0'))
                    .arg(QString(QChar::fromLatin1(c.ch)).toHtmlEscaped());
            }
            out += QLatin1Char('\n');
        }
        out += QStringLiteral("</pre></body></html>");
        return out;
    }

    QImage renderToImage(const AsciiGrid &base, const QFont &font,
                         const QColor &fondo, bool *tooBig = nullptr) const {
        if (tooBig) *tooBig = false;
        const AsciiGrid g = construirVista(base);
        if (g.isNull()) return QImage();

        QFontMetricsF fm(font);
        const double cw  = fm.horizontalAdvance(QLatin1Char('M'));
        const double ch  = fm.height();
        const double asc = fm.ascent();
        if (cw <= 0 || ch <= 0) return QImage();

        const qint64 W = qint64(std::ceil(g.cols * cw));
        const qint64 H = qint64(std::ceil(g.rows * ch));
        const qint64 LIM_LADO   = 32000;
        const qint64 LIM_PIXELES = 300LL * 1000 * 1000;
        if (W > LIM_LADO || H > LIM_LADO || W * H > LIM_PIXELES) {
            if (tooBig) *tooBig = true;
            return QImage();
        }

        QImage out(int(W), int(H), QImage::Format_ARGB32);
        if (out.isNull()) { if (tooBig) *tooBig = true; return QImage(); }
        out.fill(fondo);

        QPainter p(&out);
        p.setFont(font);
        p.setRenderHint(QPainter::TextAntialiasing, true);

        for (int y = 0; y < g.rows; ++y) {
            const int off = y * g.cols;
            const double yPx = y * ch + asc;
            for (int x = 0; x < g.cols; ++x) {
                const AsciiCell &c = g.cells[off + x];
                if (c.ch == ' ') continue;
                p.setPen(QColor(c.r, c.g, c.b, c.a));
                p.drawText(QPointF(x * cw, yPx), QChar::fromLatin1(c.ch));
            }
        }
        p.end();
        return out;
    }

private:
    char caracterPara(int r, int g, int b) const {
        const int n = ramp.size();
        if (n <= 0) return ' ';
        double lum = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255.0;
        if (invert) lum = 1.0 - lum;
        const int idx = qBound(0, int(lum * (n - 1) + 0.5), n - 1);
        return ramp.at(idx).toLatin1();
    }

    bool tieneAlpha_ = false;

    static QImage aplicarAutoNiveles(const QImage &src) {
        QImage gris = src.convertToFormat(QImage::Format_Grayscale8);
        qint64 hist[256] = {0};
        for (int y = 0; y < gris.height(); ++y) {
            const uchar *line = gris.constScanLine(y);
            for (int x = 0; x < gris.width(); ++x) ++hist[line[x]];
        }
        const qint64 total = qint64(gris.width()) * gris.height();
        if (total == 0) return src;
        int lo = 0, hi = 255;
        qint64 acc = 0;
        for (int v = 0; v < 256; ++v) {
            acc += hist[v];
            if (acc >= total * 0.005) { lo = v; break; }
        }
        acc = 0;
        for (int v = 0; v < 256; ++v) {
            acc += hist[v];
            if (acc >= total * 0.995) { hi = v; break; }
        }
        if (hi - lo < 16) return src;
        const double factor = 255.0 / double(hi - lo);
        uchar lut[256];
        for (int i = 0; i < 256; ++i)
            lut[i] = uchar(qBound(0, int((i - lo) * factor + 0.5), 255));
        QImage out = src.convertToFormat(QImage::Format_ARGB32);
        for (int y = 0; y < out.height(); ++y) {
            QRgb *line = reinterpret_cast<QRgb *>(out.scanLine(y));
            for (int x = 0; x < out.width(); ++x) {
                QRgb p = line[x];
                line[x] = qRgba(lut[qRed(p)], lut[qGreen(p)],
                                lut[qBlue(p)], qAlpha(p));
            }
        }
        return out;
    }
};

// =====================================================================
// AsciiItem
// =====================================================================
class AsciiItem : public QGraphicsItem {
public:
    // La rejilla que se dibuja. La mantiene viva MainWindow (vista_), que
    // la reemplaza por una nueva cuando cambia la imagen o el aspecto.
    AsciiItem(const AsciiGrid *grid, const QFont &font)
        : grid_(grid), font_(font) {
        recalcularMetricas();
        setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
    }

    // La apunto a otra rejilla del mismo tamaño (o de otro): hay que
    // recalcular la geometría porque boundingRect depende de las filas.
    void setGrid(const AsciiGrid *grid) {
        prepareGeometryChange();
        grid_ = grid;
        update();
    }

    QRectF boundingRect() const override {
        if (!grid_ || grid_->isNull()) return QRectF();
        return QRectF(0, 0, grid_->cols * cw_, grid_->rows * ch_);
    }

    // Devuelve (col, fila) o (-1,-1) si está fuera
    QPair<int,int> cellAt(const QPointF &pos) const {
        if (!grid_ || grid_->cols <= 0) return {-1, -1};
        int x = int(std::floor(pos.x() / cw_));
        int y = int(std::floor(pos.y() / ch_));
        if (x < 0 || x >= grid_->cols || y < 0 || y >= grid_->rows)
            return {-1, -1};
        return {x, y};
    }

    void setSelection(int col, int row) {
        if (col == selCol_ && row == selRow_) return;
        const QRectF vieja = celdaRect(selCol_, selRow_);
        selCol_ = col; selRow_ = row;
        const QRectF nueva = celdaRect(selCol_, selRow_);
        if (!vieja.isEmpty()) update(vieja.adjusted(-2, -2, 2, 2));
        if (!nueva.isEmpty()) update(nueva.adjusted(-2, -2, 2, 2));
    }

    void setFont(const QFont &f) {
        prepareGeometryChange();
        font_ = f;
        recalcularMetricas();
        update();
    }

    double cellW() const { return cw_; }
    double cellH() const { return ch_; }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *opt,
               QWidget *) override {
        if (!grid_ || grid_->isNull()) return;

        const int N = grid_->cols;
        const int M = grid_->rows;

        QRectF r = opt->exposedRect;
        if (r.isEmpty()) r = boundingRect();

        int x0 = qMax(0,     int(std::floor(r.left()   / cw_)));
        int x1 = qMin(N - 1, int(std::ceil (r.right()  / cw_)));
        int y0 = qMax(0,     int(std::floor(r.top()    / ch_)));
        int y1 = qMin(M - 1, int(std::ceil (r.bottom() / ch_)));
        if (x1 < x0 || y1 < y0) return;

        const QTransform t = p->transform();
        const double det = std::abs(t.determinant());
        const double escala = det > 0 ? std::sqrt(det) : 1.0;
        const double devFont = font_.pointSizeF() * escala;

        if (devFont < 4.0) {
            const double pw = qMax(1.0, cw_ * 0.85);
            const double ph = qMax(1.0, ch_ * 0.85);
            for (int y = y0; y <= y1; ++y) {
                const int off = y * N;
                for (int x = x0; x <= x1; ++x) {
                    const AsciiCell &c = grid_->cells[off + x];
                    if (c.ch == ' ') continue;
                    p->fillRect(QRectF(x * cw_, y * ch_, pw, ph),
                                QColor(c.r, c.g, c.b, c.a));
                }
            }
        } else {
            p->setFont(font_);
            for (int y = y0; y <= y1; ++y) {
                const int off = y * N;
                const double yPx = y * ch_ + asc_;
                for (int x = x0; x <= x1; ++x) {
                    const AsciiCell &c = grid_->cells[off + x];
                    if (c.ch == ' ') continue;
                    p->setPen(QColor(c.r, c.g, c.b, c.a));
                    p->drawText(QPointF(x * cw_, yPx), QChar::fromLatin1(c.ch));
                }
            }
        }

        // marco de la selección
        if (selCol_ >= 0 && selRow_ >= 0 &&
            selCol_ < N && selRow_ < M) {
            p->setBrush(Qt::NoBrush);
            p->setPen(QPen(QColor(255, 220, 60), 0));
            p->drawRect(celdaRect(selCol_, selRow_).adjusted(0.5, 0.5, -0.5, -0.5));
        }
    }

private:
    void recalcularMetricas() {
        QFontMetricsF fm(font_);
        cw_  = fm.horizontalAdvance(QLatin1Char('M'));
        ch_  = fm.height();
        asc_ = fm.ascent();
    }

    QRectF celdaRect(int col, int row) const {
        if (col < 0 || row < 0) return QRectF();
        return QRectF(col * cw_, row * ch_, cw_, ch_);
    }

    const AsciiGrid *grid_;
    QFont      font_;
    double     cw_ = 0, ch_ = 0, asc_ = 0;
    int        selCol_ = -1, selRow_ = -1;
};

// =====================================================================
// AsciiView
// =====================================================================
class AsciiView : public QGraphicsView {
    Q_OBJECT
public:
    explicit AsciiView(QWidget *parent = nullptr) : QGraphicsView(parent) {
        setDragMode(QGraphicsView::ScrollHandDrag);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setResizeAnchor(QGraphicsView::AnchorViewCenter);
        setRenderHint(QPainter::SmoothPixmapTransform, false);
        setBackgroundBrush(Qt::black);
        setMouseTracking(true);
    }

    void zoomIn()    { scale(1.15, 1.15); }
    void zoomOut()   { scale(1.0 / 1.15, 1.0 / 1.15); }
    void zoomReset() { resetTransform(); }
    void fitAll() {
        if (scene() && !scene()->items().isEmpty())
            fitInView(scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
    }

signals:
    void cellClicked(const QPointF &scenePos);

protected:
    void wheelEvent(QWheelEvent *e) override {
        const double f = e->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        scale(f, f);
        e->accept();
    }
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            pressPos_ = e->position().toPoint();
            dragging_ = false;
        }
        QGraphicsView::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if ((e->buttons() & Qt::LeftButton) &&
            (e->position().toPoint() - pressPos_).manhattanLength() > 4) {
            dragging_ = true;
        }
        QGraphicsView::mouseMoveEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton && !dragging_) {
            emit cellClicked(mapToScene(e->position().toPoint()));
        }
        QGraphicsView::mouseReleaseEvent(e);
    }

private:
    QPoint pressPos_;
    bool   dragging_ = false;
};

// =====================================================================
// MainWindow
// =====================================================================
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr) : QMainWindow(parent) {
        setupUi();
        setupActions();
        setWindowTitle(QStringLiteral("ASCII Maker"));
        resize(1600, 950);
    }

private:
    // Un frame ya convertido. Para una imagen fija solo hay uno.
    struct Frame {
        QImage    src;      // imagen original del frame
        AsciiGrid base;     // rejilla 1:1 (un carácter por píxel)
        int       delayMs = 66;
        // Ediciones manuales del usuario: (índice de celda, celda). Solo
        // las editadas, que son pocas, para no duplicar la rejilla entera.
        QVector<QPair<int, AsciiCell>> manuales;
    };

    AsciiMaker maker_;
    AsciiGrid  vista_;          // rejilla de dibujo (aspecto corregido)
    QImage     originalImage_;
    QString    currentPath_;

    // animación
    QVector<Frame> frames_;
    int       frameActual_ = 0;
    bool      esAnimacion() const { return frames_.size() > 1; }
    QTimer   *reloj_       = nullptr;
    bool      reproduciendo_ = false;
    QMediaPlayer *player_  = nullptr;
    QVideoSink   *sink_    = nullptr;
    bool      capturando_ = false;   // captura de frames en curso
    bool      procesando_ = false;   // convierte el frame actual
    QWidget  *gbAnim_     = nullptr;
    QLabel   *animLabel_  = nullptr;
    QPushButton *playBtn_ = nullptr;
    QSlider  *frameSlider_ = nullptr;
    QCheckBox *loopCheck_ = nullptr;

    AsciiView      *origView_   = nullptr;
    QGraphicsScene *origScene_  = nullptr;
    AsciiView      *asciiView_  = nullptr;
    QGraphicsScene *asciiScene_ = nullptr;
    AsciiItem      *asciiItem_  = nullptr;
    QLabel         *statusLabel_ = nullptr;

    // ajustes
    QLineEdit      *rampEdit_       = nullptr;
    QCheckBox      *invertCheck_    = nullptr;
    QCheckBox      *autoCheck_      = nullptr;
    QSlider        *brightSlider_   = nullptr;
    QLabel         *brightLabel_    = nullptr;
    QSlider        *contrastSlider_ = nullptr;
    QLabel         *contrastLabel_  = nullptr;
    QCheckBox      *alphaCheck_     = nullptr;
    QSpinBox       *alphaSpin_      = nullptr;
    QDoubleSpinBox *scaleSpin_      = nullptr;
    QSpinBox       *fontSizeSpin_   = nullptr;
    QDoubleSpinBox *aspectoSpin_    = nullptr;
    QSpinBox       *limColsSpin_    = nullptr;
    QSpinBox       *limRowsSpin_    = nullptr;
    QSpinBox       *limFramesSpin_  = nullptr;
    QCheckBox      *ratioCheck_    = nullptr;
    QSpinBox       *qualitySpin_   = nullptr;
    QCheckBox      *losslessCheck_ = nullptr;
    QPushButton    *bgButton_       = nullptr;
    QColor          bgColor_        = Qt::black;

    // editor de celda
    QGroupBox  *editorBox_      = nullptr;
    QLabel     *celdaInfoLabel_ = nullptr;
    QLineEdit  *charEdit_       = nullptr;
    QSpinBox   *rSpin_ = nullptr, *gSpin_ = nullptr,
               *bSpin_ = nullptr, *aSpin_ = nullptr;
    QLabel     *swatch_         = nullptr;
    QPushButton*pickColorBtn_   = nullptr;
    QPushButton*applyBtn_       = nullptr;
    QPushButton*resetCellBtn_   = nullptr;
    QPushButton*clearAllBtn_    = nullptr;
    int         selCol_ = -1, selRow_ = -1;
    bool        updatingEditor_ = false;

    QAction *saveAct_ = nullptr;

    // ---------- UI ----------
    void setupUi() {
        // vistas
        origScene_ = new QGraphicsScene(this);
        origView_  = new AsciiView(this);
        origView_->setScene(origScene_);
        origView_->setBackgroundBrush(QColor(40, 40, 44));

        asciiScene_ = new QGraphicsScene(this);
        asciiView_  = new AsciiView(this);
        asciiView_->setScene(asciiScene_);
        asciiView_->setBackgroundBrush(bgColor_);

        connect(asciiView_, &AsciiView::cellClicked,
                this, &MainWindow::onAsciiClicked);

        auto *split = new QSplitter(Qt::Horizontal, this);
        auto *wL = new QGroupBox(QStringLiteral("Original"));
        auto *lL = new QVBoxLayout(wL); lL->addWidget(origView_);
        auto *wR = new QGroupBox(QStringLiteral("ASCII  ·  clic en una celda para editarla"));
        auto *lR = new QVBoxLayout(wR); lR->addWidget(asciiView_);
        split->addWidget(wL);
        split->addWidget(wR);
        split->setStretchFactor(0, 1);
        split->setStretchFactor(1, 1);
        setCentralWidget(split);

        // dock
        auto *dock = new QDockWidget(QStringLiteral("Ajustes"), this);
        dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

        auto *panel = new QWidget;
        auto *vl = new QVBoxLayout(panel);

        // --- conversión ---
        auto *gbConv = new QGroupBox(QStringLiteral("Conversión"));
        auto *flConv = new QFormLayout(gbConv);
        rampEdit_ = new QLineEdit(maker_.ramp);
        rampEdit_->setToolTip(QStringLiteral(
            "De más oscuro (índice 0) a más claro. El primer carácter "
            "suele ser el espacio para que los negros no se pinten."));
        flConv->addRow(QStringLiteral("Rampa:"), rampEdit_);

        invertCheck_ = new QCheckBox(QStringLiteral("Invertir luminancia"));
        flConv->addRow(invertCheck_);

        autoCheck_ = new QCheckBox(QStringLiteral("Auto-niveles"));
        autoCheck_->setToolTip(QStringLiteral(
            "Estira el rango tonal útil (percentiles 0.5/99.5)."));
        flConv->addRow(autoCheck_);

        brightSlider_ = new QSlider(Qt::Horizontal);
        brightSlider_->setRange(20, 300);
        brightSlider_->setValue(100);
        brightLabel_ = new QLabel(QStringLiteral("1.00"));
        auto *bh = new QHBoxLayout;
        bh->addWidget(brightSlider_); bh->addWidget(brightLabel_);
        flConv->addRow(QStringLiteral("Brillo:"), bh);

        contrastSlider_ = new QSlider(Qt::Horizontal);
        contrastSlider_->setRange(20, 300);
        contrastSlider_->setValue(100);
        contrastLabel_ = new QLabel(QStringLiteral("1.00"));
        auto *chh = new QHBoxLayout;
        chh->addWidget(contrastSlider_); chh->addWidget(contrastLabel_);
        flConv->addRow(QStringLiteral("Contraste:"), chh);
        vl->addWidget(gbConv);

        // --- alfa ---
        auto *gbA = new QGroupBox(QStringLiteral("Alfa"));
        auto *flA = new QFormLayout(gbA);
        alphaCheck_ = new QCheckBox(QStringLiteral("Transparente → espacio"));
        flA->addRow(alphaCheck_);
        alphaSpin_ = new QSpinBox;
        alphaSpin_->setRange(0, 255);
        alphaSpin_->setValue(128);
        flA->addRow(QStringLiteral("Umbral:"), alphaSpin_);
        vl->addWidget(gbA);

        // --- escala / fuente / fondo ---
        auto *gbS = new QGroupBox(QStringLiteral("Escala y fuente"));
        auto *flS = new QFormLayout(gbS);
        scaleSpin_ = new QDoubleSpinBox;
        scaleSpin_->setRange(0.05, 16.0);
        scaleSpin_->setSingleStep(0.05);
        scaleSpin_->setDecimals(2);
        scaleSpin_->setValue(1.0);
        flS->addRow(QStringLiteral("Escala:"), scaleSpin_);
        fontSizeSpin_ = new QSpinBox;
        fontSizeSpin_->setRange(4, 96);
        fontSizeSpin_->setValue(8);
        fontSizeSpin_->setSuffix(QStringLiteral(" pt"));
        flS->addRow(QStringLiteral("Fuente:"), fontSizeSpin_);
        aspectoSpin_ = new QDoubleSpinBox;
        aspectoSpin_->setRange(1.0, 4.0);
        aspectoSpin_->setSingleStep(0.05);
        aspectoSpin_->setDecimals(2);
        // Por defecto, el valor real de la fuente: es lo único que deja
        // la imagen con la proporción de la original.
        const double medido = aspectoDeFuente();
        maker_.aspecto = medido;
        aspectoSpin_->setValue(medido);
        aspectoSpin_->setToolTip(QStringLiteral(
            "Cuánto más alto es un carácter que ancho.\n"
            "Un carácter no es un cuadrado, así que sin esta corrección\n"
            "el arte sale estirado verticalmente.\n\n"
            "1,00 = sin corregir · 2,00 = el valor clásico\n"
            "Esta fuente mide %1. Valores altos = más líneas, arte más\n"
            "aplastado.").arg(QString::number(medido, 'f', 2)));
        flS->addRow(QStringLiteral("Aspecto:"), aspectoSpin_);
        bgButton_ = new QPushButton(QStringLiteral("Fondo: negro"));
        flS->addRow(bgButton_);
        vl->addWidget(gbS);

        // --- compresión ---
        auto *gbC = new QGroupBox(QStringLiteral("Compresión"));
        gbC->setToolTip(QStringLiteral(
            "Afecta solo a la exportación: la vista previa siempre\n"
            "muestra el arte sin comprimir."));
        auto *flC = new QFormLayout(gbC);

        qualitySpin_ = new QSpinBox;
        qualitySpin_->setRange(1, 100);
        qualitySpin_->setValue(92);
        qualitySpin_->setSuffix(QStringLiteral(" %"));
        qualitySpin_->setToolTip(QStringLiteral(
            "Calidad con la que se codifica el WebP.\n\n"
            "En modo sin pérdida no marca el resultado (ahí no hay\n"
            "calidad que elegir), pero en la animación sí sirve: es el\n"
            "esfuerzo de compresión, y más alto = archivo más pequeño."));
        flC->addRow(QStringLiteral("Calidad:"), qualitySpin_);

        losslessCheck_ = new QCheckBox(QStringLiteral("Sin pérdida"));
        losslessCheck_->setToolTip(QStringLiteral(
            "Lossless: el WebP guarda los píxeles tal cual, sin\n"
            "difuminar los caracteres. Cuesta bastante más espacio\n"
            "— en la animación, del orden de 4×.\n\n"
            "Desmarcado: WebP con pérdida, mucho más pequeño pero con\n"
            "los bordes de los caracteres algo degradados.\n\n"
            "El PNG ya es siempre sin pérdida, así que esto no le afecta."));
        flC->addRow(losslessCheck_);
        vl->addWidget(gbC);

        // --- topes de tamaño ---
        auto *gbL = new QGroupBox(QStringLiteral("Topes de tamaño"));
        gbL->setToolTip(QStringLiteral(
            "Sin topes el arte es 1 carácter por píxel, exacto.\n"
            "Un tope reescala la imagen, así que el arte deja de ser 1:1\n"
            "con la original. Con «Mantener proporción» marcado solo se\n"
            "encoge; desmarcado se deforma hasta la rejilla pedida.\n"
            "Los topes de filas y columnas valen para imágenes y para video."));
        auto *flL = new QFormLayout(gbL);
        limColsSpin_ = new QSpinBox;
        limColsSpin_->setRange(0, 20000);
        limColsSpin_->setValue(0);
        limColsSpin_->setSpecialValueText(QStringLiteral("sin tope"));
        limColsSpin_->setToolTip(QStringLiteral(
            "Máximo de columnas. 0 = sin tope."));
        flL->addRow(QStringLiteral("Columnas:"), limColsSpin_);

        limRowsSpin_ = new QSpinBox;
        limRowsSpin_->setRange(0, 20000);
        limRowsSpin_->setValue(0);
        limRowsSpin_->setSpecialValueText(QStringLiteral("sin tope"));
        limRowsSpin_->setToolTip(QStringLiteral(
            "Máximo de filas de la rejilla 1:1. 0 = sin tope."));
        flL->addRow(QStringLiteral("Filas:"), limRowsSpin_);

        ratioCheck_ = new QCheckBox(QStringLiteral("Mantener proporción"));
        ratioCheck_->setChecked(true);
        ratioCheck_->setToolTip(QStringLiteral(
            "Marcado: los topes seLinkedan por el más restrictivo y la\n"
            "imagen no se deforma.\n\n"
            "Desmarcado: cada tope manda por su lado y el arte se estira o\n"
            "se aplasta hasta clavarse en la rejilla pedida. Útil para\n"
            "encajar el arte en un tamaño exacto.\n\n"
            "Ojo: un tope sin puesto no restringe su eje, así que\n"
            "desmarcado se deforma igual. Con solo «columnas» puestas,\n"
            "por ejemplo, el alto se queda como sea y la imagen se\n"
            "estira en vertical."));
        flL->addRow(ratioCheck_);

        limFramesSpin_ = new QSpinBox;
        limFramesSpin_->setRange(0, 100000);
        limFramesSpin_->setValue(0);
        limFramesSpin_->setSpecialValueText(QStringLiteral("sin tope"));
        limFramesSpin_->setToolTip(QStringLiteral(
            "Máximo de frames de la animación. 0 = sin tope.\n"
            "El video se reproduce en tiempo real, así que ceñir aquí\n"
            "es la forma de no esperar minutos."));
        flL->addRow(QStringLiteral("Frames:"), limFramesSpin_);
        vl->addWidget(gbL);

        // --- animación ---
        gbAnim_ = new QGroupBox(QStringLiteral("Animación"));
        auto *flA2 = new QFormLayout(gbAnim_);
        animLabel_ = new QLabel(QStringLiteral("(ninguna)"));
        flA2->addRow(animLabel_);

        playBtn_ = new QPushButton(QStringLiteral("Reproducir"));
        connect(playBtn_, &QPushButton::clicked, this, [this]{
            if (reproduciendo_) pausar(); else reproducir();
        });
        flA2->addRow(playBtn_);

        frameSlider_ = new QSlider(Qt::Horizontal);
        frameSlider_->setRange(0, 0);
        frameSlider_->setToolTip(QStringLiteral("Frame actual"));
        flA2->addRow(QStringLiteral("Frame:"), frameSlider_);

        loopCheck_ = new QCheckBox(QStringLiteral("Repetir en bucle"));
        flA2->addRow(loopCheck_);
        gbAnim_->setVisible(false);
        vl->addWidget(gbAnim_);

        // --- editor de celda ---
        editorBox_ = new QGroupBox(QStringLiteral("Celda seleccionada"));
        auto *flE = new QFormLayout(editorBox_);
        celdaInfoLabel_ = new QLabel(QStringLiteral("(ninguna)"));
        flE->addRow(celdaInfoLabel_);

        charEdit_ = new QLineEdit;
        charEdit_->setMaxLength(1);
        charEdit_->setPlaceholderText(QStringLiteral(" "));
        flE->addRow(QStringLiteral("Carácter:"), charEdit_);

        rSpin_ = new QSpinBox; rSpin_->setRange(0, 255);
        gSpin_ = new QSpinBox; gSpin_->setRange(0, 255);
        bSpin_ = new QSpinBox; bSpin_->setRange(0, 255);
        aSpin_ = new QSpinBox; aSpin_->setRange(0, 255); aSpin_->setValue(255);
        auto *colLay = new QGridLayout;
        colLay->addWidget(new QLabel(QStringLiteral("R:")), 0, 0);
        colLay->addWidget(rSpin_, 0, 1);
        colLay->addWidget(new QLabel(QStringLiteral("G:")), 1, 0);
        colLay->addWidget(gSpin_, 1, 1);
        colLay->addWidget(new QLabel(QStringLiteral("B:")), 2, 0);
        colLay->addWidget(bSpin_, 2, 1);
        colLay->addWidget(new QLabel(QStringLiteral("A:")), 3, 0);
        colLay->addWidget(aSpin_, 3, 1);
        flE->addRow(colLay);

        swatch_ = new QLabel;
        swatch_->setFixedHeight(28);
        swatch_->setFrameShape(QFrame::Box);
        pickColorBtn_ = new QPushButton(QStringLiteral("Elegir color…"));
        auto *swLay = new QHBoxLayout;
        swLay->addWidget(swatch_, 1);
        swLay->addWidget(pickColorBtn_);
        flE->addRow(swLay);

        applyBtn_    = new QPushButton(QStringLiteral("Aplicar a la celda"));
        resetCellBtn_= new QPushButton(QStringLiteral("Restaurar celda"));
        clearAllBtn_ = new QPushButton(QStringLiteral("Limpiar ediciones"));
        flE->addRow(applyBtn_);
        auto *rstLay = new QHBoxLayout;
        rstLay->addWidget(resetCellBtn_);
        rstLay->addWidget(clearAllBtn_);
        flE->addRow(rstLay);
        vl->addWidget(editorBox_);

        vl->addStretch(1);
        setEditorEnabled(false);

        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setWidget(panel);
        dock->setWidget(scroll);
        addDockWidget(Qt::RightDockWidgetArea, dock);

        statusLabel_ = new QLabel(QStringLiteral("Listo. Abre una imagen (Ctrl+O)."));
        statusBar()->addWidget(statusLabel_, 1);

        // conexiones de ajustes
        connect(rampEdit_, &QLineEdit::textChanged,
                this, [this](const QString &){ updateAscii(); });
        connect(invertCheck_, &QCheckBox::toggled,
                this, [this](bool){ updateAscii(); });
        connect(autoCheck_, &QCheckBox::toggled,
                this, [this](bool){ updateAscii(); });
        connect(alphaCheck_, &QCheckBox::toggled,
                this, [this](bool){ updateAscii(); });
        connect(alphaSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int){ updateAscii(); });
        connect(scaleSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this](double){ updateAscii(); });
        connect(brightSlider_, &QSlider::valueChanged, this, [this](int v){
            brightLabel_->setText(QString::number(v / 100.0, 'f', 2));
            updateAscii();
        });
        connect(contrastSlider_, &QSlider::valueChanged, this, [this](int v){
            contrastLabel_->setText(QString::number(v / 100.0, 'f', 2));
            updateAscii();
        });
        connect(fontSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int){ rebuildItem(false); });
        connect(aspectoSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, [this](double v){
                    if (std::abs(v - maker_.aspecto) < 0.001) return;
                    maker_.aspecto = v;
                    refrescarVista();
                    actualizarEstado();
                    rebuildItem(false);
                });

        // Los topes de filas y columnas obligan a reconvertir: para una
        // animación no vale solo con redibujar el frame actual.
        auto reconvertir = [this]{
            if (frames_.isEmpty()) return;
            const int lc = limColsSpin_->value(), lr = limRowsSpin_->value();
            const bool rp = ratioCheck_->isChecked();
            if (maker_.limiteCols == lc && maker_.limiteRows == lr &&
                maker_.conservarProporcion == rp) return;
            reconvertirTodo();
        };
        connect(limColsSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this, reconvertir](int){ reconvertir(); });
        connect(limRowsSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this, reconvertir](int){ reconvertir(); });
        connect(ratioCheck_, &QCheckBox::toggled,
                this, [this, reconvertir](bool){ reconvertir(); });

        // El tope de frames recorta la vista previa al vuelo, sin reabrir el
        // video: lo que se ve en pantalla y lo que se exporta son los
        // mismos frames. Al subir el tope por encima de los frames
        // capturados no hay nada que estirar, así que solo avisa.
        connect(limFramesSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this](int v){
                    aplicarTopeFrames();
                    const int total = int(frames_.size());
                    if (esAnimacion() && v > total) {
                        statusLabel_->setText(QStringLiteral(
                            "Tope de %1 frames, pero solo hay %2 capturados. "
                            "Para ver más, reabre el video con un tope mayor.")
                            .arg(v).arg(total));
                    } else if (esAnimacion()) {
                        statusLabel_->setText(QStringLiteral(
                            "Usando %1 de %2 frames.").arg(framesEfectivos())
                            .arg(total));
                    } else if (v > 0) {
                        statusLabel_->setText(QStringLiteral(
                            "Tope de frames: %1 (no hay animación abierta)")
                            .arg(v));
                    }
                });

        connect(frameSlider_, &QSlider::valueChanged, this, [this](int v){
            if (v == frameActual_) return;
            irAFrame(v);
        });

        connect(bgButton_, &QPushButton::clicked,
                this, &MainWindow::elegirFondo);

        // editor
        connect(applyBtn_,   &QPushButton::clicked, this, &MainWindow::aplicarEdicion);
        connect(resetCellBtn_, &QPushButton::clicked, this, &MainWindow::restaurarCelda);
        connect(clearAllBtn_,  &QPushButton::clicked, this, &MainWindow::limpiarEdiciones);
        connect(pickColorBtn_, &QPushButton::clicked, this, &MainWindow::elegirColorCelda);
        auto syncSwatch = [this]{
            QColor c(rSpin_->value(), gSpin_->value(),
                     bSpin_->value(), aSpin_->value());
            swatch_->setStyleSheet(QStringLiteral(
                "background: rgba(%1,%2,%3,%4); border:1px solid #888;")
                .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha()));
        };
        for (QSpinBox *s : {rSpin_, gSpin_, bSpin_, aSpin_})
            connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this, syncSwatch);
        syncSwatch();
    }

    void setupActions() {
        auto *openAct = new QAction(QStringLiteral("Abrir…"), this);
        openAct->setShortcut(QKeySequence::Open);
        connect(openAct, &QAction::triggered, this, &MainWindow::openImage);

        saveAct_ = new QAction(QStringLiteral("Exportar…"), this);
        saveAct_->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
        connect(saveAct_, &QAction::triggered, this, &MainWindow::exportar);
        saveAct_->setEnabled(false);

        auto *quitAct = new QAction(QStringLiteral("Salir"), this);
        quitAct->setShortcut(QKeySequence::Quit);
        connect(quitAct, &QAction::triggered, this, &QWidget::close);

        auto *mFile = menuBar()->addMenu(QStringLiteral("&Archivo"));
        mFile->addAction(openAct);
        mFile->addAction(saveAct_);
        mFile->addSeparator();
        mFile->addAction(quitAct);

        auto *tb = addToolBar(QStringLiteral("Principal"));
        tb->setMovable(false);
        tb->addAction(openAct);
        tb->addAction(saveAct_);
        tb->addSeparator();

        auto addZoom = [&](const QString &txt, double f) {
            auto *a = new QAction(txt, this);
            connect(a, &QAction::triggered, this, [this, f]{ asciiView_->scale(f, f); });
            tb->addAction(a);
        };
        addZoom(QStringLiteral("Encajar"), 1.0);
        tb->addSeparator();
        auto *fitAct = new QAction(QStringLiteral("Encajar"), this);
        connect(fitAct, &QAction::triggered, this,
                [this]{ asciiView_->fitAll(); });
        auto *ziAct = new QAction(QStringLiteral("+"), this);
        connect(ziAct, &QAction::triggered, this, [this]{ asciiView_->zoomIn(); });
        auto *zoAct = new QAction(QStringLiteral("−"), this);
        connect(zoAct, &QAction::triggered, this, [this]{ asciiView_->zoomOut(); });
        auto *z1Act = new QAction(QStringLiteral("1:1"), this);
        connect(z1Act, &QAction::triggered, this, [this]{ asciiView_->zoomReset(); });
        tb->addAction(fitAct);
        tb->addAction(zoAct);
        tb->addAction(ziAct);
        tb->addAction(z1Act);
    }

    // ---------- lógica ----------
    void openImage() {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Abrir imagen o video"), QString(),
            QStringLiteral("Imágenes y animación (*.png *.jpg *.jpeg *.bmp "
                           "*.gif *.webp *.tif *.tiff *.avif *.mp4 *.m4v "
                           "*.mov *.webm *.mkv *.avi *.wmv *.m4v *.ogv);;"
                           "Imágenes (*.png *.jpg *.jpeg *.bmp *.tif *.tiff "
                           "*.webp);;GIF (*.gif);;Video (*.mp4 *.m4v *.mov "
                           "*.webm *.mkv *.avi *.wmv *.ogv);;Todos (*)"));
        if (path.isEmpty()) return;
        abrirRuta(path);
    }

    void abrirRuta(const QString &path) {
        const QString ext = QFileInfo(path).suffix().toLower();
        if (ext == "gif")               return abrirGif(path);
        if (esExtensionDeVideo(ext))    return abrirVideo(path);

        QImageReader reader(path);
        reader.setAutoTransform(true);
        QImage img = reader.read();
        if (img.isNull()) {
            QMessageBox::warning(this, QStringLiteral("Error"),
                QStringLiteral("No se pudo abrir:\n%1")
                    .arg(reader.errorString()));
            return;
        }
        cargarImagenFija(img, path);
    }

    static bool esExtensionDeVideo(const QString &ext) {
        static const QStringList v = {
            "mp4", "m4v", "mov", "webm", "mkv", "avi", "wmv", "ogv",
            "mpg", "mpeg", "3gp", "flv", "ts", "m2ts"};
        return v.contains(ext);
    }

    // ---------- imagen fija ----------
    void cargarImagenFija(const QImage &img, const QString &path) {
        pausar();
        frames_.clear();
        originalImage_ = img;
        currentPath_   = path;

        leerAjustes();
        maker_.setSource(img);

        Frame f;
        f.src  = img;
        f.base = AsciiGrid{ maker_.cols, maker_.rows, maker_.cells };
        frames_.append(f);
        frameActual_ = 0;
        // Para una imagen fija conservamos las ediciones en maker_.cells,
        // así que el frame solo las lleva si ya las tenía.
        for (int i = 0; i < frames_[0].base.cells.size(); ++i)
            if (frames_[0].base.cells[i].manual)
                frames_[0].manuales.append({i, frames_[0].base.cells[i]});

        mostrarOriginal(img);
        terminarCarga(path);
    }

    void mostrarOriginal(const QImage &img) {
        origScene_->clear();
        origScene_->addPixmap(QPixmap::fromImage(img));
        origScene_->setSceneRect(QRectF(QPointF(0, 0), QSizeF(img.size())));
        origView_->fitAll();
    }

    void terminarCarga(const QString &path) {
        selCol_ = selRow_ = -1;
        frameActual_ = 0;
        gbAnim_->setVisible(esAnimacion());
        actualizarPanelAnimacion();
        rebuildItem(true);
        setEditorEnabled(false);
        actualizarEstado();
        saveAct_->setEnabled(true);
        setWindowTitle(QStringLiteral("ASCII Maker — %1")
                           .arg(QFileInfo(path).fileName()));
    }

    // El panel refleja siempre lo que se ve: si hay recorte activo lo dice,
    // porque entonces el deslizador y el frame actual se mueven distinto
    // al número de frames capturados.
    void actualizarPanelAnimacion() {
        const bool vis = esAnimacion();
        gbAnim_->setVisible(vis);
        if (!vis) return;

        const int total = int(frames_.size());
        const int n     = framesEfectivos();
        frameSlider_->setRange(0, qMax(0, n - 1));
        frameSlider_->setValue(frameActual_);

        QString txt = recorteActivo()
            ? QStringLiteral("Usando %1 de %2 frames")
            : QStringLiteral("%1 frames");
        txt = txt.arg(n).arg(total);   // los dos huecos, siempre
        txt += QStringLiteral(" · %1 ms/frame")
                   .arg(QString::number(duracionDeFrames(n) /
                                        double(qMax(1, n)), 'f', 0));
        if (recorteActivo())
            txt += QStringLiteral("\nRecortado por el tope de frames");
        txt += QStringLiteral("\n%1 celdas/frame · %2 MB en memoria")
                   .arg(frames_.first().base.cols * frames_.first().base.rows)
                   .arg(QString::number(memoriaFrames() / (1024.0 * 1024.0),
                                        'f', 1));
        animLabel_->setText(txt);
    }

    int duracionTotal() const { return duracionDeFrames(int(frames_.size())); }

    int duracionDeFrames(int n) const {
        int ms = 0;
        for (int i = 0; i < qMin(n, int(frames_.size())); ++i)
            ms += frames_[i].delayMs;
        return ms;
    }

    // Only las rejillas 1:1, que es lo grande de verdad. Las imágenes
    // originales se descartan al terminar la captura para no duplicar.
    qint64 memoriaFrames() const {
        return qint64(frames_.size()) *
               (frames_.isEmpty() ? 0
                                  : qint64(frames_.first().base.cols) *
                                    frames_.first().base.rows * sizeof(AsciiCell));
    }

    // ---------- GIF ----------
    void abrirGif(const QString &path) {
        pausar();
        QMovie movie(path);
        if (!movie.isValid()) {
            QMessageBox::warning(this, QStringLiteral("Error"),
                QStringLiteral("No se pudo leer el GIF:\n%1")
                    .arg(movie.lastErrorString()));
            return;
        }
        const int total = qMax(1, movie.frameCount());
        const int tope = limFramesSpin_->value();

        statusBar()->showMessage(QStringLiteral("Leyendo GIF…"));
        QApplication::setOverrideCursor(Qt::WaitCursor);

        frames_.clear();
        leerAjustes();
        movie.jumpToFrame(0);
        for (int i = 0; i < total; ++i) {
            if (tope > 0 && i >= tope) break;
            if (!movie.jumpToFrame(i)) break;
            const QImage img = movie.currentImage();
            if (img.isNull()) continue;
            Frame f;
            f.src     = img;
            f.delayMs = qMax(10, movie.nextFrameDelay());
            convertirFrame(f);
            frames_.append(f);
            if ((i & 31) == 0) {
                QApplication::processEvents();
                statusBar()->showMessage(
                    QStringLiteral("Leyendo GIF… %1/%2").arg(i + 1).arg(total));
            }
        }
        QApplication::restoreOverrideCursor();
        statusBar()->clearMessage();

        if (frames_.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Error"),
                QStringLiteral("El GIF no tiene frames legibles."));
            return;
        }
        originalImage_ = frames_.first().src;
        currentPath_   = path;
        mostrarOriginal(frames_.first().src);
        frameActual_ = 0;
        terminarCarga(path);
    }

    // ---------- video ----------
    // Se reproduce en tiempo real y se va capturando cada frame, congelando
    // el player mientras se convierte cada uno para no perder ninguno.
    //
    // Ojo con el final: con un QVideoSink sin superficie de pintado,
    // QMediaPlayer no siempre llega a emitir EndOfMedia — se queda parado
    // en el último frame. Por eso no nos fiamos solo de esa señal: hay un
    // watchdog que corta cuando el player deja de entregar frames, y
    // además vigilamos la posición contra la duración.
    void abrirVideo(const QString &path) {
        pausar();
        frames_.clear();
        leerAjustes();

        if (player_) { player_->stop(); player_->deleteLater(); player_ = nullptr; }
        if (sink_)   { sink_->deleteLater(); sink_ = nullptr; }

        player_ = new QMediaPlayer(this);
        sink_   = new QVideoSink(this);
        player_->setVideoSink(sink_);
        player_->setSource(QUrl::fromLocalFile(path));
        player_->setPlaybackRate(1.0);

        const int tope = limFramesSpin_->value();

        QEventLoop loop;
        bool error = false, terminado = false;
        capturando_ = true;

        // Corta cuando el player lleva un rato sin soltar frames.
        QTimer watchdog;
        watchdog.setSingleShot(true);
        watchdog.setInterval(1500);
        QObject::connect(&watchdog, &QTimer::timeout, [&]{
            if (!frames_.isEmpty()) { terminado = true; loop.quit(); }
        });

        QObject::connect(player_, &QMediaPlayer::errorOccurred,
                         [&](QMediaPlayer::Error, const QString &msg) {
            error = true;
            statusBar()->showMessage(
                QStringLiteral("Error de reproducción: %1").arg(msg));
            loop.quit();
        });
        QObject::connect(player_, &QMediaPlayer::mediaStatusChanged,
                         [&](QMediaPlayer::MediaStatus s) {
            if (s == QMediaPlayer::EndOfMedia) { terminado = true; loop.quit(); }
            else if (s == QMediaPlayer::InvalidMedia) { error = true; loop.quit(); }
        });
        QObject::connect(player_, &QMediaPlayer::positionChanged,
                         [&](qint64 pos) {
            const qint64 dur = player_->duration();
            if (dur > 0 && pos >= dur - 40) { terminado = true; loop.quit(); }
        });
        QObject::connect(sink_, &QVideoSink::videoFrameChanged,
                         [&](const QVideoFrame &vf) {
            if (!capturando_ || procesando_) return;
            watchdog.start();
            const QImage img = vf.toImage();
            if (img.isNull()) return;
            // Congelamos mientras convertimos, o el player se nos adelanta.
            procesando_ = true;
            player_->pause();

            if (tope > 0 && frames_.size() >= tope) {
                capturando_ = false;
                player_->stop();
                loop.quit();
                procesando_ = false;
                return;
            }
            Frame f;
            f.src     = img;
            f.delayMs = 1000 / 15;
            convertirFrame(f);
            frames_.append(f);

            statusBar()->showMessage(QStringLiteral(
                "Capturando video… %1 frames  (%2×%3 celdas)")
                .arg(frames_.size())
                .arg(frames_.last().base.cols).arg(frames_.last().base.rows));
            // Repintar la barra de progreso. El guardia `procesando_` evita
            // que otro frame entre por aquí y duplique el trabajo.
            QApplication::processEvents();
            player_->play();
            procesando_ = false;
        });

        statusBar()->showMessage(QStringLiteral("Reproduciendo video…"));
        QApplication::setOverrideCursor(Qt::WaitCursor);
        player_->play();

        QTimer::singleShot(60 * 60 * 1000, &loop, &QEventLoop::quit);  // 1 h tope
        loop.exec();

        capturando_ = false;
        procesando_ = false;
        player_->stop();
        QApplication::restoreOverrideCursor();
        statusBar()->clearMessage();

        if (error || frames_.isEmpty()) {
            QMessageBox::warning(this, QStringLiteral("Error"),
                QStringLiteral("No se pudieron extraer frames del video.\n"
                               "Prueba con otro formato (mp4, webm, mov)."));
            return;
        }
        (void)terminado;

        originalImage_ = frames_.first().src;
        currentPath_   = path;
        mostrarOriginal(frames_.first().src);
        frameActual_ = 0;
        terminarCarga(path);

        if (tope > 0 && frames_.size() >= tope)
            statusBar()->showMessage(QStringLiteral(
                "Capturados %1 frames (tope alcanzado).").arg(frames_.size()),
                                     6000);
        else if (terminado && frames_.size() == 1)
            statusBar()->showMessage(QStringLiteral(
                "Solo se capturó 1 frame: el video se ha parado antes de "
                "entregar el resto."), 8000);
    }

    // Convierte una imagen a rejilla 1:1 con los ajustes actuales.
    void convertirFrame(Frame &f) {
        maker_.setFrameSource(f.src);
        f.base.cols  = maker_.cols;
        f.base.rows  = maker_.rows;
        f.base.cells = maker_.cells;
        // re-aplica ediciones manuales de este frame
        for (const auto &par : f.manuales) {
            if (par.first >= 0 && par.first < f.base.cells.size())
                f.base.cells[par.first] = par.second;
        }
    }

    // Vuelca los widgets a maker_ (topes incluidos).
    void leerAjustes() {
        maker_.ramp           = rampEdit_->text();
        if (maker_.ramp.isEmpty()) maker_.ramp = QStringLiteral(" ");
        maker_.invert         = invertCheck_->isChecked();
        maker_.autoLevels     = autoCheck_->isChecked();
        maker_.brightness     = brightSlider_->value() / 100.0;
        maker_.contrast       = contrastSlider_->value() / 100.0;
        maker_.alphaAsSpace   = alphaCheck_->isChecked();
        maker_.alphaThreshold = alphaSpin_->value();
        maker_.scale          = scaleSpin_->value();
        maker_.limiteCols     = limColsSpin_->value();
        maker_.limiteRows     = limRowsSpin_->value();
        maker_.conservarProporcion = ratioCheck_->isChecked();
    }

    // Reconvierte todos los frames con los ajustes actuales.
    void reconvertirTodo() {
        if (frames_.isEmpty()) return;
        leerAjustes();
        for (int i = 0; i < frames_.size(); ++i) {
            const QImage antes = frames_[i].src;
            convertirFrame(frames_[i]);
            frames_[i].src = antes;
        }
        if (frameActual_ >= frames_.size()) frameActual_ = 0;
        if (selCol_ >= frames_[frameActual_].base.cols ||
            selRow_ >= frames_[frameActual_].base.rows)
            selCol_ = selRow_ = -1;
        refrescarVista();
        actualizarEstado();
        actualizarEditorDesdeCelda();
    }

    // Cuántos frames se usan de verdad. El tope recorta la vista previa al
    // vuelo: no hace falta reabrir el video para ver el efecto.
    int framesEfectivos() const {
        const int total = int(frames_.size());
        const int t = limFramesSpin_ ? limFramesSpin_->value() : 0;
        return t > 0 ? qMin(t, total) : total;
    }

    bool recorteActivo() const {
        return frames_.size() > 1 && framesEfectivos() < frames_.size();
    }

    // Reaplica el recorte tras cambiar el tope: acota el deslizador, deja
    // el frame actual dentro del rango y repinta.
    void aplicarTopeFrames() {
        if (frames_.isEmpty()) return;
        const int n = framesEfectivos();
        const bool bloq = frameSlider_->blockSignals(true);
        frameSlider_->setRange(0, qMax(0, n - 1));
        frameSlider_->blockSignals(bloq);
        if (frameActual_ > n - 1) frameActual_ = qMax(0, n - 1);
        if (frameSlider_->value() != frameActual_) {
            const bool b2 = frameSlider_->blockSignals(true);
            frameSlider_->setValue(frameActual_);
            frameSlider_->blockSignals(b2);
        }
        refrescarVista();
        if (asciiItem_) asciiItem_->update();
        actualizarPanelAnimacion();
        actualizarEstado();
    }

    AsciiGrid &baseActual() {
        return frames_[qBound(0, frameActual_, frames_.size() - 1)].base;
    }

    void updateAscii() {
        if (frames_.isEmpty()) return;
        reconvertirTodo();
        rebuildItem(false);
    }

    void actualizarEstado() {
        if (frames_.isEmpty() || originalImage_.isNull()) return;
        const AsciiGrid &b = baseActual();
        QString txt = QStringLiteral(
            "%1 × %2 px  →  %3 × %4 celdas  ·  dibujo %3 × %5")
            .arg(originalImage_.width()).arg(originalImage_.height())
            .arg(b.cols).arg(b.rows).arg(vista_.rows);
        if (esAnimacion()) {
            // El denominador es lo que se ve, no lo capturado: si hay
            // recorte, se ve aquí sin tener que mirar el panel.
            txt = QStringLiteral("frame %1/%2  ·  %3")
                      .arg(frameActual_ + 1).arg(framesEfectivos()).arg(txt);
            if (recorteActivo())
                txt = QStringLiteral("%1  (de %2 capturados)")
                          .arg(txt).arg(frames_.size());
        }
        statusLabel_->setText(txt);
    }

    // Copia la rejilla de dibujo a vista_ y la pasa al item.
    void refrescarVista() {
        if (frames_.isEmpty()) { vista_ = AsciiGrid(); return; }
        vista_ = maker_.construirVista(baseActual());
        if (asciiItem_) {
            asciiItem_->setGrid(&vista_);
            auto v = maker_.sourceToView(baseActual(), selCol_, selRow_);
            asciiItem_->setSelection(v.first, v.second);
        }
    }

    // ---------- reproducción ----------
    void reproducir() {
        if (!esAnimacion()) return;
        if (!reloj_) {
            reloj_ = new QTimer(this);
            connect(reloj_, &QTimer::timeout, this, [this]{
                const int n = framesEfectivos();
                if (frameActual_ + 1 < n) {
                    irAFrame(frameActual_ + 1);
                } else if (loopCheck_->isChecked()) {
                    irAFrame(0);
                } else {
                    pausar();
                }
            });
        }
        frameActual_ = qBound(0, frameActual_, framesEfectivos() - 1);
        if (frameActual_ >= framesEfectivos() - 1) irAFrame(0);
        reproduciendo_ = true;
        playBtn_->setText(QStringLiteral("Pausar"));
        reloj_->start(qMax(10, frames_[frameActual_].delayMs));
    }

    void pausar() {
        reproduciendo_ = false;
        if (reloj_) reloj_->stop();
        if (playBtn_) playBtn_->setText(QStringLiteral("Reproducir"));
        if (player_) player_->pause();
    }

    void irAFrame(int n) {
        if (frames_.isEmpty()) return;
        n = qBound(0, n, framesEfectivos() - 1);
        frameActual_ = n;
        if (reproduciendo_ && reloj_)
            reloj_->start(qMax(10, frames_[n].delayMs));
        if (frameSlider_ && frameSlider_->value() != n) {
            const bool bloq = frameSlider_->blockSignals(true);
            frameSlider_->setValue(n);
            frameSlider_->blockSignals(bloq);
        }
        // la selección puede haberse quedado fuera del nuevo frame
        const AsciiGrid &b = frames_[n].base;
        if (selCol_ >= b.cols || selRow_ >= b.rows) selCol_ = selRow_ = -1;
        refrescarVista();
        if (asciiItem_) asciiItem_->update();
        actualizarEstado();
        actualizarEditorDesdeCelda();
    }

    void rebuildItem(bool hacerFit) {
        if (asciiItem_) {
            asciiScene_->removeItem(asciiItem_);
            delete asciiItem_;
            asciiItem_ = nullptr;
        }
        refrescarVista();
        if (vista_.isNull()) return;

        QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        f.setStyleHint(QFont::Monospace);
        f.setFamily(QStringLiteral("monospace"));
        f.setPointSize(fontSizeSpin_->value());

        asciiItem_ = new AsciiItem(&vista_, f);
        const auto sel = maker_.sourceToView(baseActual(), selCol_, selRow_);
        asciiItem_->setSelection(sel.first, sel.second);
        asciiScene_->addItem(asciiItem_);
        asciiScene_->setSceneRect(asciiItem_->boundingRect());

        if (hacerFit) asciiView_->fitAll();
    }

    void elegirFondo() {
        const QColor c = QColorDialog::getColor(
            bgColor_, this, QStringLiteral("Color de fondo"));
        if (!c.isValid()) return;
        bgColor_ = c;
        bgButton_->setText(QStringLiteral("Fondo: %1").arg(c.name()));
        asciiView_->setBackgroundBrush(c);
        if (asciiItem_) asciiItem_->update();
    }

    // ---------- selección / edición ----------
    void onAsciiClicked(const QPointF &scenePos) {
        if (!asciiItem_) return;
        const auto p = asciiItem_->cellAt(scenePos);
        if (p.first < 0) return;
        // el clic cae en la rejilla de dibujo: lo llevamos a la 1:1, que
        // es donde viven los píxeles y las ediciones
        const auto s = maker_.viewToSource(baseActual(), p.first, p.second);
        if (s.first < 0) return;
        selCol_ = s.first;
        selRow_ = s.second;
        asciiItem_->setSelection(p.first, p.second);
        actualizarEditorDesdeCelda();
    }

    void setEditorEnabled(bool on) {
        editorBox_->setEnabled(on);
    }

    void actualizarEditorDesdeCelda() {
        if (frames_.isEmpty() || selCol_ < 0 || selRow_ < 0) {
            celdaInfoLabel_->setText(QStringLiteral("(ninguna)"));
            setEditorEnabled(false);
            return;
        }
        const AsciiGrid &b = baseActual();
        if (selCol_ >= b.cols || selRow_ >= b.rows) {
            celdaInfoLabel_->setText(QStringLiteral("(ninguna)"));
            setEditorEnabled(false);
            return;
        }
        setEditorEnabled(true);
        const AsciiCell &c = b.cells[selRow_ * b.cols + selCol_];
        updatingEditor_ = true;
        charEdit_->setText(QString(QChar::fromLatin1(c.ch)));
        rSpin_->setValue(c.r);
        gSpin_->setValue(c.g);
        bSpin_->setValue(c.b);
        aSpin_->setValue(c.a);
        updatingEditor_ = false;
        QString extra;
        if (esAnimacion()) extra = QStringLiteral("  ·  frame %1").arg(
            frameActual_ + 1);
        celdaInfoLabel_->setText(QStringLiteral("x=%1  y=%2  px%3%4")
            .arg(selCol_).arg(selRow_)
            .arg(b.rows == vista_.rows ? QString()
                                       : QStringLiteral("  (celda %1)")
                                             .arg(selRow_ * vista_.rows
                                                  / qMax(1, b.rows)))
            .arg(extra +
                 (c.manual ? QStringLiteral("  · editada") : QString())));
    }

    void aplicarEdicion() {
        if (frames_.isEmpty() || selCol_ < 0 || selRow_ < 0) return;
        AsciiGrid &b = baseActual();
        if (selCol_ >= b.cols || selRow_ >= b.rows) return;
        const int idx = selRow_ * b.cols + selCol_;
        AsciiCell &c = b.cells[idx];

        QString t = charEdit_->text();
        if (t.isEmpty()) t = QStringLiteral(" ");
        c.ch = t.at(0).toLatin1();
        c.r = quint8(rSpin_->value());
        c.g = quint8(gSpin_->value());
        c.b = quint8(bSpin_->value());
        c.a = quint8(aSpin_->value());
        c.manual = true;

        // `manuales` es la única fuente de verdad: convertirFrame() la
        // reaplica cada vez que se reconvierte, así que la edición
        // sobrevive a cambios de brillo, rampa, topes, etc.
        Frame &f = frames_[frameActual_];
        bool guardado = false;
        for (auto &par : f.manuales)
            if (par.first == idx) { par.second = c; guardado = true; break; }
        if (!guardado) f.manuales.append({idx, c});
        std::sort(f.manuales.begin(), f.manuales.end(),
                  [](const auto &a, const auto &b){ return a.first < b.first; });

        refrescarVista();
        actualizarEditorDesdeCelda();
    }

    void restaurarCelda() {
        if (frames_.isEmpty() || selCol_ < 0 || selRow_ < 0) return;
        AsciiGrid &b = baseActual();
        if (selCol_ >= b.cols || selRow_ >= b.rows) return;
        const int idx = selRow_ * b.cols + selCol_;

        // quitamos la edición manual y reconvertimos: la celda vuelve a
        // derivarse del píxel, y las demás manuales se reaplican solas.
        Frame &f = frames_[frameActual_];
        for (int i = 0; i < f.manuales.size(); ++i)
            if (f.manuales[i].first == idx) f.manuales.removeAt(i);
        convertirFrame(f);

        refrescarVista();
        actualizarEditorDesdeCelda();
    }

    void limpiarEdiciones() {
        if (frames_.isEmpty()) return;
        for (Frame &f : frames_) {
            f.manuales.clear();
            convertirFrame(f);
        }
        if (asciiItem_) {
            selCol_ = selRow_ = -1;
            asciiItem_->setSelection(-1, -1);
            asciiItem_->update();
        }
        refrescarVista();
        setEditorEnabled(false);
        celdaInfoLabel_->setText(QStringLiteral("(ninguna)"));
        actualizarEstado();
    }

    void elegirColorCelda() {
        QColor inicial(rSpin_->value(), gSpin_->value(),
                       bSpin_->value(), aSpin_->value());
        QColor c = QColorDialog::getColor(
            inicial, this, QStringLiteral("Color de la celda"),
            QColorDialog::ShowAlphaChannel);
        if (!c.isValid()) return;
        rSpin_->setValue(c.red());
        gSpin_->setValue(c.green());
        bSpin_->setValue(c.blue());
        aSpin_->setValue(c.alpha());
    }

    // ---------- exportar ----------
    QString baseSinExtension() const {
        if (currentPath_.isEmpty()) return QStringLiteral("ascii");
        return QFileInfo(currentPath_).completeBaseName();
    }
    QString carpetaImagen() const {
        if (currentPath_.isEmpty()) return QDir::homePath();
        return QFileInfo(currentPath_).absolutePath();
    }

    void exportar() {
        if (maker_.cols <= 0) return;

        // Con animación cargada solo tiene sentido el WebP animado (y el frame
        // actual como imagen fija, por si quieres solo un fotograma).
        const QString filtro = esAnimacion()
            ? QStringLiteral("WebP animado (*.webp);;PNG del frame actual "
                             "(*.png);;Todos (*)")
            : QStringLiteral("WebP (*.webp);;PNG (*.png);;Texto (*.txt);;"
                             "HTML (*.html)");

        const bool soporteWebp =
            QImageWriter::supportedImageFormats()
                .contains(QByteArrayLiteral("webp"));
        QString extDef = esAnimacion() ? QStringLiteral("webp")
                                       : (soporteWebp ? QStringLiteral("webp")
                                                      : QStringLiteral("png"));
        const QString sugerido = carpetaImagen() + "/"
                                 + baseSinExtension() + "." + extDef;

        QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Exportar ASCII"), sugerido, filtro);
        if (path.isEmpty()) return;

        QString ext = QFileInfo(path).suffix().toLower();
        if (ext.isEmpty()) {
            ext = extDef;
            path += "." + ext;
        }

        if (esAnimacion()) {
            if (ext == QLatin1String("webp"))      exportarAnimado(path);
            else if (ext == QLatin1String("png"))   exportarImagen(path, ext);
            else QMessageBox::warning(this, QStringLiteral("Formato"),
                QStringLiteral("Con una animación cargada solo se exporta "
                               ".webp (animado) o .png (el frame actual)."));
            return;
        }

        if (ext == QLatin1String("txt")) {
            escribirTexto(path, maker_.renderToText(baseActual()));
        } else if (ext == QLatin1String("html") || ext == QLatin1String("htm")) {
            escribirTexto(path,
                          maker_.renderToHtml(baseActual(), bgColor_, fontPx()));
        } else if (ext == QLatin1String("webp") || ext == QLatin1String("png")) {
            exportarImagen(path, ext);
        } else {
            QMessageBox::warning(this, QStringLiteral("Formato no soportado"),
                QStringLiteral("Extensión «.%1» no reconocida. Usa .webp, "
                               ".png, .txt o .html.").arg(ext));
        }
    }

    // ---------- WebP animado ----------
    // Qt 6.11 no tiene API de escritura multiframe (QImageWriter solo
    // escribe una imagen, qimagewriter.h:67), así que renderizamos cada
    // frame a PNG y dejamos que ffmpeg ensamble el WebP animado.
    void exportarAnimado(const QString &path) {
#ifdef ASCII_MAKER_FFMPEG
        const QString ff = QStringLiteral(ASCII_MAKER_FFMPEG);
        if (!QFileInfo(ff).isExecutable()) {
            QMessageBox::warning(this, QStringLiteral("ffmpeg no disponible"),
                QStringLiteral("No se encontró ffmpeg, que es lo único que "
                               "sabe escribir WebP animado."));
            return;
        }
        const int n = framesEfectivos();
        if (n > 1) {
            const qint64 mb = memoriaFrames() / (1024 * 1024);
            if (mb > 4000) {
                const auto r = QMessageBox::question(
                    this, QStringLiteral("Memoria alta"),
                    QStringLiteral("La animación ocupa unos %1 MB en memoria.\n"
                                   "El render puede dejar la máquina sin "
                                   "respuesta.\n\n¿Seguir?").arg(mb),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                if (r != QMessageBox::Yes) return;
            }
            if (recorteActivo()) {
                statusBar()->showMessage(QStringLiteral(
                    "Se exportan %1 de %2 frames (tope de frames).")
                    .arg(n).arg(frames_.size()));
            }
        }

        const QString dirSugerida = carpetaImagen();
        const QString tmp = QStringLiteral("%1/ascii-maker-%2")
                                .arg(dirSugerida.isEmpty() ? QDir::tempPath()
                                                           : dirSugerida)
                                .arg(QCoreApplication::applicationPid());
        QDir tmpDir(tmp);
        if (!tmpDir.mkpath(QStringLiteral("."))) {
            QMessageBox::warning(this, QStringLiteral("Error"),
                QStringLiteral("No se pudo crear el temporal:\n%1").arg(tmp));
            return;
        }
        const QFont f = fuenteMonoespaciada();
        QApplication::setOverrideCursor(Qt::WaitCursor);

        bool fallo = false;
        for (int i = 0; i < n; ++i) {
            bool tooBig = false;
            const QImage img = maker_.renderToImage(
                frames_[i].base, f, bgColor_, &tooBig);
            if (img.isNull()) {
                fallo = true;
                statusBar()->showMessage(
                    QStringLiteral("Frame %1: imagen demasiado grande").arg(i + 1));
                break;
            }
            const QString png = QStringLiteral("%1/f%2.png")
                                    .arg(tmp)
                                    .arg(i, 6, 10, QChar('0'));
            QImageWriter w(png, "png");
            if (!w.write(img)) { fallo = true; break; }
            if ((i & 7) == 0) {
                statusBar()->showMessage(QStringLiteral("Renderizando %1/%2…")
                                             .arg(i + 1).arg(n));
                QApplication::processEvents();
            }
        }
        QApplication::restoreOverrideCursor();

        if (fallo) {
            tmpDir.removeRecursively();
            QMessageBox::warning(this, QStringLiteral("Error"),
                QStringLiteral("No se pudo renderizar algún frame.\n"
                               "Baja el tamaño de fuente o pon topes de "
                               "columnas y filas."));
            return;
        }

        // ffmpeg no admite delays por frame: usamos la media de los frames que
        // entran en la exportación. Para duraciones irregulares hay que
        // promediar, así que se pierde un poco el ritmo original.
        int sumaMs = 0;
        for (int i = 0; i < n; ++i) sumaMs += frames_[i].delayMs;
        const double fps = n * 1000.0 / qMax(1, sumaMs);
        const int fpsInt = qBound(1, int(fps + 0.5), 60);

        statusBar()->showMessage(QStringLiteral("Ensamblando WebP animado…"));
        QApplication::setOverrideCursor(Qt::WaitCursor);

        // OJO: no pasar -preset. En libwebp de ffmpeg, un preset reescribe
        // la configuración y deja -lossless 1 a 0 en silencio: el archivo
        // sale con pérdida aunque se pida sin pérdida (comprobado: los
        // tamaños con preset coinciden exactamente con los del modo
        // con pérdida). Por eso aquí solo se tocan -lossless y -q:v.
        //
        // En modo sin pérdida, -q:v no es calidad sino esfuerzo de
        // compresión: más alto = archivo más pequeño.
        const bool sinPerdida = losslessCheck_ && losslessCheck_->isChecked();
        const int calidad = qualitySpin_ ? qualitySpin_->value() : 80;

        QStringList args = {
            QStringLiteral("-y"), QStringLiteral("-hide_banner"),
            QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-framerate"), QString::number(fpsInt),
            QStringLiteral("-i"), QStringLiteral("%1/f%2.png")
                .arg(tmp).arg(QStringLiteral("%06d")),
            QStringLiteral("-vcodec"), QStringLiteral("libwebp_anim"),
            QStringLiteral("-loop"), QStringLiteral("0"),
            QStringLiteral("-lossless"), sinPerdida ? "1" : "0",
            QStringLiteral("-q:v"), QString::number(calidad),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuva420p"),
            path};

        QProcess proc;
        proc.start(ff, args);
        if (!proc.waitForFinished(10 * 60 * 1000)) {
            proc.kill();
            QApplication::restoreOverrideCursor();
            tmpDir.removeRecursively();
            QMessageBox::warning(this, QStringLiteral("Error"),
                QStringLiteral("ffmpeg tardó demasiado y se ha cortado."));
            return;
        }
        QApplication::restoreOverrideCursor();
        statusBar()->clearMessage();

        tmpDir.removeRecursively();

        if (proc.exitStatus() != QProcess::NormalExit ||
            proc.exitCode() != 0 || !QFileInfo::exists(path)) {
            const QString err = QString::fromLocal8Bit(proc.readAllStandardError())
                                    .trimmed();
            QMessageBox::warning(this, QStringLiteral("Error de ffmpeg"),
                QStringLiteral("No se pudo ensamblar el WebP animado:\n%1")
                    .arg(err.isEmpty() ? QStringLiteral("código %1")
                                             .arg(proc.exitCode())
                                       : err));
            return;
        }

        const qint64 kb = QFileInfo(path).size() / 1024;
        statusLabel_->setText(QStringLiteral(
            "Exportado: %1  (%2 frames, %3 fps, %4, %5 KB)")
            .arg(path).arg(n).arg(fpsInt)
            .arg(sinPerdida ? QStringLiteral("sin pérdida")
                            : QStringLiteral("con pérdida"))
            .arg(kb));
#else
        QMessageBox::warning(this, QStringLiteral("ffmpeg no disponible"),
            QStringLiteral("Este binario se compiló sin ffmpeg, así que no "
                           "puede escribir WebP animado.\n\n"
                           "En Debian/Ubuntu:\n"
                           "  sudo apt install ffmpeg\n"
                           "y vuelve a compilar."));
#endif
    }

    // La fuente que se usa en el preview y en las exportaciones.
    QFont fuenteMonoespaciada() const {
        QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        f.setStyleHint(QFont::Monospace);
        f.setFamily(QStringLiteral("monospace"));
        f.setPointSize(fontSizeSpin_->value());
        return f;
    }

    // Proporción real alto/ancho de una celda de esa fuente.
    double aspectoDeFuente() const {
        const QFontMetricsF fm(fuenteMonoespaciada());
        const double cw = fm.horizontalAdvance(QLatin1Char('M'));
        return cw > 0 ? fm.height() / cw : 2.0;
    }

    // Tamaño de fuente en píxeles, para el HTML (ahí no hay QPainter).
    double fontPx() const {
        const QFont f = fuenteMonoespaciada();
        return f.pixelSize() > 0 ? double(f.pixelSize())
                                 : f.pointSizeF() * 96.0 / 72.0;
    }

    void escribirTexto(const QString &path, const QString &txt) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, QStringLiteral("Error"),
                QStringLiteral("No se pudo escribir:\n%1").arg(f.errorString()));
            return;
        }
        QTextStream ts(&f);
        ts.setEncoding(QStringConverter::Utf8);
        ts << txt;
        ts.flush();
        statusLabel_->setText(QStringLiteral("Guardado: %1").arg(path));
    }

    void exportarImagen(const QString &path, const QString &ext) {
        if (ext == QLatin1String("webp") &&
            !QImageWriter::supportedImageFormats()
                 .contains(QByteArrayLiteral("webp"))) {
            QMessageBox::warning(this, QStringLiteral("WebP no disponible"),
                QStringLiteral(
                    "Qt no tiene el plugin WebP instalado.\n"
                    "En Debian/Ubuntu:  apt install qt6-image-formats-plugins\n"
                    "Se guardará como PNG en su lugar."));
            exportarImagen(QFileInfo(path).absolutePath() + "/"
                           + QFileInfo(path).completeBaseName() + ".png",
                           QStringLiteral("png"));
            return;
        }

        QFont f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        f.setStyleHint(QFont::Monospace);
        f.setFamily(QStringLiteral("monospace"));
        f.setPointSize(fontSizeSpin_->value());

        statusBar()->showMessage(QStringLiteral("Renderizando…"));
        QApplication::setOverrideCursor(Qt::WaitCursor);
        bool tooBig = false;
        QImage img = maker_.renderToImage(baseActual(), f, bgColor_, &tooBig);
        QApplication::restoreOverrideCursor();
        statusBar()->clearMessage();

        if (img.isNull()) {
            QMessageBox::warning(this, QStringLiteral("Error"),
                tooBig
                ? QStringLiteral(
                    "La imagen resultante es demasiado grande.\n"
                    "Baja el tamaño de fuente o la escala.")
                : QStringLiteral("No se pudo renderizar la imagen."));
            return;
        }

        QImageWriter writer(path);
        if (ext == QLatin1String("png")) {
            // El PNG es sin pérdida por definición, así que el control de
            // calidad no le aplica. Lo que interesa es comprimirlo a tope,
            // y aquí Qt tiene dos rarezas que conviene no olvidar:
            //
            //  - setCompression() NO hace nada en el handler de PNG de Qt
            //    6.11: probados del 0 al 9 dan siempre el mismo archivo.
            //  - setQuality() sí funciona pero con la escala invertida:
            //    más calidad = menos compresión = archivo más grande. Con
            //    calidad 92 salen PNG de 5 MB; con 0, de 60 KB.
            writer.setQuality(0);
        } else if (losslessCheck_ && losslessCheck_->isChecked()) {
            // Qt solo cambia a VP8L (lossless) con calidad exactamente 100;
            // con 99 sigue escribiendo VP8 con pérdida.
            writer.setQuality(100);
        } else if (qualitySpin_) {
            writer.setQuality(qualitySpin_->value());
        }
        if (!writer.write(img)) {
            QMessageBox::warning(this, QStringLiteral("Error"),
                QStringLiteral("No se pudo guardar:\n%1")
                    .arg(writer.errorString()));
            return;
        }
        statusLabel_->setText(QStringLiteral("Exportado: %1  (%2×%3)")
            .arg(path).arg(img.width()).arg(img.height()));
    }
};

// =====================================================================
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("ASCII Maker"));
    MainWindow w;
    w.show();
    return app.exec();
}

#include "main.moc"