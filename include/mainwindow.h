#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QWidget>
#include <QShortcut>
#include <QImage>
#include <QColor>
#include <QPoint>
#include <QLabel>
#include <QPainter>  
#include <QList>
#include <QVector>
#include <QListWidget>
#include <QSlider>
#include <QStack>


struct TextItem {
    QString text;
    QPoint position;      // en coordonnées image
    QFont font;
    QColor color;
    QRect boundingRect;   // pour sélection / déplacement
    bool selected = false;
};

struct Layer {
    QString name;
    QImage image;
    QVector<QImage> undoStack;
    QVector<QImage> redoStack;
    double opacity = 1.0;
};

class Canvas : public QWidget {
    Q_OBJECT
public:
    explicit Canvas(QWidget *parent = nullptr);

    // set the composited image to display (read-only)
    void setCompositeImage(const QImage &composite);

    // set pointer to the active layer image (Canvas will draw into this image)
    void setTargetImage(QImage *target);

    // image access
    QImage getDisplayedImage() const;

    // pen / eraser
    void setPenColor(const QColor &c);
    void setPenWidth(int w);
    void setEraserMode(bool on);

    // mainwindow.h, dans Canvas
    bool isRectSelection() const { return currentTool == RECT_SELECT && _hasSelection; }
    bool isLassoSelection() const { return currentTool == LASSO_SELECT && _hasSelection; }
    QPolygon getLassoPolygon() const { return lassoPolygon; }


    // zoom
    void setZoom(double z);

    // tools
    enum Tool { BRUSH, ERASER, LINE, RECTANGLE, CIRCLE, RECT_SELECT, LASSO_SELECT, TEXT };
    void setTool(Tool t);
    bool hasSelection() const { return _hasSelection; }
    QRect getSelectionRect() const { return selectionRect; }
    QImage getSelectionImage() const
    {
        if (!hasSelection() || !targetImg) return QImage();

        if (currentTool == RECT_SELECT) {
            return targetImg->copy(selectionRect);
        } 
        else if (currentTool == LASSO_SELECT) {
            if (lassoPolygon.isEmpty()) return QImage();
            QImage img(selectionRect.size(), QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::transparent);

            QPainter painter(&img);
            QPolygon poly;
            for (const QPoint &p : lassoPolygon)
                poly << (p - selectionRect.topLeft());  // position relative à selectionRect
            painter.setClipRegion(QRegion(poly));
            painter.drawImage(-selectionRect.topLeft(), *targetImg);

            return img;
        }

        return QImage();
    }

    void commitTextItems();

signals:
    void strokeStarted(); // emitted on mouse press (before modifying)
    void strokeFinished(); // emitted on mouse release (after modifying)

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override; 

private:
    QRect selectionRect;
    QPolygon lassoPolygon;   // pour lasso
    bool selecting = false;
    QImage composite;  
    QPoint imageOffset;  // composited image for display
    QImage *targetImg;  // pointer to active layer image (may be nullptr)
    QPoint lastPoint;   // widget coords
    QPoint startPoint;  // for shapes
    int penWidth;
    QColor penColor;
    bool eraserMode;

    QVector<TextItem> textItems;
    int activeTextIndex = -1;


    double zoom;

    Tool currentTool;
    bool _hasSelection = false;

    void ensureTargetSizeMatchesWidget();
    QPoint widgetToImage(const QPoint &p, const QSize &imgSize);
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    // file actions
    void openFile();
    void saveFile();
    void clearCanvas();

    // layer actions
    void addLayer();
    void removeLayer();
    void activateLayer(int index);

    // undo/redo
    void undo();
    void redo();

    // tools and palette
    void selectBrush();
    void selectEraser();
    void setColorFromButton();
    void chooseColor();
    void changeBrushSize(int v);

    // canvas events
    void onStrokeStarted();
    void onStrokeFinished();

    // Day 5 transforms
    void zoomIn();
    void zoomOut();
    void rotateLeft();
    void rotateRight();
    void flipHorizontal();
    void flipVertical();

    // Day 8 filters
    void grayscale();
    void invertColors();

    void pasteSelection();
    void copySelection();
    void cutSelection();

    void onLayerContextMenu(const QPoint &pos);
    void duplicateLayer();
    void deleteLayer();
    void renameLayer();
    void changeLayerOpacity();


private:
    // UI helpers
    void setupMenu();
    void setupToolbarAndPalette();
    void setupLayerPanel();
    void openPNGAsNewLayer();

    // layers & compositing
    void compositeLayers();              // recompute composite (paint layers bottom->top)
    void pushUndoForActiveLayer();       // push snapshot into undo stack (called at stroke start)
    void clearRedoForActiveLayer();

    Canvas *canvas;
    QLabel *statusLabel;
    QListWidget *layerListWidget;
    QPoint imageOffset;

    QVector<Layer> layers;
    int activeLayerIndex;

    // current tool state
    int brushSize;
    QColor brushColor;

    // zoom factor (MainWindow level)
    double zoomFactor;

    QImage selectionBuffer;      // buffer temporaire pour copier/couper
    QRect selectionRect;         // zone sélectionnée
    QPolygon lassoPolygon;       // pour lasso

    
};

#endif // MAINWINDOW_H
