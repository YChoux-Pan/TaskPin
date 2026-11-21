#pragma once

#include "translation/TranslationTypes.h"
#include <QWidget>

class TranslationCanvas : public QWidget {
    Q_OBJECT
public:
    explicit TranslationCanvas(QWidget *parent = nullptr);
    // Block rectangles use physical pixels, independently of the input image's DPR.
    void setContent(const QImage &image, const QList<Translation::Block> &blocks,
                    const QList<Translation::Result> &results);
    // Returns the same cached pixels currently displayed, before zooming.
    QImage renderedImage() const;
    void setOriginal(bool original);
    bool original() const;
    void setZoom(double zoom);
    double zoom() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_originalImage;
    QImage m_translatedImage;
    bool m_original = false;
    double m_zoom = 1;
    void updateCanvasSize();
};
