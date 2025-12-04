#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QWidget>
#include <QImage>
#include <QColor>
#include <QPoint>
#include <QLabel>
#include <QList>
#include <QVector>
#include <QListWidget>
#include <QSlider>

#include <QStack>

// Small Layer struct: name + image + undo/redo stacks
struct Layer {
    QString name;
    QImage image;
    QVector<QImage> undoStack;
    QVector<QImage> redoStack;
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

    // zoom
    void setZoom(double z);

    // tools
    enum Tool { BRUSH, ERASER, LINE, RECTANGLE, CIRCLE };
    void setTool(Tool t);

signals:
    void strokeStarted(); // emitted on mouse press (before modifying)
    void strokeFinished(); // emitted on mouse release (after modifying)

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QImage composite;  
    QPoint imageOffset;  // composited image for display
    QImage *targetImg;  // pointer to active layer image (may be nullptr)
    QPoint lastPoint;   // widget coords
    QPoint startPoint;  // for shapes
    int penWidth;
    QColor penColor;
    bool eraserMode;

    double zoom;

    Tool currentTool;

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

private:
    // UI helpers
    void setupMenu();
    void setupToolbarAndPalette();
    void setupLayerPanel();

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
};

#endif // MAINWINDOW_H
