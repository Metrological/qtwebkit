/*
 * Copyright (C) 2006 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2008 Holger Hans Peter Freyther
 * Copyright (C) 2009 Dirk Schulze <krit@webkit.org>
 * Copyright (C) 2010 Torch Mobile (Beijing) Co. Ltd. All rights reserved.
 * Copyright (C) 2014 Digia Plc. and/or its subsidiary(-ies)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "ImageBuffer.h"

#include "GraphicsContext.h"
#include "ImageData.h"
#include "MIMETypeRegistry.h"
#include "StillImageQt.h"
#include "TransparencyLayer.h"
#include <wtf/text/CString.h>
#include <wtf/text/WTFString.h>

#include <QBuffer>
#include <QImage>
#include <QImageWriter>
#include <QPainter>
#include <QPixmap>
#include <math.h>

#if ENABLE(ACCELERATED_2D_CANVAS)
#include <QOpenGLFramebufferObject>
#include <QOpenGLPaintDevice>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include "TextureMapper.h"
#include "TextureMapperPlatformLayer.h"
#include "TextureMapperGL.h"
#include <private/qopenglpaintengine_p.h>
#include "OpenGLShims.h"
#include "GLSharedContext.h"
#include "GraphicsSurface.h"
#endif

namespace WebCore {

struct ImageBufferDataPrivate {
    virtual ~ImageBufferDataPrivate() { }
    virtual QPaintDevice* paintDevice() = 0;
    virtual QImage toQImage() const = 0;
    virtual PassRefPtr<Image> copyImage(BackingStoreCopy copyBehavior) const = 0;
    virtual bool isAccelerated() const = 0;
    virtual PlatformLayer* platformLayer() = 0;
    virtual void draw(GraphicsContext* destContext, ColorSpace styleColorSpace, const FloatRect& destRect,
                      const FloatRect& srcRect, CompositeOperator op, BlendMode blendMode, bool useLowQualityScale,
                      bool ownContext) = 0;
    virtual void drawPattern(GraphicsContext* destContext, const FloatRect& srcRect, const AffineTransform& patternTransform,
                             const FloatPoint& phase, ColorSpace styleColorSpace, CompositeOperator op,
                             const FloatRect& destRect, bool ownContext) = 0;
    virtual void clip(GraphicsContext* context, const FloatRect& floatRect) const = 0;
    virtual void platformTransformColorSpace(const Vector<int>& lookUpTable) = 0;
};

#if ENABLE(ACCELERATED_2D_CANVAS)

/*************** accelerated implementation ****************/

class ImageBufferPaintDevice;
struct ImageBufferDataPrivateAccelerated : public TextureMapperPlatformLayer, public ImageBufferDataPrivate {
    ImageBufferDataPrivateAccelerated(const IntSize& size);
    QPaintDevice* paintDevice() { return m_pdev.get(); }
    QImage toQImage() const;
    PassRefPtr<Image> copyImage(BackingStoreCopy copyBehavior) const;
    bool isAccelerated() const { return true; }
    PlatformLayer* platformLayer() { return this; }
#if USE(GRAPHICS_SURFACE)
    virtual IntSize platformLayerSize() const;
    virtual GraphicsSurfaceToken graphicsSurfaceToken() const;
    virtual uint32_t copyToGraphicsSurface();
    virtual GraphicsSurface::Flags graphicsSurfaceFlags() const { return GraphicsSurface::SupportsAlpha | GraphicsSurface::SupportsTextureTarget | GraphicsSurface::SupportsSharing | GraphicsSurface::IsCanvas; }
#endif
    void commitChanges() const;
    void draw(GraphicsContext* destContext, ColorSpace styleColorSpace, const FloatRect& destRect,
              const FloatRect& srcRect, CompositeOperator op, BlendMode blendMode, bool useLowQualityScale,
              bool ownContext);
    void drawPattern(GraphicsContext* destContext, const FloatRect& srcRect, const AffineTransform& patternTransform,
                     const FloatPoint& phase, ColorSpace styleColorSpace, CompositeOperator op,
                     const FloatRect& destRect, bool ownContext);
    void clip(GraphicsContext* context, const FloatRect& floatRect) const;
    void platformTransformColorSpace(const Vector<int>& lookUpTable);
    void paintToTextureMapper(TextureMapper*, const FloatRect&, const TransformationMatrix& modelViewMatrix = TransformationMatrix(), float opacity = 1.0);

    mutable bool m_fboDirty;
    OwnPtr<QOpenGLFramebufferObject> m_fbo;
    OwnPtr<QOpenGLPaintDevice> m_pdev;
#if USE(GRAPHICS_SURFACE)
    mutable RefPtr<GraphicsSurface> m_graphicsSurface;
#endif
};

class ImageBufferPaintDevice : public QOpenGLPaintDevice
{
public:
    ImageBufferPaintDevice(ImageBufferDataPrivateAccelerated* impl)
    : QOpenGLPaintDevice(impl->m_fbo->size())
    , m_impl(impl) { }
    virtual ~ImageBufferPaintDevice() { }

    void ensureActiveTarget()
    {
        GLSharedContext::makeCurrent();
        m_impl->m_fbo->bind();
        m_impl->m_fboDirty = true;
    }
private:
    ImageBufferDataPrivateAccelerated* m_impl;
};

ImageBufferDataPrivateAccelerated::ImageBufferDataPrivateAccelerated(const IntSize& size)
    : m_fboDirty(true)
{
    GLSharedContext::makeCurrent();

    m_fbo = adoptPtr(new QOpenGLFramebufferObject(size, QOpenGLFramebufferObject::CombinedDepthStencil, GL_TEXTURE_2D, GL_RGBA));
    m_fbo->bind();
    m_pdev = adoptPtr(new ImageBufferPaintDevice(this));
}

QImage ImageBufferDataPrivateAccelerated::toQImage() const
{
    commitChanges();
    QImage image = m_fbo->toImage();
    return image;
}

PassRefPtr<Image> ImageBufferDataPrivateAccelerated::copyImage(BackingStoreCopy copyBehavior) const
{
    return StillImage::create(QPixmap::fromImage(toQImage()));
}

#if USE(GRAPHICS_SURFACE)
IntSize ImageBufferDataPrivateAccelerated::platformLayerSize() const
{
    return IntSize(m_fbo->size());
}

GraphicsSurfaceToken ImageBufferDataPrivateAccelerated::graphicsSurfaceToken() const
{
    if (!m_graphicsSurface) {
        m_graphicsSurface = GraphicsSurface::create(m_fbo->size(), graphicsSurfaceFlags(), QOpenGLContext::currentContext());
    }

    return m_graphicsSurface->exportToken();
}

uint32_t ImageBufferDataPrivateAccelerated::copyToGraphicsSurface()
{
    if (!m_graphicsSurface) {
        m_graphicsSurface = GraphicsSurface::create(m_fbo->size(), graphicsSurfaceFlags(), QOpenGLContext::currentContext());
    }

    commitChanges();
    m_graphicsSurface->copyFromTexture(m_fbo->texture(), IntRect(IntPoint(), m_fbo->size()));
    return m_graphicsSurface->frontBuffer();
}
#endif

void ImageBufferDataPrivateAccelerated::commitChanges() const
{
    if (!m_fboDirty)
        return;

    // this will flush pending QPainter operations and force ensureActiveTarget() to be called on the next paint
    QOpenGL2PaintEngineEx* acceleratedPaintEngine = static_cast<QOpenGL2PaintEngineEx*>(m_pdev->paintEngine());
    acceleratedPaintEngine->invalidateState();
    m_fboDirty = false;
}

void ImageBufferDataPrivateAccelerated::draw(GraphicsContext* destContext, ColorSpace styleColorSpace, const FloatRect& destRect,
                                             const FloatRect& srcRect, CompositeOperator op, BlendMode blendMode,
                                             bool useLowQualityScale, bool ownContext)
{
    if (destContext->isAcceleratedContext()) {
        commitChanges();

        QOpenGL2PaintEngineEx* acceleratedPaintEngine = static_cast<QOpenGL2PaintEngineEx*>(destContext->platformContext()->paintEngine());
        if (acceleratedPaintEngine) {
            FloatRect flippedSrc = srcRect;
            flippedSrc.setY(m_fbo->size().height() - flippedSrc.height() - flippedSrc.y());
            QPaintDevice* targetPaintDevice = acceleratedPaintEngine->paintDevice();

            if (m_pdev == targetPaintDevice) {
                // painting to itself, use an intermediate buffer
                GLSharedContext::makeCurrent();
                QRect rect(QPoint(), m_fbo->size());

                // create a temporal fbo and paint device
                QOpenGLFramebufferObject fbo(m_fbo->size(), QOpenGLFramebufferObject::NoAttachment, GL_TEXTURE_2D, GL_RGBA);
                fbo.bind();
                QOpenGLPaintDevice pdev(m_fbo->size());

                // create a painter for the device and draw the content of the current texture
                QPainter painter(&pdev);
                QOpenGL2PaintEngineEx* p = static_cast<QOpenGL2PaintEngineEx*>(painter.paintEngine());
                p->drawTexture(rect, m_fbo->texture(), m_fbo->size(), rect);
                painter.end();

                acceleratedPaintEngine->drawTexture(destRect, fbo.texture(), rect.size(), flippedSrc);
            } else {
                // paint to a different buffer
                acceleratedPaintEngine->drawTexture(destRect, m_fbo->texture(), m_fbo->size(), flippedSrc);
            }
        }
    } else {
        RefPtr<Image> image = StillImage::create(QPixmap::fromImage(toQImage()));
        destContext->drawImage(image.get(), styleColorSpace, destRect, srcRect, op, blendMode,
                               DoNotRespectImageOrientation, useLowQualityScale);
    }
}

void ImageBufferDataPrivateAccelerated::drawPattern(GraphicsContext* destContext, const FloatRect& srcRect, const AffineTransform& patternTransform,
                                                    const FloatPoint& phase, ColorSpace styleColorSpace, CompositeOperator op,
                                                    const FloatRect& destRect, bool ownContext)
{
    RefPtr<Image> image = StillImage::create(QPixmap::fromImage(toQImage()));
    if (destContext->isAcceleratedContext()) {
        // this causes the QOpenGLPaintDevice of the destContext to be bound so we can draw
        destContext->platformContext()->beginNativePainting();
        destContext->platformContext()->endNativePainting();
    }
    image->drawPattern(destContext, srcRect, patternTransform, phase, styleColorSpace, op, destRect);
}

void ImageBufferDataPrivateAccelerated::clip(GraphicsContext* context, const FloatRect& floatRect) const
{
    QPixmap alphaMask = QPixmap::fromImage(toQImage());
    IntRect rect = enclosingIntRect(floatRect);
    context->pushTransparencyLayerInternal(rect, 1.0, alphaMask);
}

void ImageBufferDataPrivateAccelerated::platformTransformColorSpace(const Vector<int>& lookUpTable)
{
    QPainter* painter = paintDevice()->paintEngine()->painter();

    QImage image = toQImage().convertToFormat(QImage::Format_ARGB32);
    ASSERT(!image.isNull());

    uchar* bits = image.bits();
    const int bytesPerLine = image.bytesPerLine();

    for (int y = 0; y < image.height(); ++y) {
        quint32* scanLine = reinterpret_cast_ptr<quint32*>(bits + y * bytesPerLine);
        for (int x = 0; x < image.width(); ++x) {
            QRgb& pixel = scanLine[x];
            pixel = qRgba(lookUpTable[qRed(pixel)],
                          lookUpTable[qGreen(pixel)],
                          lookUpTable[qBlue(pixel)],
                          qAlpha(pixel));
        }
    }

    painter->save();
    painter->resetTransform();
    painter->setOpacity(1.0);
    painter->setClipping(false);
    painter->setCompositionMode(QPainter::CompositionMode_Source);
    // Should coordinates be flipped?
    painter->drawImage(QPoint(0,0), image);
    painter->restore();
}

void ImageBufferDataPrivateAccelerated::paintToTextureMapper(TextureMapper* textureMapper, const FloatRect& targetRect, const TransformationMatrix& matrix, float opacity)
{
    if (textureMapper->accelerationMode() != TextureMapper::OpenGLMode) {
        return;
    }

    commitChanges();

    static_cast<TextureMapperGL*>(textureMapper)->drawTexture(m_fbo->texture(), TextureMapperGL::ShouldFlipTexture | TextureMapperGL::ShouldBlend, m_fbo->size(), targetRect, matrix, opacity);

}

#endif // ACCELERATED_2D_CANVAS

/*************** non accelerated implementation ****************/

struct ImageBufferDataPrivateUnaccelerated : public ImageBufferDataPrivate {
    ImageBufferDataPrivateUnaccelerated(const IntSize& size);
    QPaintDevice* paintDevice() { return m_pixmap.isNull() ? 0 : &m_pixmap; }
    QImage toQImage() const;
    PassRefPtr<Image> copyImage(BackingStoreCopy copyBehavior) const;
    virtual bool isAccelerated() const { return false; }
    PlatformLayer* platformLayer() { return 0; }
    void draw(GraphicsContext* destContext, ColorSpace styleColorSpace, const FloatRect& destRect,
              const FloatRect& srcRect, CompositeOperator op, BlendMode blendMode, bool useLowQualityScale,
              bool ownContext);
    void drawPattern(GraphicsContext* destContext, const FloatRect& srcRect, const AffineTransform& patternTransform,
                     const FloatPoint& phase, ColorSpace styleColorSpace, CompositeOperator op,
                     const FloatRect& destRect, bool ownContext);
    void clip(GraphicsContext* context, const FloatRect& floatRect) const;
    void platformTransformColorSpace(const Vector<int>& lookUpTable);

    QPixmap m_pixmap;
    RefPtr<Image> m_image;
};

ImageBufferDataPrivateUnaccelerated::ImageBufferDataPrivateUnaccelerated(const IntSize& size)
    : m_pixmap(size)
    , m_image(StillImage::createForRendering(&m_pixmap))
{
    m_pixmap.fill(QColor(Qt::transparent));
}

QImage ImageBufferDataPrivateUnaccelerated::toQImage() const
{
    QPaintEngine* paintEngine = m_pixmap.paintEngine();
    if (!paintEngine || paintEngine->type() != QPaintEngine::Raster)
        return m_pixmap.toImage();

    // QRasterPixmapData::toImage() will deep-copy the backing QImage if there's an active QPainter on it.
    // For performance reasons, we don't want that here, so we temporarily redirect the paint engine.
    QPaintDevice* currentPaintDevice = paintEngine->paintDevice();
    paintEngine->setPaintDevice(0);
    QImage image = m_pixmap.toImage();
    paintEngine->setPaintDevice(currentPaintDevice);
    return image;
}

PassRefPtr<Image> ImageBufferDataPrivateUnaccelerated::copyImage(BackingStoreCopy copyBehavior) const
{
    if (copyBehavior == CopyBackingStore)
        return StillImage::create(m_pixmap);

    return StillImage::createForRendering(&m_pixmap);
}

void ImageBufferDataPrivateUnaccelerated::draw(GraphicsContext* destContext, ColorSpace styleColorSpace, const FloatRect& destRect,
                                               const FloatRect& srcRect, CompositeOperator op, BlendMode blendMode,
                                               bool useLowQualityScale, bool ownContext)
{
    if (ownContext) {
        // We're drawing into our own buffer.  In order for this to work, we need to copy the source buffer first.
        RefPtr<Image> copy = copyImage(CopyBackingStore);
        destContext->drawImage(copy.get(), ColorSpaceDeviceRGB, destRect, srcRect, op, blendMode, DoNotRespectImageOrientation, useLowQualityScale);
    } else
        destContext->drawImage(m_image.get(), styleColorSpace, destRect, srcRect, op, blendMode, DoNotRespectImageOrientation, useLowQualityScale);
}

void ImageBufferDataPrivateUnaccelerated::drawPattern(GraphicsContext* destContext, const FloatRect& srcRect, const AffineTransform& patternTransform,
                                                      const FloatPoint& phase, ColorSpace styleColorSpace, CompositeOperator op,
                                                      const FloatRect& destRect, bool ownContext)
{
    if (ownContext) {
        // We're drawing into our own buffer.  In order for this to work, we need to copy the source buffer first.
        RefPtr<Image> copy = copyImage(CopyBackingStore);
        copy->drawPattern(destContext, srcRect, patternTransform, phase, styleColorSpace, op, destRect);
    } else
        m_image->drawPattern(destContext, srcRect, patternTransform, phase, styleColorSpace, op, destRect);
}

void ImageBufferDataPrivateUnaccelerated::clip(GraphicsContext* context, const FloatRect& floatRect) const
{
    QPixmap* nativeImage = m_image->nativeImageForCurrentFrame();

    if (!nativeImage)
        return;

    IntRect rect = enclosingIntRect(floatRect);
    QPixmap alphaMask = *nativeImage;

    context->pushTransparencyLayerInternal(rect, 1.0, alphaMask);
}

void ImageBufferDataPrivateUnaccelerated::platformTransformColorSpace(const Vector<int>& lookUpTable)
{
    QPainter* painter = paintDevice()->paintEngine()->painter();

    bool isPainting = painter->isActive();
    if (isPainting)
        painter->end();

    QImage image = toQImage().convertToFormat(QImage::Format_ARGB32);
    ASSERT(!image.isNull());

    uchar* bits = image.bits();
    const int bytesPerLine = image.bytesPerLine();

    for (int y = 0; y < m_pixmap.height(); ++y) {
        quint32* scanLine = reinterpret_cast_ptr<quint32*>(bits + y * bytesPerLine);
        for (int x = 0; x < m_pixmap.width(); ++x) {
            QRgb& pixel = scanLine[x];
            pixel = qRgba(lookUpTable[qRed(pixel)],
                          lookUpTable[qGreen(pixel)],
                          lookUpTable[qBlue(pixel)],
                          qAlpha(pixel));
        }
    }

    m_pixmap = QPixmap::fromImage(image);

    if (isPainting)
        painter->begin(&m_pixmap);
}

// ********************************************************
ImageBufferData::ImageBufferData(const IntSize& size, bool accelerated)
{
    QPainter* painter = new QPainter;
    m_painter = adoptPtr(painter);

#if ENABLE(ACCELERATED_2D_CANVAS)
    if (accelerated) {
        m_impl = adoptPtr(new ImageBufferDataPrivateAccelerated(size));
    } else
#endif
        m_impl = adoptPtr(new ImageBufferDataPrivateUnaccelerated(size));

    if (!m_impl->paintDevice())
        return;
    if (!painter->begin(m_impl->paintDevice()))
        return;

    painter->setRenderHints(QPainter::Antialiasing | QPainter::HighQualityAntialiasing);
    QPen pen = painter->pen();
    pen.setColor(Qt::black);
    pen.setWidth(1);
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::SvgMiterJoin);
    pen.setMiterLimit(10);
    painter->setPen(pen);
    QBrush brush = painter->brush();
    brush.setColor(Qt::black);
    painter->setBrush(brush);
    painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
}

ImageBuffer::ImageBuffer(const IntSize& size, float /* resolutionScale */, ColorSpace, RenderingMode renderingMode, bool& success)
    : m_data(size, renderingMode == Accelerated)
    , m_size(size)
    , m_logicalSize(size)
{
    success = m_data.m_painter && m_data.m_painter->isActive();
    if (!success)
        return;

    m_context = adoptPtr(new GraphicsContext(m_data.m_painter.get()));
}

ImageBuffer::~ImageBuffer()
{
#if ENABLE(ACCELERATED_2D_CANVAS)
    QOpenGLContext *previousContext = QOpenGLContext::currentContext();
    GLSharedContext::makeCurrent();
#endif
    m_data.m_painter->end();
#if ENABLE(ACCELERATED_2D_CANVAS)
    if (previousContext)
        previousContext->makeCurrent(previousContext->surface());
#endif
}

GraphicsContext* ImageBuffer::context() const
{
    ASSERT(m_data.m_painter->isActive());

    return m_context.get();
}

PassRefPtr<Image> ImageBuffer::copyImage(BackingStoreCopy copyBehavior, ScaleBehavior) const
{
    return m_data.m_impl->copyImage(copyBehavior);
}

BackingStoreCopy ImageBuffer::fastCopyImageMode()
{
    return DontCopyBackingStore;
}

void ImageBuffer::draw(GraphicsContext* destContext, ColorSpace styleColorSpace, const FloatRect& destRect, const FloatRect& srcRect,
                       CompositeOperator op, BlendMode blendMode, bool useLowQualityScale)
{
    m_data.m_impl->draw(destContext, styleColorSpace, destRect, srcRect, op, blendMode, useLowQualityScale, destContext == context());
}

void ImageBuffer::drawPattern(GraphicsContext* destContext, const FloatRect& srcRect, const AffineTransform& patternTransform,
                              const FloatPoint& phase, ColorSpace styleColorSpace, CompositeOperator op, const FloatRect& destRect)
{
    m_data.m_impl->drawPattern(destContext, srcRect, patternTransform, phase, styleColorSpace, op, destRect, destContext == context());
}

void ImageBuffer::clip(GraphicsContext* context, const FloatRect& floatRect) const
{
    m_data.m_impl->clip(context, floatRect);
}

void ImageBuffer::platformTransformColorSpace(const Vector<int>& lookUpTable)
{
    m_data.m_impl->platformTransformColorSpace(lookUpTable);
}

template <Multiply multiplied>
PassRefPtr<Uint8ClampedArray> getImageData(const IntRect& rect, const ImageBufferData& imageData, const IntSize& size)
{
    float area = 4.0f * rect.width() * rect.height();
    if (area > static_cast<float>(std::numeric_limits<int>::max()))
        return 0;

    RefPtr<Uint8ClampedArray> result = Uint8ClampedArray::createUninitialized(rect.width() * rect.height() * 4);

    QImage::Format format = (multiplied == Unmultiplied) ? QImage::Format_RGBA8888 : QImage::Format_RGBA8888_Premultiplied;
    QImage image(result->data(), rect.width(), rect.height(), format);
    if (rect.x() < 0 || rect.y() < 0 || rect.maxX() > size.width() || rect.maxY() > size.height())
        image.fill(0);

    // Let drawImage deal with the conversion.
    QPainter p(&image);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(QPoint(0,0), imageData.m_impl->toQImage(), rect);

    return result.release();
}

PassRefPtr<Uint8ClampedArray> ImageBuffer::getUnmultipliedImageData(const IntRect& rect, CoordinateSystem) const
{
    return getImageData<Unmultiplied>(rect, m_data, m_size);
}

PassRefPtr<Uint8ClampedArray> ImageBuffer::getPremultipliedImageData(const IntRect& rect, CoordinateSystem) const
{
    return getImageData<Premultiplied>(rect, m_data, m_size);
}

void ImageBuffer::putByteArray(Multiply multiplied, Uint8ClampedArray* source, const IntSize& sourceSize, const IntRect& sourceRect, const IntPoint& destPoint, CoordinateSystem)
{
    ASSERT(sourceRect.width() > 0);
    ASSERT(sourceRect.height() > 0);

    bool isPainting = m_data.m_painter->isActive();
    if (!isPainting)
        m_data.m_painter->begin(m_data.m_impl->paintDevice());
    else {
        m_data.m_painter->save();

        // putImageData() should be unaffected by painter state
        m_data.m_painter->resetTransform();
        m_data.m_painter->setOpacity(1.0);
        m_data.m_painter->setClipping(false);
    }

    // Let drawImage deal with the conversion.
    QImage::Format format = (multiplied == Unmultiplied) ? QImage::Format_RGBA8888 : QImage::Format_RGBA8888_Premultiplied;
    QImage image(source->data(), sourceSize.width(), sourceSize.height(), format);

    m_data.m_painter->setCompositionMode(QPainter::CompositionMode_Source);
    m_data.m_painter->drawImage(destPoint + sourceRect.location(), image, sourceRect);

    if (!isPainting)
        m_data.m_painter->end();
    else
        m_data.m_painter->restore();
}

static bool encodeImage(const QPixmap& pixmap, const String& format, const double* quality, QByteArray& data)
{
    int compressionQuality = 100;
    if (quality && *quality >= 0.0 && *quality <= 1.0)
        compressionQuality = static_cast<int>(*quality * 100 + 0.5);

    QBuffer buffer(&data);
    buffer.open(QBuffer::WriteOnly);
    bool success = pixmap.save(&buffer, format.utf8().data(), compressionQuality);
    buffer.close();

    return success;
}

String ImageBuffer::toDataURL(const String& mimeType, const double* quality, CoordinateSystem) const
{
    ASSERT(MIMETypeRegistry::isSupportedImageMIMETypeForEncoding(mimeType));

    // QImageWriter does not support mimetypes. It does support Qt image formats (png,
    // gif, jpeg..., xpm) so skip the image/ to get the Qt image format used to encode
    // the m_pixmap image.

    RefPtr<Image> image = copyImage(DontCopyBackingStore);
    QByteArray data;
    if (!encodeImage(*image->nativeImageForCurrentFrame(), mimeType.substring(sizeof "image"), quality, data))
        return "data:,";

    return "data:" + mimeType + ";base64," + data.toBase64().data();
}

PlatformLayer* ImageBuffer::platformLayer() const
{
    return m_data.m_impl->platformLayer();
}

}
