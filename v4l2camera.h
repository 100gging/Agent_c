#ifndef V4L2CAMERA_H
#define V4L2CAMERA_H

#include <QImage>
#include <QString>
#include <cstdint>

class V4L2Camera
{
public:
    V4L2Camera();
    ~V4L2Camera();

    bool open(const char *device = "/dev/video0");
    void close();
    bool isOpen() const;

    bool startStreaming();
    void stopStreaming();
    bool isStreaming() const;

    /** 비차단: 새 프레임이 있으면 반환, 없으면 마지막 프레임 반환 */
    QImage captureFrame();

    /** 마지막 캡처 프레임을 JPEG으로 저장 */
    bool saveFrame(const QString &path, int quality = 95);

private:
    struct Buffer {
        void  *start;
        size_t length;
    };

    int       m_fd;
    Buffer   *m_buffers;
    int       m_bufferCount;
    int       m_width;
    int       m_height;
    uint32_t  m_pixfmt;
    bool      m_streaming;
    QImage    m_lastFrame;

    QImage decodeFrame(const void *data, int size);
    static QImage convertYUYV(const uint8_t *data, int width, int height);
};

#endif // V4L2CAMERA_H
