#include "mainwindow.h"

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QStatusBar>
#include <QToolBar>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QMouseEvent>
#include <QPushButton>
#include <QFileInfo>
#include <QMessageBox>
#include <QColorDialog>
#include <QLabel>
#include <QSlider>
#include <QSplitter>
#include <cmath>
#include <QDebug>
#include <QKeySequence>
#include <QTransform>
#include <algorithm>

// ---------------- Canvas implementation ----------------
Canvas::Canvas(QWidget *parent)
    : QWidget(parent),
      targetImg(nullptr),
      penWidth(6),
      penColor(Qt::black),
      eraserMode(false),
      zoom(1.0),
      currentTool(BRUSH)
{
    // initial blank composite (will be replaced by compositeLayers() from MainWindow)
    composite = QImage(1200, 800, QImage::Format_ARGB32_Premultiplied);
    composite.fill(Qt::white);

    setAttribute(Qt::WA_StaticContents);
    setMinimumSize(400, 300);

    // initialize offsets and state
    imageOffset = QPoint(0, 0);
    lastPoint = QPoint(-1, -1);
    startPoint = QPoint(-1, -1);
}

void Canvas::setCompositeImage(const QImage &c)
{
    composite = c;
    // If target image is null, set targetImg to composite's backing image pointer is not possible,
    // but MainWindow calls setTargetImage separately. Just repaint composite.
    update();
}

void Canvas::setTargetImage(QImage *target)
{
    targetImg = target;
    // make sure the target image has at least composite size or widget size
    ensureTargetSizeMatchesWidget();
}

QImage Canvas::getDisplayedImage() const
{
    return composite;
}

void Canvas::setPenColor(const QColor &c) { penColor = c; eraserMode = false; }
void Canvas::setPenWidth(int w) { penWidth = w; }
void Canvas::setEraserMode(bool on) { eraserMode = on; }

void Canvas::setZoom(double z)
{
    if (z <= 0.0) return;
    zoom = z;
    update();
}

void Canvas::setTool(Tool t)
{
    currentTool = t;
    // set cursor for feedback
    if (t == BRUSH) setCursor(QCursor(Qt::CrossCursor));
    else if (t == ERASER) setCursor(QCursor(Qt::PointingHandCursor));
    else setCursor(QCursor(Qt::CrossCursor));
}

void Canvas::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (composite.isNull()) {
        painter.fillRect(rect(), Qt::white);
        return;
    }

    // Compute displayed size with zoom
    int dispW = int(composite.width() * zoom);
    int dispH = int(composite.height() * zoom);

    // Center the image inside the widget
    int x = (width() - dispW) / 2;
    int y = (height() - dispH) / 2;
    imageOffset = QPoint(x, y);

    QRect targetRect(x, y, dispW, dispH);
    painter.drawImage(targetRect, composite);

    // Optionally: draw a live preview for shape tools (rectangle/ellipse/line)
    // We draw preview on top, in widget coords converted from startPoint (image coords).
    if ((currentTool == RECTANGLE || currentTool == CIRCLE || currentTool == LINE)
        && startPoint != QPoint(-1, -1)) {

        // map image coords to widget coords (reverse of widgetToImage)
        auto imageToWidget = [&](const QPoint &ip) -> QPoint {
            double wx = imageOffset.x() + ip.x() * zoom;
            double wy = imageOffset.y() + ip.y() * zoom;
            return QPoint(int(wx), int(wy));
        };

        QPen previewPen(penColor);
        previewPen.setStyle(Qt::DashLine);
        previewPen.setWidth(std::max(1, penWidth));
        painter.setPen(previewPen);

        QPoint wStart = imageToWidget(startPoint);
        QPoint wNow = mapFromGlobal(QCursor::pos()); // fallback; better to track last mouse pos in widget coords
        // But we don't have the current widget mouse pos here; we will not attempt live preview
        // to keep code simple and robust. (Live preview can be added later.)
    }
}

QPoint Canvas::widgetToImage(const QPoint &p, const QSize &imgSize)
{
    // Map widget point p to image coordinates, taking into account centering and zoom.
    // imageOffset is the top-left of the displayed image in widget coords.
    double ix = (double(p.x()) - double(imageOffset.x())) / zoom;
    double iy = (double(p.y()) - double(imageOffset.y())) / zoom;

    if (!std::isfinite(ix) || !std::isfinite(iy))
        return QPoint(-1, -1);

    // Clamp to image extents
    if (ix < 0.0 || iy < 0.0 || ix >= imgSize.width() || iy >= imgSize.height())
        return QPoint(-1, -1);

    return QPoint(int(ix + 0.5), int(iy + 0.5));
}

void Canvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (!targetImg) return;
        QSize imgSize = targetImg->size();
        QPoint imgPt = widgetToImage(event->pos(), imgSize);
        if (imgPt == QPoint(-1, -1)) return;

        // store points in IMAGE coordinates
        lastPoint = imgPt;
        startPoint = imgPt;

        // notify MainWindow to push undo snapshot BEFORE modifying
        emit strokeStarted();
    }
}

void Canvas::mouseMoveEvent(QMouseEvent *event)
{
    if (!(event->buttons() & Qt::LeftButton) || !targetImg) return;
    QSize imgSize = targetImg->size();
    QPoint imgP = widgetToImage(event->pos(), imgSize);
    if (imgP == QPoint(-1, -1)) {
        // out of image bounds: ignore movement
        return;
    }

    // For brush/eraser: draw as mouse moves (using image coords)
    if (currentTool == BRUSH || currentTool == ERASER) {
        QPainter painter(targetImg);
        painter.setRenderHint(QPainter::Antialiasing, true);

        if (eraserMode) {
            // Clear using CompositionMode_Clear for proper alpha erasing
            painter.setCompositionMode(QPainter::CompositionMode_Clear);
            QPen pen(Qt::transparent, penWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter.setPen(pen);
            painter.drawLine(lastPoint, imgP);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        } else {
            QPen pen(penColor, penWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter.setPen(pen);
            painter.drawLine(lastPoint, imgP);
        }

        // update lastPoint (image coords)
        lastPoint = imgP;

        // notify MainWindow to recomposite and update display
        emit strokeFinished();
    } else {
        // For shapes: we don't commit until mouse release (no live preview here).
        // Could implement preview by storing current mouse widget pos and calling update().
    }
}

void Canvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (!targetImg) return;
    if (event->button() == Qt::LeftButton) {
        QSize imgSize = targetImg->size();
        QPoint endPt = widgetToImage(event->pos(), imgSize);
        if (endPt == QPoint(-1, -1)) {
            // If release outside image, treat as no-op for shapes
            lastPoint = QPoint(-1, -1);
            startPoint = QPoint(-1, -1);
            emit strokeFinished();
            return;
        }

        if (currentTool == LINE || currentTool == RECTANGLE || currentTool == CIRCLE) {
            // commit the shape using image coords startPoint -> endPt
            QPainter painter(targetImg);
            painter.setRenderHint(QPainter::Antialiasing, true);
            QPen pen(penColor, penWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter.setPen(pen);

            if (currentTool == LINE) {
                painter.drawLine(startPoint, endPt);
            } else if (currentTool == RECTANGLE) {
                QRect r(startPoint, endPt);
                painter.drawRect(r.normalized());
            } else if (currentTool == CIRCLE) {
                QRect r(startPoint, endPt);
                painter.drawEllipse(r.normalized());
            }
            // reset points
            lastPoint = QPoint(-1, -1);
            startPoint = QPoint(-1, -1);

            emit strokeFinished();
        } else {
            // brush/eraser: the release doesn't need extra action (already drawn in mouseMove)
            lastPoint = QPoint(-1, -1);
            startPoint = QPoint(-1, -1);
            emit strokeFinished();
        }
    }
}

void Canvas::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    ensureTargetSizeMatchesWidget();
}

void Canvas::ensureTargetSizeMatchesWidget()
{
    if (!targetImg) return;
    // Ensure the target image has at least the same pixel size as the composite image if composite is larger
    QSize wsize = targetImg->size();
    // We don't force it to widget size here; keep simple: if too small, expand to composite size
    if (targetImg->width() < composite.width() || targetImg->height() < composite.height()) {
        int newW = qMax(targetImg->width(), composite.width());
        int newH = qMax(targetImg->height(), composite.height());
        QImage newImg(newW, newH, QImage::Format_ARGB32_Premultiplied);
        newImg.fill(Qt::transparent);
        QPainter p(&newImg);
        p.drawImage(0, 0, *targetImg);
        *targetImg = newImg;
    }
}

// ---------------- MainWindow implementation ----------------
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      activeLayerIndex(-1),
      brushSize(6),
      brushColor(Qt::black),
      zoomFactor(1.0)
{
    setWindowTitle("EpiGrimp - Layers + Undo/Redo prototype");
    resize(1200, 800);

    // central: left = layer panel, right = canvas (splitter)
    QWidget *central = new QWidget(this);
    QHBoxLayout *hl = new QHBoxLayout(central);

    // layer panel
    QVBoxLayout *leftLayout = new QVBoxLayout();
    layerListWidget = new QListWidget(this);
    layerListWidget->setFixedWidth(220);
    leftLayout->addWidget(new QLabel("Layers", this));
    leftLayout->addWidget(layerListWidget);

    // layer buttons
    QPushButton *addBtn = new QPushButton("Add Layer", this);
    QPushButton *delBtn = new QPushButton("Remove Layer", this);
    leftLayout->addWidget(addBtn);
    leftLayout->addWidget(delBtn);

    hl->addLayout(leftLayout);

    // canvas area
    canvas = new Canvas(this);
    hl->addWidget(canvas, 1);

    setCentralWidget(central);

    // status bar
    statusLabel = new QLabel("Ready", this);
    statusBar()->addWidget(statusLabel);

    // initial two layers (bottom = background white, top = transparent)
    Layer bg;
    bg.name = "Background";
    bg.image = QImage(1600, 1200, QImage::Format_ARGB32_Premultiplied);
    bg.image.fill(Qt::white);
    layers.append(bg);

    Layer top;
    top.name = "Layer 1";
    top.image = QImage(1600, 1200, QImage::Format_ARGB32_Premultiplied);
    top.image.fill(Qt::transparent);
    layers.append(top);

    // set active layer to top (index 1)
    activeLayerIndex = layers.size() - 1;

    // fill layer list UI (show top at top)
    for (int i = layers.size()-1; i >= 0; --i) {
        layerListWidget->addItem(layers[i].name);
    }
    layerListWidget->setCurrentRow(0);

    // connect UI
    connect(addBtn, &QPushButton::clicked, this, &MainWindow::addLayer);
    connect(delBtn, &QPushButton::clicked, this, &MainWindow::removeLayer);
    connect(layerListWidget, &QListWidget::currentRowChanged, this, &MainWindow::activateLayer);

    // canvas <-> mainwindow signals
    connect(canvas, &Canvas::strokeStarted, this, &MainWindow::onStrokeStarted);
    connect(canvas, &Canvas::strokeFinished, this, &MainWindow::onStrokeFinished);

    // set initial target and composite
    canvas->setTargetImage(&layers[activeLayerIndex].image);
    compositeLayers();

    setupMenu();
    setupToolbarAndPalette();
    statusLabel->setText("Ready - active layer: " + layers[activeLayerIndex].name);
}

MainWindow::~MainWindow() = default;

void MainWindow::setupMenu()
{
    QMenu *fileMenu = menuBar()->addMenu("&File");

    QAction *openAct = new QAction("&Open...", this);
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::openFile);
    fileMenu->addAction(openAct);

    QAction *saveAct = new QAction("&Save Composite...", this);
    saveAct->setShortcut(QKeySequence::Save);
    connect(saveAct, &QAction::triggered, this, &MainWindow::saveFile);
    fileMenu->addAction(saveAct);

    fileMenu->addSeparator();

    QAction *clearAct = new QAction("&Clear Active Layer", this);
    connect(clearAct, &QAction::triggered, this, &MainWindow::clearCanvas);
    fileMenu->addAction(clearAct);

    fileMenu->addSeparator();

    QAction *quitAct = new QAction("&Quit", this);
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, this, &MainWindow::close);
    fileMenu->addAction(quitAct);

    // Filters menu (Day 8)
    QMenu *filterMenu = menuBar()->addMenu("&Filters");
    QAction *gray = new QAction("Grayscale", this);
    connect(gray, &QAction::triggered, this, &MainWindow::grayscale);
    filterMenu->addAction(gray);
    QAction *invert = new QAction("Invert", this);
    connect(invert, &QAction::triggered, this, &MainWindow::invertColors);
    filterMenu->addAction(invert);
}

QWidget* createColorBtn(const QColor &color, QObject *receiver, const char *slot)
{
    QPushButton *btn = new QPushButton();
    btn->setFixedSize(24, 24);
    btn->setStyleSheet(QString("background-color: %1;").arg(color.name()));
    btn->setProperty("color", color.name());
    QObject::connect(btn, SIGNAL(clicked()), receiver, slot);
    return btn;
}

void MainWindow::setupToolbarAndPalette()
{
    QToolBar *tb = addToolBar("Tools");
    tb->setMovable(false);

    QAction *undoAct = new QAction("Undo", this);
    undoAct->setShortcut(QKeySequence::Undo);
    connect(undoAct, &QAction::triggered, this, &MainWindow::undo);
    tb->addAction(undoAct);

    QAction *redoAct = new QAction("Redo", this);
    redoAct->setShortcut(QKeySequence::Redo);
    connect(redoAct, &QAction::triggered, this, &MainWindow::redo);
    tb->addAction(redoAct);

    tb->addSeparator();

    QAction *brushAct = new QAction("Brush (B)", this);
    brushAct->setShortcut(Qt::Key_B);
    connect(brushAct, &QAction::triggered, this, &MainWindow::selectBrush);
    tb->addAction(brushAct);

    QAction *eraserAct = new QAction("Eraser (E)", this);
    eraserAct->setShortcut(Qt::Key_E);
    connect(eraserAct, &QAction::triggered, this, &MainWindow::selectEraser);
    tb->addAction(eraserAct);

    tb->addSeparator();

    // shape tools
    QAction *lineAct = new QAction("Line", this);
    connect(lineAct, &QAction::triggered, [this]() {
        canvas->setTool(Canvas::LINE);
        statusLabel->setText("Line tool");
    });
    tb->addAction(lineAct);

    QAction *rectAct = new QAction("Rectangle", this);
    connect(rectAct, &QAction::triggered, [this]() {
        canvas->setTool(Canvas::RECTANGLE);
        statusLabel->setText("Rectangle tool");
    });
    tb->addAction(rectAct);

    QAction *circleAct = new QAction("Circle", this);
    connect(circleAct, &QAction::triggered, [this]() {
        canvas->setTool(Canvas::CIRCLE);
        statusLabel->setText("Circle tool");
    });
    tb->addAction(circleAct);

    tb->addSeparator();

    // Day 5 transforms & zoom
    QAction *zoomInAct = new QAction("Zoom +", this);
    connect(zoomInAct, &QAction::triggered, this, &MainWindow::zoomIn);
    tb->addAction(zoomInAct);
    QAction *zoomOutAct = new QAction("Zoom -", this);
    connect(zoomOutAct, &QAction::triggered, this, &MainWindow::zoomOut);
    tb->addAction(zoomOutAct);

    QAction *rotL = new QAction("Rotate ⟲", this);
    connect(rotL, &QAction::triggered, this, &MainWindow::rotateLeft);
    tb->addAction(rotL);

    QAction *rotR = new QAction("Rotate ⟳", this);
    connect(rotR, &QAction::triggered, this, &MainWindow::rotateRight);
    tb->addAction(rotR);

    QAction *flipH = new QAction("Flip H", this);
    connect(flipH, &QAction::triggered, this, &MainWindow::flipHorizontal);
    tb->addAction(flipH);

    QAction *flipV = new QAction("Flip V", this);
    connect(flipV, &QAction::triggered, this, &MainWindow::flipVertical);
    tb->addAction(flipV);

    tb->addSeparator();

    // brush size slider
    QLabel *szLabel = new QLabel("Size:", this);
    tb->addWidget(szLabel);
    QSlider *sizeSlider = new QSlider(Qt::Horizontal, this);
    sizeSlider->setRange(1, 80);
    sizeSlider->setValue(brushSize);
    sizeSlider->setFixedWidth(120);
    tb->addWidget(sizeSlider);
    connect(sizeSlider, &QSlider::valueChanged, this, &MainWindow::changeBrushSize);

    tb->addSeparator();

    // palette
    QList<QColor> palette = { Qt::black, Qt::red, Qt::green, Qt::blue, QColor("#FFA500") };
    for (const QColor &c : palette) {
        QWidget *btn = createColorBtn(c, this, SLOT(setColorFromButton()));
        tb->addWidget(btn);
    }

    QAction *pickColor = new QAction("Pick Color...", this);
    tb->addAction(pickColor);
    connect(pickColor, &QAction::triggered, this, &MainWindow::chooseColor);
}

void MainWindow::openFile()
{
    QString fileName = QFileDialog::getOpenFileName(this, "Open Image", QString(),
                                                    "Images (*.png *.jpg *.bmp);;All Files (*)");
    if (fileName.isEmpty()) return;

    QImage loaded;
    if (!loaded.load(fileName)) {
        QMessageBox::warning(this, "Open failed", "Could not open image.");
        return;
    }

    // place loaded image onto active layer (preserve transparency if possible)
    layers[activeLayerIndex].image = loaded.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    // clear undo/redo
    layers[activeLayerIndex].undoStack.clear();
    layers[activeLayerIndex].redoStack.clear();

    compositeLayers();
    canvas->setTargetImage(&layers[activeLayerIndex].image);
    statusLabel->setText(QFileInfo(fileName).fileName() + " loaded into " + layers[activeLayerIndex].name);
}

void MainWindow::saveFile()
{
    QString fileName = QFileDialog::getSaveFileName(this, "Save Composite As", QString(),
                                                    "PNG Image (*.png);;JPEG Image (*.jpg);;BMP Image (*.bmp)");
    if (fileName.isEmpty()) return;

    QImage composite = canvas->getDisplayedImage();
    if (!composite.save(fileName)) {
        QMessageBox::warning(this, "Save failed", "Unable to save file.");
        return;
    }
    statusLabel->setText(QFileInfo(fileName).fileName() + " saved");
}

void MainWindow::clearCanvas()
{
    // clear active layer (fill transparent)
    if (activeLayerIndex < 0 || activeLayerIndex >= layers.size()) return;
    pushUndoForActiveLayer();
    layers[activeLayerIndex].image.fill(Qt::transparent);
    clearRedoForActiveLayer();
    compositeLayers();
    statusLabel->setText(layers[activeLayerIndex].name + " cleared");
}

void MainWindow::addLayer()
{
    Layer l;
    l.name = QString("Layer %1").arg(layers.size());
    l.image = QImage(layers[0].image.size(), QImage::Format_ARGB32_Premultiplied);
    l.image.fill(Qt::transparent);
    layers.append(l);

    // update UI list: we show top layer at index 0, so insert at top
    layerListWidget->insertItem(0, l.name);
    layerListWidget->setCurrentRow(0);

    // set active to newly added layer
    activeLayerIndex = layers.size() - 1;
    canvas->setTargetImage(&layers[activeLayerIndex].image);
    compositeLayers();
    statusLabel->setText("Added " + l.name);
}

void MainWindow::removeLayer()
{
    if (layers.size() <= 1) {
        QMessageBox::information(this, "Cannot remove", "Need at least one layer.");
        return;
    }
    // Remove active layer
    int idx = activeLayerIndex;
    layers.removeAt(idx);
    // update UI list: remove corresponding (remember UI order is reversed)
    int uiIndex = (layers.size()) - idx; // after removal
    if (uiIndex >= 0 && uiIndex < layerListWidget->count())
        delete layerListWidget->takeItem(uiIndex);

    // set active to topmost layer
    activeLayerIndex = layers.size() - 1;
    layerListWidget->setCurrentRow(0);
    canvas->setTargetImage(&layers[activeLayerIndex].image);
    compositeLayers();
    statusLabel->setText("Layer removed, active: " + layers[activeLayerIndex].name);
}

void MainWindow::activateLayer(int uiRow)
{
    // uiRow = 0 means top layer; internal index = layers.size()-1 - uiRow
    int idx = layers.size() - 1 - uiRow;
    if (idx < 0 || idx >= layers.size()) return;
    activeLayerIndex = idx;
    canvas->setTargetImage(&layers[activeLayerIndex].image);
    statusLabel->setText("Active layer: " + layers[activeLayerIndex].name);
}

void MainWindow::compositeLayers()
{
    if (layers.isEmpty()) return;
    QImage comp = layers[0].image; // start with bottom
    QPainter p(&comp);
    // paint layers 1..n-1 on top
    for (int i = 1; i < layers.size(); ++i) {
        p.drawImage(0, 0, layers[i].image);
    }
    canvas->setCompositeImage(comp);
}

void MainWindow::pushUndoForActiveLayer()
{
    if (activeLayerIndex < 0 || activeLayerIndex >= layers.size()) return;
    Layer &L = layers[activeLayerIndex];
    // push current image snapshot to undo
    L.undoStack.append(L.image.copy());
    // keep undo stack to reasonable size
    const int MAX_UNDO = 20;
    if (L.undoStack.size() > MAX_UNDO) L.undoStack.remove(0);
}

void MainWindow::clearRedoForActiveLayer()
{
    if (activeLayerIndex < 0 || activeLayerIndex >= layers.size()) return;
    layers[activeLayerIndex].redoStack.clear();
}

void MainWindow::onStrokeStarted()
{
    // called when Canvas mouse presses; prepare undo snapshot
    pushUndoForActiveLayer();
    clearRedoForActiveLayer();
}

void MainWindow::onStrokeFinished()
{
    // recomposite after each stroke or intermediate move
    compositeLayers();
}

void MainWindow::undo()
{
    if (activeLayerIndex < 0 || activeLayerIndex >= layers.size()) return;
    Layer &L = layers[activeLayerIndex];
    if (L.undoStack.isEmpty()) {
        statusLabel->setText("Nothing to undo");
        return;
    }
    // move current to redo, pop last undo into current
    L.redoStack.append(L.image.copy());
    QImage prev = L.undoStack.takeLast();
    L.image = prev;
    compositeLayers();
    statusLabel->setText("Undo on " + L.name);
}

void MainWindow::redo()
{
    if (activeLayerIndex < 0 || activeLayerIndex >= layers.size()) return;
    Layer &L = layers[activeLayerIndex];
    if (L.redoStack.isEmpty()) {
        statusLabel->setText("Nothing to redo");
        return;
    }
    L.undoStack.append(L.image.copy());
    QImage next = L.redoStack.takeLast();
    L.image = next;
    compositeLayers();
    statusLabel->setText("Redo on " + L.name);
}

void MainWindow::selectBrush()
{
    canvas->setEraserMode(false);
    canvas->setPenColor(brushColor);
    canvas->setPenWidth(brushSize);
    canvas->setTool(Canvas::BRUSH);
    statusLabel->setText("Brush selected");
}

void MainWindow::selectEraser()
{
    canvas->setEraserMode(true);
    canvas->setPenWidth(brushSize);
    canvas->setTool(Canvas::ERASER);
    statusLabel->setText("Eraser selected");
}

void MainWindow::setColorFromButton()
{
    QPushButton *b = qobject_cast<QPushButton*>(sender());
    if (!b) return;
    QVariant v = b->property("color");
    if (!v.isValid()) return;
    QColor c(v.toString());
    if (c.isValid()) {
        brushColor = c;
        if (activeLayerIndex >= 0 && activeLayerIndex < layers.size())
            canvas->setPenColor(brushColor);
        statusLabel->setText("Color: " + brushColor.name());
    }
}

void MainWindow::chooseColor()
{
    QColor c = QColorDialog::getColor(brushColor, this, "Pick color");
    if (c.isValid()) {
        brushColor = c;
        canvas->setPenColor(brushColor);
        statusLabel->setText("Color chosen: " + brushColor.name());
    }
}

void MainWindow::changeBrushSize(int v)
{
    brushSize = v;
    canvas->setPenWidth(v);
    statusLabel->setText(QString("Brush size: %1").arg(v));
}

// ---------------- Day 5 transforms ----------------
void MainWindow::zoomIn()
{
    zoomFactor += 0.1;
    if (zoomFactor > 5.0) zoomFactor = 5.0;
    canvas->setZoom(zoomFactor);
    statusLabel->setText(QString("Zoom: %1x").arg(QString::number(zoomFactor,'f',1)));
}

void MainWindow::zoomOut()
{
    zoomFactor -= 0.1;
    if (zoomFactor < 0.2) zoomFactor = 0.2;
    canvas->setZoom(zoomFactor);
    statusLabel->setText(QString("Zoom: %1x").arg(QString::number(zoomFactor,'f',1)));
}

void MainWindow::rotateLeft()
{
    if (activeLayerIndex < 0 || activeLayerIndex >= layers.size()) return;
    QTransform transform;
    transform.rotate(-90);
    layers[activeLayerIndex].image = layers[activeLayerIndex].image.transformed(transform);
    compositeLayers();
    statusLabel->setText("Rotated left");
}

void MainWindow::rotateRight()
{
    if (activeLayerIndex < 0 || activeLayerIndex >= layers.size()) return;
    QTransform transform;
    transform.rotate(90);
    layers[activeLayerIndex].image = layers[activeLayerIndex].image.transformed(transform);
    compositeLayers();
    statusLabel->setText("Rotated right");
}

void MainWindow::flipHorizontal()
{
    if (activeLayerIndex < 0 || activeLayerIndex >= layers.size()) return;
    layers[activeLayerIndex].image = layers[activeLayerIndex].image.mirrored(true, false);
    compositeLayers();
    statusLabel->setText("Flipped horizontally");
}

void MainWindow::flipVertical()
{
    if (activeLayerIndex < 0 || activeLayerIndex >= layers.size()) return;
    layers[activeLayerIndex].image = layers[activeLayerIndex].image.mirrored(false, true);
    compositeLayers();
    statusLabel->setText("Flipped vertically");
}

// ---------------- Day 8 filters ----------------
void MainWindow::grayscale()
{
    if (activeLayerIndex < 0 || activeLayerIndex >= layers.size()) return;
    QImage &img = layers[activeLayerIndex].image;
    // convert to ARGB if necessary
    if (img.format() != QImage::Format_ARGB32 && img.format() != QImage::Format_ARGB32_Premultiplied)
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    for (int y = 0; y < img.height(); ++y) {
        QRgb *scan = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            QColor c(scan[x]);
            int g = qGray(c.rgb());
            scan[x] = QColor(g, g, g, c.alpha()).rgba();
        }
    }
    compositeLayers();
    statusLabel->setText("Grayscale applied to " + layers[activeLayerIndex].name);
}

void MainWindow::invertColors()
{
    if (activeLayerIndex < 0 || activeLayerIndex >= layers.size()) return;
    QImage &img = layers[activeLayerIndex].image;
    if (img.format() != QImage::Format_ARGB32 && img.format() != QImage::Format_ARGB32_Premultiplied)
        img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    for (int y = 0; y < img.height(); ++y) {
        QRgb *scan = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            QColor c(scan[x]);
            QColor n(255 - c.red(), 255 - c.green(), 255 - c.blue(), c.alpha());
            scan[x] = n.rgba();
        }
    }
    compositeLayers();
    statusLabel->setText("Inverted colors on " + layers[activeLayerIndex].name);
}

// end of file
