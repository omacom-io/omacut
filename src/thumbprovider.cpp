#include "thumbprovider.h"

#include <QMutexLocker>

void ThumbProvider::setImages(const QVector<QImage> &images) {
    QMutexLocker lock(&m_mutex);
    m_images = images;
}

void ThumbProvider::setImage(int index, const QImage &image) {
    if (index < 0)
        return;

    QMutexLocker lock(&m_mutex);
    if (index >= m_images.size())
        m_images.resize(index + 1);
    m_images[index] = image;
}

QImage ThumbProvider::requestImage(const QString &id, QSize *size, const QSize &requested) {
    // id looks like "<video-revision>/<index>"; only the index maps to storage.
    const int index = id.section('/', -1).toInt();

    QMutexLocker lock(&m_mutex);
    if (index < 0 || index >= m_images.size())
        return {};

    QImage img = m_images.at(index);
    if (img.isNull())
        return {};
    if (size)
        *size = img.size();
    QSize target = requested;
    if (target.width() <= 0 && target.height() > 0 && img.height() > 0)
        target.setWidth(qMax(1, qRound(double(img.width()) * target.height() / img.height())));
    if (target.height() <= 0 && target.width() > 0 && img.width() > 0)
        target.setHeight(qMax(1, qRound(double(img.height()) * target.width() / img.width())));
    if (target.width() > 0 && target.height() > 0)
        img = img.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    return img;
}
