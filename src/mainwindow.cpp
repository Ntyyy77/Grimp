#include "mainwindow.h"

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QFontComboBox>
#include <QSpinBox>
#include <QFileDialog>
#include <QStatusBar>
#include <QToolBar>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QPainter>
#include <QActionGroup>
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

void Canvas::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.drawImage(imageOffset.x(), imageOffset.y(),
                      composite.scaled(composite.size()*zoom));


    for (int i = 0; i < textItems.size(); ++i) {
        const TextItem &t = textItems[i];

        painter.setFont(t.font);
        painter.setPen(t.color);

        QPoint wPos(imageOffset.x() + t.position.x()*zoom,
                    imageOffset.y() + t.position.y()*zoom);

        painter.drawText(wPos, t.text);

        if (t.selected) {
            painter.setPen(QPen(Qt::blue, 1, Qt::DashLine));
            QRect scaledRect = QRect(wPos, t.boundingRect.size() * zoom);
            painter.drawRect(scaledRect);
        }
    }


    QPen selPen(Qt::DashLine);
    selPen.setColor(Qt::blue);
    selPen.setWidth(1);
    painter.setPen(selPen);

    if (currentTool == RECT_SELECT && !selectionRect.isNull()) {
        QRect wRect = QRect(
            imageOffset + selectionRect.topLeft()*zoom,
            imageOffset + selectionRect.bottomRight()*zoom
        );
        painter.drawRect(wRect.normalized());
    } else if (currentTool == LASSO_SELECT && !lassoPolygon.isEmpty()) {
        QPolygon wPoly;
        for (const QPoint &p : lassoPolygon) {
            wPoly << QPoint(imageOffset.x() + p.x()*zoom, imageOffset.y() + p.y()*zoom);
        }
        painter.drawPolygon(wPoly);
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
    if (currentTool == TEXT && event->button() == Qt::LeftButton && targetImg) {
        QPoint imgPt = widgetToImage(event->pos(), targetImg->size());
        if (imgPt == QPoint(-1,-1)) return;

        // Sélection texte existant
        activeTextIndex = -1;
        for (int i = textItems.size() - 1; i >= 0; --i) {
            if (textItems[i].boundingRect
                    .translated(textItems[i].position)
                    .contains(imgPt)) {
                activeTextIndex = i;
                textItems[i].selected = true;
                lastPoint = imgPt;
                update();
                return;
            }
        }

        // Nouveau texte
        bool ok;
        QString txt = QInputDialog::getText(
            this, "Add Text", "Text:", QLineEdit::Normal, "", &ok
        );
        if (!ok || txt.isEmpty()) return;

        TextItem item;
        item.text = txt;
        item.position = imgPt;
        item.font = QFont("Arial", 24);
        item.color = penColor;

        QFontMetrics fm(item.font);
        item.boundingRect = fm.boundingRect(item.text);

        textItems.append(item);
        activeTextIndex = textItems.size() - 1;

        update();
        return;
    }

    if (event->button() == Qt::LeftButton && targetImg) {
        QPoint imgPt = widgetToImage(event->pos(), targetImg->size());
        if (imgPt == QPoint(-1,-1)) return;

        startPoint = imgPt;
        lastPoint = imgPt;

        if (currentTool == RECT_SELECT) {
            selecting = true;
            selectionRect = QRect(imgPt, imgPt);
        } else if (currentTool == LASSO_SELECT) {
            selecting = true;
            lassoPolygon.clear();
            lassoPolygon << imgPt;
        }

        emit strokeStarted(); // pour undo
    }
}


void Canvas::mouseMoveEvent(QMouseEvent *event)
{
    if (currentTool == TEXT && activeTextIndex >= 0 &&
        (event->buttons() & Qt::LeftButton)) {

        QPoint imgPt = widgetToImage(event->pos(), targetImg->size());
        if (imgPt == QPoint(-1,-1)) return;

        QPoint delta = imgPt - lastPoint;
        textItems[activeTextIndex].position += delta;
        lastPoint = imgPt;

        update();
        return;
    }
    if (!(event->buttons() & Qt::LeftButton) || !targetImg) return;
    QPoint imgPt = widgetToImage(event->pos(), targetImg->size());
    if (imgPt == QPoint(-1,-1)) return;

    if (currentTool == RECT_SELECT && selecting) {
        selectionRect.setBottomRight(imgPt);
        update(); // redraw pour visualiser
    } else if (currentTool == LASSO_SELECT && selecting) {
        lassoPolygon << imgPt;
        update(); // redraw
    }
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
        lastPoint = imgP;
        emit strokeFinished();
    } else {
    }
}

void Canvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && selecting) {
        QPoint imgPt = widgetToImage(event->pos(), targetImg->size());
        if (imgPt != QPoint(-1,-1)) {
            if (currentTool == RECT_SELECT) {
                selectionRect.setBottomRight(imgPt);
            } else if (currentTool == LASSO_SELECT) {
                lassoPolygon << imgPt;
            }
        }
        selecting = false;

        // **Met à jour hasSelection ici**
        if (currentTool == RECT_SELECT && !selectionRect.isNull())
            _hasSelection = true;
        else if (currentTool == LASSO_SELECT && !lassoPolygon.isEmpty())
            _hasSelection = true;
        else
            _hasSelection = false;
        emit strokeFinished();
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
    // après avoir créé layerListWidget
    layerListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(layerListWidget, &QListWidget::customContextMenuRequested,
            this, &MainWindow::onLayerContextMenu);


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

    QShortcut *copyShortcut = new QShortcut(QKeySequence("Ctrl+C"), this);
    connect(copyShortcut, &QShortcut::activated, this, &MainWindow::copySelection);

    QShortcut *cutShortcut = new QShortcut(QKeySequence("Ctrl+X"), this);
    connect(cutShortcut, &QShortcut::activated, this, &MainWindow::cutSelection);

    QShortcut *pasteShortcut = new QShortcut(QKeySequence("Ctrl+V"), this);
    connect(pasteShortcut, &QShortcut::activated, this, &MainWindow::pasteSelection);

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

    // --- Undo / Redo ---
    QAction *undoAct = new QAction("Undo", this);
    undoAct->setShortcut(QKeySequence::Undo);
    connect(undoAct, &QAction::triggered, this, &MainWindow::undo);
    tb->addAction(undoAct);

    QAction *redoAct = new QAction("Redo", this);
    redoAct->setShortcut(QKeySequence::Redo);
    connect(redoAct, &QAction::triggered, this, &MainWindow::redo);
    tb->addAction(redoAct);

    tb->addSeparator();

    // --- Outils dessin avec QActionGroup (exclusifs) ---
    QActionGroup* toolGroup = new QActionGroup(this);
    toolGroup->setExclusive(true); // un seul actif à la fois

    // Brush
    QAction *brushAct = new QAction("Brush (B)", this);
    brushAct->setShortcut(Qt::Key_B);
    brushAct->setCheckable(true);
    connect(brushAct, &QAction::triggered, this, &MainWindow::selectBrush);
    tb->addAction(brushAct);
    toolGroup->addAction(brushAct);

    // Eraser
    QAction *eraserAct = new QAction("Eraser (E)", this);
    eraserAct->setShortcut(Qt::Key_E);
    eraserAct->setCheckable(true);
    connect(eraserAct, &QAction::triggered, this, &MainWindow::selectEraser);
    tb->addAction(eraserAct);
    toolGroup->addAction(eraserAct);

    tb->addSeparator();

    // Shape tools
    QAction *lineAct = new QAction("Line", this);
    lineAct->setCheckable(true);
    connect(lineAct, &QAction::triggered, [this]() {
        canvas->setTool(Canvas::LINE);
        statusLabel->setText("Line tool");
    });
    tb->addAction(lineAct);
    toolGroup->addAction(lineAct);

    QAction *rectAct = new QAction("Rectangle", this);
    rectAct->setCheckable(true);
    connect(rectAct, &QAction::triggered, [this]() {
        canvas->setTool(Canvas::RECTANGLE);
        statusLabel->setText("Rectangle tool");
    });
    tb->addAction(rectAct);
    toolGroup->addAction(rectAct);

    QAction *circleAct = new QAction("Circle", this);
    circleAct->setCheckable(true);
    connect(circleAct, &QAction::triggered, [this]() {
        canvas->setTool(Canvas::CIRCLE);
        statusLabel->setText("Circle tool");
    });
    tb->addAction(circleAct);
    toolGroup->addAction(circleAct);

    tb->addSeparator();

    // Selection tools
    QAction *rectSelAct = new QAction("Rect Select", this);
    rectSelAct->setCheckable(true);
    connect(rectSelAct, &QAction::triggered, [this]() {
        canvas->setTool(Canvas::RECT_SELECT);
        statusLabel->setText("Rectangular selection tool");
    });
    tb->addAction(rectSelAct);
    toolGroup->addAction(rectSelAct);

    QAction *lassoSelAct = new QAction("Lasso Select", this);
    lassoSelAct->setCheckable(true);
    connect(lassoSelAct, &QAction::triggered, [this]() {
        canvas->setTool(Canvas::LASSO_SELECT);
        statusLabel->setText("Free selection (lasso) tool");
    });
    tb->addAction(lassoSelAct);
    toolGroup->addAction(lassoSelAct);

    tb->addSeparator();

    // Copy / Cut / Paste (non exclusif)
    QAction *copyAct = new QAction("Copy", this);
    connect(copyAct, &QAction::triggered, this, &MainWindow::copySelection);
    tb->addAction(copyAct);

    QAction *cutAct = new QAction("Cut", this);
    connect(cutAct, &QAction::triggered, this, &MainWindow::cutSelection);
    tb->addAction(cutAct);

    QAction *pasteAct = new QAction("Paste", this);
    connect(pasteAct, &QAction::triggered, this, &MainWindow::pasteSelection);
    tb->addAction(pasteAct);

    tb->addSeparator();

    // --- Zoom / Rotation / Flip (non exclusifs) ---
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

    // --- Brush size ---
    QLabel *szLabel = new QLabel("Size:", this);
    tb->addWidget(szLabel);
    QSlider *sizeSlider = new QSlider(Qt::Horizontal, this);
    sizeSlider->setRange(1, 80);
    sizeSlider->setValue(brushSize);
    sizeSlider->setFixedWidth(120);
    tb->addWidget(sizeSlider);
    connect(sizeSlider, &QSlider::valueChanged, this, &MainWindow::changeBrushSize);

    tb->addSeparator();

    // --- Palette ---
    QList<QColor> palette = { Qt::black, Qt::red, Qt::green, Qt::blue, QColor("#FFA500") };
    for (const QColor &c : palette) {
        QWidget *btn = createColorBtn(c, this, SLOT(setColorFromButton()));
        tb->addWidget(btn);
    }

    QAction *pickColor = new QAction("Pick Color...", this);
    tb->addAction(pickColor);
    connect(pickColor, &QAction::triggered, this, &MainWindow::chooseColor);

    // --- Style pour highlight automatique des outils ---
    tb->setStyleSheet(
        "QToolButton:checked { background-color: lightblue; border: 2px solid blue; }"
    );

    // --- Initial tool sélectionné ---
    brushAct->setChecked(true);

    QAction *textAct = new QAction("Text (T)", this);
    textAct->setShortcut(Qt::Key_T);
    textAct->setCheckable(true);
    connect(textAct, &QAction::triggered, [this]() {
        canvas->commitTextItems();      // sécurise le texte précédent
        canvas->setTool(Canvas::TEXT);
        statusLabel->setText("Text tool");
    });
    tb->addAction(textAct);
    toolGroup->addAction(textAct);

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

    QImage comp = layers[0].image;
    QPainter p(&comp);

    for (int i = 1; i < layers.size(); ++i) {
        p.setOpacity(layers[i].opacity);
        p.drawImage(0, 0, layers[i].image);
    }
    p.setOpacity(1.0);

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
    canvas->commitTextItems();
    canvas->setEraserMode(false);
    canvas->setPenColor(brushColor);
    canvas->setPenWidth(brushSize);
    canvas->setTool(Canvas::BRUSH);
    statusLabel->setText("Brush selected");
}

void MainWindow::selectEraser()
{
    canvas->commitTextItems();
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
    QImage preview = img.copy(); // copie pour prévisualisation

    // appliquer filtre à la copie
    for (int y = 0; y < preview.height(); ++y) {
        QRgb *scan = reinterpret_cast<QRgb*>(preview.scanLine(y));
        for (int x = 0; x < preview.width(); ++x) {
            QColor c(scan[x]);
            int g = qGray(c.rgb());
            scan[x] = QColor(g, g, g, c.alpha()).rgba();
        }
    }

    // créer un dialog avec prévisualisation
    QDialog dlg(this);
    dlg.setWindowTitle("Apply Grayscale?");
    dlg.resize(400, 300);

    QVBoxLayout *lay = new QVBoxLayout(&dlg);
    QLabel *imgLabel = new QLabel(&dlg);
    imgLabel->setPixmap(QPixmap::fromImage(preview).scaled(380, 250, Qt::KeepAspectRatio));
    lay->addWidget(imgLabel);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *okBtn = new QPushButton("OK", &dlg);
    QPushButton *cancelBtn = new QPushButton("Cancel", &dlg);
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);
    lay->addLayout(btnLayout);

    connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    // Enter = OK, Esc = Cancel
    dlg.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dlg.setModal(true);

    if (dlg.exec() == QDialog::Accepted) {
        pushUndoForActiveLayer();
        clearRedoForActiveLayer();
        img = preview; // appliquer la modification
        compositeLayers();
        statusLabel->setText("Grayscale applied to " + layers[activeLayerIndex].name);
    } else {
        statusLabel->setText("Grayscale canceled");
    }
}

void MainWindow::invertColors()
{
    if (activeLayerIndex < 0 || activeLayerIndex >= layers.size()) return;

    QImage &img = layers[activeLayerIndex].image;
    QImage preview = img.copy(); // copie pour prévisualisation

    // appliquer le filtre inversé sur la copie
    for (int y = 0; y < preview.height(); ++y) {
        QRgb *scan = reinterpret_cast<QRgb*>(preview.scanLine(y));
        for (int x = 0; x < preview.width(); ++x) {
            QColor c(scan[x]);
            QColor n(255 - c.red(), 255 - c.green(), 255 - c.blue(), c.alpha());
            scan[x] = n.rgba();
        }
    }

    // créer un dialog pour prévisualisation
    QDialog dlg(this);
    dlg.setWindowTitle("Apply Invert Colors?");
    dlg.resize(400, 300);

    QVBoxLayout *lay = new QVBoxLayout(&dlg);
    QLabel *imgLabel = new QLabel(&dlg);
    imgLabel->setPixmap(QPixmap::fromImage(preview).scaled(380, 250, Qt::KeepAspectRatio));
    lay->addWidget(imgLabel);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    QPushButton *okBtn = new QPushButton("OK", &dlg);
    QPushButton *cancelBtn = new QPushButton("Cancel", &dlg);
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);
    lay->addLayout(btnLayout);

    connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    dlg.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    dlg.setModal(true);

    // si OK ou Entrée -> appliquer, sinon Échap -> annuler
    if (dlg.exec() == QDialog::Accepted) {
        pushUndoForActiveLayer();
        clearRedoForActiveLayer();
        img = preview; // appliquer la modification
        compositeLayers();
        statusLabel->setText("Inverted colors applied to " + layers[activeLayerIndex].name);
    } else {
        statusLabel->setText("Invert canceled");
    }
}

void MainWindow::copySelection()
{
    if (!canvas->hasSelection()) return; // ← utilise Canvas
    selectionBuffer = canvas->getSelectionImage();
    statusLabel->setText("Selection copied");
}

void MainWindow::cutSelection()
{
    if (!canvas->hasSelection()) return;
    pushUndoForActiveLayer();
    selectionBuffer = canvas->getSelectionImage();

    // Clear selection on layer
    QPainter p(&layers[activeLayerIndex].image);
    p.setCompositionMode(QPainter::CompositionMode_Clear);
    p.fillRect(canvas->getSelectionRect(), Qt::transparent);
    compositeLayers();
    statusLabel->setText("Selection cut");
}

void MainWindow::pasteSelection()
{
    if (selectionBuffer.isNull()) return;
    pushUndoForActiveLayer();
    QPainter p(&layers[activeLayerIndex].image);
    p.drawImage(canvas->getSelectionRect().topLeft(), selectionBuffer);
    compositeLayers();
    statusLabel->setText("Selection pasted");
}

void MainWindow::onLayerContextMenu(const QPoint &pos)
{
    // item sur lequel on a fait clic droit
    QListWidgetItem *item = layerListWidget->itemAt(pos);
    if (!item) return;

    // créer menu
    QMenu menu(this);
    QAction *dupAct = menu.addAction("Duplicate Layer");
    QAction *delAct = menu.addAction("Delete Layer");
    QAction *renameAct = menu.addAction("Rename Layer");
    QAction *opacityAct = menu.addAction("Change Opacity");

    QAction *selected = menu.exec(layerListWidget->mapToGlobal(pos));
    if (!selected) return;

    // stocker index de layer actif temporairement
    int uiIndex = layerListWidget->row(item);
    int layerIndex = layers.size() - 1 - uiIndex;
    activeLayerIndex = layerIndex;
    canvas->setTargetImage(&layers[activeLayerIndex].image);

    if (selected == dupAct) duplicateLayer();
    else if (selected == delAct) deleteLayer();
    else if (selected == renameAct) renameLayer();
    else if (selected == opacityAct) changeLayerOpacity();
}

void MainWindow::duplicateLayer()
{
    if (activeLayerIndex < 0 || activeLayerIndex >= layers.size()) return;
    Layer copy = layers[activeLayerIndex];
    copy.name += " Copy";
    layers.append(copy);

    // mettre en haut dans l'UI
    layerListWidget->insertItem(0, copy.name);
    layerListWidget->setCurrentRow(0);

    activeLayerIndex = layers.size() - 1;
    canvas->setTargetImage(&layers[activeLayerIndex].image);
    compositeLayers();
    statusLabel->setText("Layer duplicated: " + copy.name);
}

void MainWindow::deleteLayer()
{
    if (layers.size() <= 1) {
        QMessageBox::information(this, "Cannot remove", "Need at least one layer.");
        return;
    }

    int idx = activeLayerIndex;
    layers.removeAt(idx);

    // UI update
    int uiIndex = layerListWidget->count() - 1 - idx;
    delete layerListWidget->takeItem(uiIndex);

    activeLayerIndex = layers.size() - 1; // top layer
    layerListWidget->setCurrentRow(0);
    canvas->setTargetImage(&layers[activeLayerIndex].image);
    compositeLayers();
    statusLabel->setText("Layer removed, active: " + layers[activeLayerIndex].name);
}

void MainWindow::renameLayer()
{
    bool ok;
    QString newName = QInputDialog::getText(this, "Rename Layer",
                                            "New name:",
                                            QLineEdit::Normal,
                                            layers[activeLayerIndex].name,
                                            &ok);
    if (ok && !newName.isEmpty()) {
        layers[activeLayerIndex].name = newName;

        // update UI
        int uiIndex = layerListWidget->count() - 1 - activeLayerIndex;
        layerListWidget->item(uiIndex)->setText(newName);
        statusLabel->setText("Layer renamed: " + newName);
    }
}

void MainWindow::changeLayerOpacity()
{
    bool ok;
    double op = QInputDialog::getDouble(
        this, "Layer Opacity",
        "Opacity (0.0 - 1.0):",
        layers[activeLayerIndex].opacity,
        0.0, 1.0, 2, &ok);

    if (ok) {
        layers[activeLayerIndex].opacity = op;
        compositeLayers();
        statusLabel->setText(QString("Opacity: %1").arg(op));
    }
}

void Canvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (currentTool != TEXT || !targetImg) return;

    QPoint imgPt = widgetToImage(event->pos(), targetImg->size());
    for (TextItem &t : textItems) {
        QRect itemRect = t.boundingRect.translated(t.position);
        if (itemRect.contains(imgPt)) {

            // Dialog pour éditer texte
            QDialog dlg(this);
            dlg.setWindowTitle("Edit Text");
            QVBoxLayout *lay = new QVBoxLayout(&dlg);

            QLineEdit *line = new QLineEdit(t.text, &dlg);
            lay->addWidget(line);

            QPushButton *colorBtn = new QPushButton("Choose Color", &dlg);
            lay->addWidget(colorBtn);

            QFontComboBox *fontCombo = new QFontComboBox(&dlg);
            fontCombo->setCurrentFont(t.font);
            lay->addWidget(fontCombo);

            QSpinBox *sizeSpin = new QSpinBox(&dlg);
            sizeSpin->setRange(6, 200);
            sizeSpin->setValue(t.font.pointSize());
            lay->addWidget(sizeSpin);

            QHBoxLayout *btnLayout = new QHBoxLayout();
            QPushButton *okBtn = new QPushButton("OK", &dlg);
            QPushButton *cancelBtn = new QPushButton("Cancel", &dlg);
            btnLayout->addWidget(okBtn);
            btnLayout->addWidget(cancelBtn);
            lay->addLayout(btnLayout);

            QColor chosenColor = t.color;

            connect(colorBtn, &QPushButton::clicked, [&]() {
                QColor c = QColorDialog::getColor(chosenColor, this, "Pick Color");
                if (c.isValid()) chosenColor = c;
            });
            connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
            connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

            if (dlg.exec() == QDialog::Accepted) {
                t.text = line->text();
                t.color = chosenColor;
                t.font = QFont(fontCombo->currentFont().family(), sizeSpin->value());
                QFontMetrics fm(t.font);
                t.boundingRect = fm.boundingRect(t.text);
                update();
            }
            return;
        }
    }
}


void Canvas::commitTextItems()
{
    if (!targetImg || textItems.isEmpty()) return;

    QPainter p(targetImg);
    p.setRenderHint(QPainter::Antialiasing, true);

    for (const TextItem &t : textItems) {
        p.setFont(t.font);
        p.setPen(t.color);
        p.drawText(t.position, t.text);
    }

    textItems.clear();
    activeTextIndex = -1;
    emit strokeFinished();
    update();
}

void MainWindow::openPNGAsNewLayer()
{
    QString fileName = QFileDialog::getOpenFileName(this,
        "Open PNG as Layer",
        QString(),
        "PNG Images (*.png)");

    if (fileName.isEmpty()) return;

    QImage loaded;
    if (!loaded.load(fileName)) {
        QMessageBox::warning(this, "Open failed", "Could not open PNG image.");
        return;
    }

    // Créer un nouveau layer
    Layer l;
    l.name = QFileInfo(fileName).baseName(); // nom du fichier sans extension
    l.image = loaded.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    layers.append(l);

    // Ajouter en haut dans l'UI
    layerListWidget->insertItem(0, l.name);
    layerListWidget->setCurrentRow(0);

    // Activer le nouveau layer
    activeLayerIndex = layers.size() - 1;
    canvas->setTargetImage(&layers[activeLayerIndex].image);

    // Mettre à jour le composite
    compositeLayers();

    statusLabel->setText(fileName + " loaded into new layer: " + l.name);
}
