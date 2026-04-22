#include "v4l2camera.h"

#ifdef __linux__
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#endif

#define NUM_BUFFERS 4

V4L2Camera::V4L2Camera()
    : m_fd(-1), m_buffers(nullptr), m_bufferCount(0),
      m_width(640), m_height(480), m_pixfmt(0), m_streaming(false)
{
}

V4L2Camera::~V4L2Camera()
{
    close();
}

bool V4L2Camera::isOpen() const { return m_fd >= 0; }
bool V4L2Camera::isStreaming() const { return m_streaming; }

bool V4L2Camera::open(const char *device)
{
#ifdef __linux__
    if (m_fd >= 0) close();

    m_fd = ::open(device, O_RDWR);
    if (m_fd < 0) {
        perror("V4L2Camera open");
        return false;
    }

    /* 디바이스 캡처/스트리밍 지원 확인 */
    struct v4l2_capability cap;
    if (ioctl(m_fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("VIDIOC_QUERYCAP");
        close();
        return false;
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
        !(cap.capabilities & V4L2_CAP_STREAMING)) {
        fprintf(stderr, "V4L2Camera: device doesn't support capture/streaming\n");
        close();
        return false;
    }

    /* 포맷 설정: MJPEG 우선, 실패 시 YUYV */
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width  = m_width;
    fmt.fmt.pix.height = m_height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(m_fd, VIDIOC_S_FMT, &fmt) < 0 ||
        fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG) {
        /* MJPEG 실패 → YUYV 시도 */
        memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width  = m_width;
        fmt.fmt.pix.height = m_height;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;

        if (ioctl(m_fd, VIDIOC_S_FMT, &fmt) < 0) {
            perror("VIDIOC_S_FMT");
            close();
            return false;
        }
    }

    m_pixfmt = fmt.fmt.pix.pixelformat;
    m_width  = fmt.fmt.pix.width;
    m_height = fmt.fmt.pix.height;

    /* 버퍼 요청 (mmap) */
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count  = NUM_BUFFERS;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(m_fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("VIDIOC_REQBUFS");
        close();
        return false;
    }

    m_bufferCount = (int)req.count;
    m_buffers = new Buffer[m_bufferCount];

    for (int i = 0; i < m_bufferCount; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = (unsigned)i;

        if (ioctl(m_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            close();
            return false;
        }

        m_buffers[i].length = buf.length;
        m_buffers[i].start  = mmap(NULL, buf.length,
                                   PROT_READ | PROT_WRITE,
                                   MAP_SHARED, m_fd, buf.m.offset);
        if (m_buffers[i].start == MAP_FAILED) {
            perror("mmap");
            m_buffers[i].start = nullptr;
            close();
            return false;
        }
    }

    return true;
#else
    (void)device;
    return false;
#endif
}

void V4L2Camera::close()
{
#ifdef __linux__
    stopStreaming();

    if (m_buffers) {
        for (int i = 0; i < m_bufferCount; i++) {
            if (m_buffers[i].start && m_buffers[i].start != MAP_FAILED)
                munmap(m_buffers[i].start, m_buffers[i].length);
        }
        delete[] m_buffers;
        m_buffers = nullptr;
        m_bufferCount = 0;
    }

    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
#endif
}

bool V4L2Camera::startStreaming()
{
#ifdef __linux__
    if (m_fd < 0 || m_streaming) return false;

    for (int i = 0; i < m_bufferCount; i++) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = (unsigned)i;

        if (ioctl(m_fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            return false;
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(m_fd, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        return false;
    }

    m_streaming = true;
    return true;
#else
    return false;
#endif
}

void V4L2Camera::stopStreaming()
{
#ifdef __linux__
    if (!m_streaming || m_fd < 0) return;

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(m_fd, VIDIOC_STREAMOFF, &type);
    m_streaming = false;
#endif
}

QImage V4L2Camera::captureFrame()
{
#ifdef __linux__
    if (!m_streaming) return m_lastFrame;

    /* select로 비차단 확인 (최대 30ms) */
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(m_fd, &fds);
    struct timeval tv = { 0, 30000 };

    int r = select(m_fd + 1, &fds, NULL, NULL, &tv);
    if (r <= 0) return m_lastFrame;

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(m_fd, VIDIOC_DQBUF, &buf) < 0)
        return m_lastFrame;

    QImage frame = decodeFrame(m_buffers[buf.index].start, (int)buf.bytesused);
    if (!frame.isNull())
        m_lastFrame = frame;

    /* 버퍼 재큐잉 */
    ioctl(m_fd, VIDIOC_QBUF, &buf);

    return m_lastFrame;
#else
    return QImage();
#endif
}

QImage V4L2Camera::decodeFrame(const void *data, int size)
{
    if (!data || size <= 0) return QImage();

#ifdef __linux__
    if (m_pixfmt == V4L2_PIX_FMT_MJPEG) {
        QImage img;
        img.loadFromData((const uchar *)data, size, "JPEG");
        return img;
    } else if (m_pixfmt == V4L2_PIX_FMT_YUYV) {
        return convertYUYV((const uint8_t *)data, m_width, m_height);
    }
#else
    (void)data; (void)size;
#endif
    return QImage();
}

QImage V4L2Camera::convertYUYV(const uint8_t *data, int width, int height)
{
    QImage img(width, height, QImage::Format_RGB888);

    for (int y = 0; y < height; y++) {
        uint8_t *dst = img.scanLine(y);
        const uint8_t *src = data + y * width * 2;

        for (int x = 0; x < width; x += 2) {
            int y0 = src[0], u = src[1], y1 = src[2], v = src[3];
            src += 4;

            int c0 = y0 - 16, c1 = y1 - 16;
            int d = u - 128,  e = v - 128;

            auto clamp = [](int val) -> uint8_t {
                return (uint8_t)(val < 0 ? 0 : (val > 255 ? 255 : val));
            };

            *dst++ = clamp((298 * c0 + 409 * e + 128) >> 8);
            *dst++ = clamp((298 * c0 - 100 * d - 208 * e + 128) >> 8);
            *dst++ = clamp((298 * c0 + 516 * d + 128) >> 8);

            *dst++ = clamp((298 * c1 + 409 * e + 128) >> 8);
            *dst++ = clamp((298 * c1 - 100 * d - 208 * e + 128) >> 8);
            *dst++ = clamp((298 * c1 + 516 * d + 128) >> 8);
        }
    }

    return img;
}

bool V4L2Camera::saveFrame(const QString &path, int quality)
{
    if (m_lastFrame.isNull())
        captureFrame();
    if (m_lastFrame.isNull())
        return false;
    return m_lastFrame.save(path, "JPEG", quality);
}
