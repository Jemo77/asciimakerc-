// =========================================================
//  ascii-maker — Imagen -> ASCII con color RGBA 1:1
//  - Clic en la vista ASCII para seleccionar/editar celdas
//  - Exportar a: WebP, PNG, TXT, HTML
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
    // real de la fuente (ver aspectoPorFuente).
    double  aspecto        = 2.0;

    QImage source, adjusted;
    int    cols = 0, rows = 0;
    QVector<AsciiCell> cells;

    void invalidateView() { vistaValida_ = false; }

    // Devuelve la celda (x, y) de la rejilla 1:1 que se ve en esa celda
    // del dibujo. Si el bloque tiene ediciones manuales gana la primera.
    QPair<int, int> viewToSource(int cx, int cy) const {
        if (cols <= 0 || rows <= 0) return {-1, -1};
        const int vr = viewRows();
        if (vr >= rows) return {cx, cy};
        const int y0 = qBound(0, int(qint64(cy)  * rows / vr), rows - 1);
        const int y1 = qBound(y0 + 1, int(qint64(cy + 1) * rows / vr), rows);
        for (int y = y0; y < y1; ++y)
            if (cells[y * cols + cx].manual) return {cx, y};
        return {cx, y0};
    }

    // Inversa: la celda del dibujo donde acaba cayendo un píxel 1:1.
    // El ancho no cambia; solo se reparte el alto.
    QPair<int, int> sourceToView(int sx, int sy) const {
        if (cols <= 0 || rows <= 0) return {-1, -1};
        const int vr = viewRows();
        if (vr >= rows) return {sx, sy};
        return {sx, qBound(0, int(qint64(sy) * vr / rows), vr - 1)};
    }

    // Rejilla 1:1 (un carácter por píxel): la que se exporta a TXT y la
    // que guarda las ediciones manuales.
    const AsciiGrid &viewGrid() const {
        if (vistaValida_) return vista_;
        vistaValida_ = true;
        vista_.cols  = cols;
        vista_.rows  = viewRows();
        vista_.cells = cells;

        if (vista_.isNull() || vista_.rows >= rows) return vista_;

        // Promediamos cada bloque de filas para que el alto del carácter
        // no estire la imagen. Las ediciones manuales no se promedian.
        vista_.cells.resize(qint64(vista_.cols) * vista_.rows);
        for (int y = 0; y < vista_.rows; ++y) {
            const int y0 = qBound(0, int(qint64(y)     * rows / vista_.rows),
                                  rows - 1);
            const int y1 = qBound(y0 + 1,
                                  int(qint64(y + 1) * rows / vista_.rows),
                                  rows);
            for (int x = 0; x < vista_.cols; ++x) {
                const AsciiCell *manual = nullptr;
                int sr = 0, sg = 0, sb = 0, sa = 0, n = 0;
                for (int yy = y0; yy < y1; ++yy) {
                    const AsciiCell &c = cells[yy * cols + x];
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
                vista_.cells[qint64(y) * vista_.cols + x] = dst;
            }
        }
        return vista_;
    }

    void setSource(const QImage &img) {
        source = img.convertToFormat(QImage::Format_ARGB32);
        rebuild();
    }

    void rebuild() {
        // Conservamos las ediciones manuales por índice si el tamaño no cambió.
        const QVector<AsciiCell> old = cells;
        const int oldCols = cols, oldRows = rows;

        vistaValida_ = false;
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
        if (oldCols == cols && oldRows == rows && old.size() == cells.size()) {
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

    QString renderToText() const {
        QString out;
        out.reserve(cols * (rows + 1));
        for (int y = 0; y < rows; ++y) {
            const int off = y * cols;
            for (int x = 0; x < cols; ++x)
                out.append(QChar::fromLatin1(cells[off + x].ch));
            out.append(QLatin1Char('\n'));
        }
        return out;
    }

    QString renderToHtml(const QColor &fondo, double fontPx) const {
        const AsciiGrid &g = viewGrid();
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

    QImage renderToImage(const QFont &font, const QColor &fondo,
                         bool *tooBig = nullptr) const {
        if (tooBig) *tooBig = false;
        const AsciiGrid &g = viewGrid();
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

    // Filas de la rejilla de dibujo una vez compensado el aspecto.
    int viewRows() const {
        if (rows <= 0) return 0;
        return qMax(1, int(std::lround(rows / qMax(1.0, aspecto))));
    }

    bool         tieneAlpha_ = false;
    mutable AsciiGrid  vista_;
    mutable bool  vistaValida_ = false;

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
    AsciiMaker maker_;
    AsciiGrid  vista_;          // rejilla de dibujo (aspecto corregido)
    QImage     originalImage_;
    QString    currentPath_;

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
        maker_.invalidateView();
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
                    maker_.invalidateView();
                    refrescarVista();
                    actualizarEstado();
                    rebuildItem(false);
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
            this, QStringLiteral("Abrir imagen"), QString(),
            QStringLiteral("Imágenes (*.png *.jpg *.jpeg *.bmp *.gif *.webp "
                           "*.tif *.tiff);;Todos (*)"));
        if (path.isEmpty()) return;

        QImageReader reader(path);
        reader.setAutoTransform(true);
        QImage img = reader.read();
        if (img.isNull()) {
            QMessageBox::warning(this, QStringLiteral("Error"),
                QStringLiteral("No se pudo abrir:\n%1")
                    .arg(reader.errorString()));
            return;
        }
        originalImage_ = img;
        currentPath_   = path;

        origScene_->clear();
        origScene_->addPixmap(QPixmap::fromImage(img));
        origScene_->setSceneRect(QRectF(QPointF(0, 0), QSizeF(img.size())));
        origView_->fitAll();

        maker_.setSource(img);
        selCol_ = selRow_ = -1;
        rebuildItem(true);
        setEditorEnabled(false);

        saveAct_->setEnabled(true);
        setWindowTitle(QStringLiteral("ASCII Maker — %1")
                           .arg(QFileInfo(path).fileName()));
    }

    void updateAscii() {
        if (originalImage_.isNull()) return;
        maker_.ramp           = rampEdit_->text();
        if (maker_.ramp.isEmpty()) maker_.ramp = QStringLiteral(" ");
        maker_.invert         = invertCheck_->isChecked();
        maker_.autoLevels     = autoCheck_->isChecked();
        maker_.brightness     = brightSlider_->value() / 100.0;
        maker_.contrast       = contrastSlider_->value() / 100.0;
        maker_.alphaAsSpace   = alphaCheck_->isChecked();
        maker_.alphaThreshold = alphaSpin_->value();
        maker_.scale          = scaleSpin_->value();

        maker_.setSource(originalImage_);

        // la selección puede haberse quedado fuera
        if (selCol_ >= maker_.cols || selRow_ >= maker_.rows)
            selCol_ = selRow_ = -1;
        refrescarVista();
        actualizarEstado();
        actualizarEditorDesdeCelda();
    }

    void actualizarEstado() {
        if (maker_.cols <= 0 || originalImage_.isNull()) return;
        const QString px = QStringLiteral("%1 × %2 px").arg(
            originalImage_.width(), originalImage_.height());
        const QString txt = QStringLiteral(
            "%1  →  %2 × %3 celdas  ·  TXT %2 × %4 (1 carácter = 1 píxel)")
            .arg(px).arg(maker_.cols).arg(vista_.rows).arg(maker_.rows);
        statusLabel_->setText(
            vista_.rows == maker_.rows
                ? txt
                : QStringLiteral("%1  (aspecto %2)").arg(txt).arg(
                      QString::number(maker_.aspecto, 'f', 2)));
    }

    // Copia la rejilla de dibujo a vista_ y la pasa al item.
    void refrescarVista() {
        vista_ = maker_.viewGrid();
        if (asciiItem_) {
            asciiItem_->setGrid(&vista_);
            auto v = maker_.sourceToView(selCol_, selRow_);
            asciiItem_->setSelection(v.first, v.second);
        }
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
        const auto sel = maker_.sourceToView(selCol_, selRow_);
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
        const auto s = maker_.viewToSource(p.first, p.second);
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
        if (selCol_ < 0 || selRow_ < 0 ||
            selCol_ >= maker_.cols || selRow_ >= maker_.rows) {
            celdaInfoLabel_->setText(QStringLiteral("(ninguna)"));
            setEditorEnabled(false);
            return;
        }
        setEditorEnabled(true);
        const AsciiCell &c = maker_.cells[selRow_ * maker_.cols + selCol_];
        updatingEditor_ = true;
        charEdit_->setText(QString(QChar::fromLatin1(c.ch)));
        rSpin_->setValue(c.r);
        gSpin_->setValue(c.g);
        bSpin_->setValue(c.b);
        aSpin_->setValue(c.a);
        updatingEditor_ = false;
        celdaInfoLabel_->setText(QStringLiteral("x=%1  y=%2  px%3%4")
            .arg(selCol_).arg(selRow_)
            .arg(maker_.rows == vista_.rows ? QString()
                                           : QStringLiteral("  (celda %1)")
                                                 .arg(selRow_ * vista_.rows
                                                      / qMax(1, maker_.rows)))
            .arg(c.manual ? QStringLiteral("  · editada") : QString()));
    }

    void aplicarEdicion() {
        if (selCol_ < 0 || selRow_ < 0) return;
        if (selCol_ >= maker_.cols || selRow_ >= maker_.rows) return;
        AsciiCell &c = maker_.cells[selRow_ * maker_.cols + selCol_];

        QString t = charEdit_->text();
        if (t.isEmpty()) t = QStringLiteral(" ");
        c.ch = t.at(0).toLatin1();
        c.r = quint8(rSpin_->value());
        c.g = quint8(gSpin_->value());
        c.b = quint8(bSpin_->value());
        c.a = quint8(aSpin_->value());
        c.manual = true;

        maker_.invalidateView();
        refrescarVista();
        actualizarEditorDesdeCelda();
    }

    void restaurarCelda() {
        if (selCol_ < 0 || selRow_ < 0) return;
        if (selCol_ >= maker_.cols || selRow_ >= maker_.rows) return;
        // volvemos a hacer el cálculo solo para esa celda
        maker_.scale = scaleSpin_->value();
        // rebuild sin tocar los manuales sería más limpio, pero sencillo:
        maker_.cells[selRow_ * maker_.cols + selCol_].manual = false;
        // forzamos un rebuild total sin preservar (solo esa celda perderá el flag)
        // → más simple: hacer un rebuild temporal conservando el resto
        QVector<AsciiCell> manuales = maker_.cells;
        maker_.rebuild();
        for (int i = 0; i < maker_.cells.size() && i < manuales.size(); ++i) {
            if (manuales[i].manual) {
                maker_.cells[i] = manuales[i];
                maker_.cells[i].manual = true;
            }
        }
        maker_.invalidateView();
        refrescarVista();
        actualizarEditorDesdeCelda();
    }

    void limpiarEdiciones() {
        for (AsciiCell &c : maker_.cells) c.manual = false;
        // recalcular desde cero
        QImage img = originalImage_;
        if (img.isNull()) return;
        maker_.setSource(img);       // rebuild completo sin preservar
        if (asciiItem_) {
            selCol_ = selRow_ = -1;
            asciiItem_->setSelection(-1, -1);
            asciiItem_->update();
        }
        refrescarVista();
        setEditorEnabled(false);
        celdaInfoLabel_->setText(QStringLiteral("(ninguna)"));
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

        const QString filtro = QStringLiteral(
            "WebP (*.webp);;PNG (*.png);;Texto (*.txt);;HTML (*.html)");

        const bool soporteWebp =
            QImageWriter::supportedImageFormats()
                .contains(QByteArrayLiteral("webp"));
        const QString extDef = soporteWebp ? QStringLiteral("webp")
                                           : QStringLiteral("png");
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

        if (ext == QLatin1String("txt")) {
            escribirTexto(path, maker_.renderToText());
        } else if (ext == QLatin1String("html") || ext == QLatin1String("htm")) {
            escribirTexto(path, maker_.renderToHtml(bgColor_, fontPx()));
        } else if (ext == QLatin1String("webp") || ext == QLatin1String("png")) {
            exportarImagen(path, ext);
        } else {
            QMessageBox::warning(this, QStringLiteral("Formato no soportado"),
                QStringLiteral("Extensión «.%1» no reconocida. Usa .webp, "
                               ".png, .txt o .html.").arg(ext));
        }
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
        QImage img = maker_.renderToImage(f, bgColor_, &tooBig);
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
        writer.setQuality(92);
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