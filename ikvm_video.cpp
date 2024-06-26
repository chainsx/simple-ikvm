#include "ikvm_video.hpp"

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>

namespace ikvm
{

const int Video::bitsPerSample(8);
const int Video::bytesPerPixel(4);
const int Video::samplesPerPixel(3);

Video::Video(const std::string& p, Input& input, int fr, int sub) :
    resizeAfterOpen(false), timingsError(false), fd(-1), frameRate(fr),
    lastFrameIndex(-1), height(600), width(800), subSampling(sub), input(input),
    path(p), pixelformat(V4L2_PIX_FMT_YUYV)
{}

Video::~Video()
{
    stop();
}

char* Video::getData()
{
    if (lastFrameIndex >= 0)
    {
        return (char*)buffers[lastFrameIndex].data;
    }

    return nullptr;
}

void Video::getFrame()
{
    int rc(0);
    int fd_flags;
    v4l2_buffer buf;
    fd_set fds;
    timeval tv;

    if (fd < 0)
    {
        return;
    }

    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    tv.tv_sec = 1;
    tv.tv_usec = 0;

    memset(&buf, 0, sizeof(v4l2_buffer));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    // Switch to non-blocking in order to safely dequeue all buffers; if the
    // video signal is lost while blocking to dequeue, the video driver may
    // wait forever if signal is not re-acquired
    fd_flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, fd_flags | O_NONBLOCK);

    rc = select(fd + 1, &fds, NULL, NULL, &tv);
    if (rc > 0)
    {
        do
        {
            rc = ioctl(fd, VIDIOC_DQBUF, &buf);
            if (rc >= 0)
            {
                buffers[buf.index].queued = false;

                if (!(buf.flags & V4L2_BUF_FLAG_ERROR))
                {
                    lastFrameIndex = buf.index;
                    buffers[lastFrameIndex].payload = buf.bytesused;
                    break;
                }
                else
                {
                    buffers[buf.index].payload = 0;
                }
            }
        } while (rc >= 0);
    }

    fcntl(fd, F_SETFL, fd_flags);

    for (unsigned int i = 0; i < buffers.size(); ++i)
    {
        if (i == (unsigned int)lastFrameIndex)
        {
            continue;
        }

        if (!buffers[i].queued)
        {
            memset(&buf, 0, sizeof(v4l2_buffer));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;

            rc = ioctl(fd, VIDIOC_QBUF, &buf);
            if (rc)
            {
                std::cout<<"Failed to queue buffer"<<strerror(errno)<<std::endl;
            }
            else
            {
                buffers[i].queued = true;
            }
        }
    }
}

bool Video::needsResize()
{
    int rc;
    v4l2_dv_timings timings;

    if (fd < 0)
    {
        return false;
    }

    if (resizeAfterOpen)
    {
        return true;
    }

    memset(&timings, 0, sizeof(v4l2_dv_timings));
    rc = ioctl(fd, VIDIOC_QUERY_DV_TIMINGS, &timings);
    if (rc < 0)
    {
        if (!timingsError)
        {
            std::cout<<"Failed to query timings"<<strerror(errno)<<std::endl;
            timingsError = true;
        }

        restart();
        return false;
    }
    else
    {
        timingsError = false;
    }

    if (timings.bt.width != width || timings.bt.height != height)
    {
        width = timings.bt.width;
        height = timings.bt.height;

        if (!width || !height)
        {
            std::cout<<"Failed to get new resolution"<<width<<height<<std::endl;
        }

        lastFrameIndex = -1;
        return true;
    }

    return false;
}

void Video::resize()
{
    int rc;
    unsigned int i;
    bool needsResizeCall(false);
    v4l2_buf_type type(V4L2_BUF_TYPE_VIDEO_CAPTURE);
    v4l2_requestbuffers req;

    if (fd < 0)
    {
        return;
    }

    if (resizeAfterOpen)
    {
        resizeAfterOpen = false;
        return;
    }

    for (i = 0; i < buffers.size(); ++i)
    {
        if (buffers[i].data)
        {
            needsResizeCall = true;
            break;
        }
    }

    if (needsResizeCall)
    {
        rc = ioctl(fd, VIDIOC_STREAMOFF, &type);
        if (rc)
        {
            std::cout<<"Failed to stop streaming"<<strerror(errno)<<std::endl;
        }
    }

    for (i = 0; i < buffers.size(); ++i)
    {
        if (buffers[i].data)
        {
            munmap(buffers[i].data, buffers[i].size);
            buffers[i].data = nullptr;
            buffers[i].queued = false;
        }
    }

    if (needsResizeCall)
    {
        v4l2_dv_timings timings;

        memset(&req, 0, sizeof(v4l2_requestbuffers));
        req.count = 0;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        rc = ioctl(fd, VIDIOC_REQBUFS, &req);
        if (rc < 0)
        {
            std::cout<<"Failed to zero streaming buffers"<<strerror(errno)<<std::endl;
        }

        memset(&timings, 0, sizeof(v4l2_dv_timings));
        rc = ioctl(fd, VIDIOC_QUERY_DV_TIMINGS, &timings);
        if (rc < 0)
        {
            std::cout<<"Failed to query timings, restart"<<strerror(errno)<<std::endl;
            restart();
            return;
        }

        rc = ioctl(fd, VIDIOC_S_DV_TIMINGS, &timings);
        if (rc < 0)
        {
            std::cout<<"Failed to set timings"<<strerror(errno)<<std::endl;
        }

        buffers.clear();
    }

    memset(&req, 0, sizeof(v4l2_requestbuffers));
    req.count = 3;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    rc = ioctl(fd, VIDIOC_REQBUFS, &req);
    if (rc < 0 || req.count < 2)
    {
        std::cout<<"Failed to request streaming buffers"<<strerror(errno)<<std::endl;
    }

    buffers.resize(req.count);

    for (i = 0; i < buffers.size(); ++i)
    {
        v4l2_buffer buf;

        memset(&buf, 0, sizeof(v4l2_buffer));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        rc = ioctl(fd, VIDIOC_QUERYBUF, &buf);
        if (rc < 0)
        {
            std::cout<<"Failed to query buffer"<<strerror(errno)<<std::endl;
        }

        buffers[i].data = mmap(NULL, buf.length, PROT_READ | PROT_WRITE,
                               MAP_SHARED, fd, buf.m.offset);
        if (buffers[i].data == MAP_FAILED)
        {
            std::cout<<"Failed to mmap buffer"<<strerror(errno)<<std::endl;
        }

        buffers[i].size = buf.length;

        rc = ioctl(fd, VIDIOC_QBUF, &buf);
        if (rc < 0)
        {
            std::cout<<"Failed to queue buffer"<<strerror(errno)<<std::endl;
        }

        buffers[i].queued = true;
    }

    rc = ioctl(fd, VIDIOC_STREAMON, &type);
    if (rc)
    {
        std::cout<<"Failed to start streaming"<<strerror(errno)<<std::endl;
    }
}

void Video::start()
{
    int rc;
    size_t oldHeight = height;
    size_t oldWidth = width;
    v4l2_capability cap;
    v4l2_format fmt;
    v4l2_streamparm sparm;
    v4l2_control ctrl;

    if (fd >= 0)
    {
        return;
    }

    input.sendWakeupPacket();

    fd = open(path.c_str(), O_RDWR);
    if (fd < 0)
    {
        std::cout<<"Failed to open video device"<<path.c_str()<<strerror(errno)<<std::endl;
    }

    memset(&cap, 0, sizeof(v4l2_capability));
    rc = ioctl(fd, VIDIOC_QUERYCAP, &cap);
    if (rc < 0)
    {
        std::cout<<"Failed to query video device capabilities"<<strerror(errno)<<std::endl;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
        !(cap.capabilities & V4L2_CAP_STREAMING))
    {
        std::cout<<"Video device doesn't support this application"<<std::endl;
    }

    memset(&fmt, 0, sizeof(v4l2_format));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    rc = ioctl(fd, VIDIOC_G_FMT, &fmt);
    if (rc < 0)
    {
        std::cout<<"Failed to query video device format"<<strerror(errno)<<std::endl;
    }

    memset(&sparm, 0, sizeof(v4l2_streamparm));
    sparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    sparm.parm.capture.timeperframe.numerator = 1;
    sparm.parm.capture.timeperframe.denominator = frameRate;
    rc = ioctl(fd, VIDIOC_S_PARM, &sparm);
    if (rc < 0)
    {
        std::cout<<"Failed to set video device frame rate"<<strerror(errno)<<std::endl;
    }

    ctrl.id = V4L2_CID_JPEG_CHROMA_SUBSAMPLING;
    ctrl.value = subSampling ? V4L2_JPEG_CHROMA_SUBSAMPLING_420
                             : V4L2_JPEG_CHROMA_SUBSAMPLING_444;
    rc = ioctl(fd, VIDIOC_S_CTRL, &ctrl);
    if (rc < 0)
    {
        std::cout<<"Failed to set video jpeg subsampling"<<strerror(errno)<<std::endl;
    }

    height = fmt.fmt.pix.height;
    width = fmt.fmt.pix.width;
    pixelformat = fmt.fmt.pix.pixelformat;

    if (pixelformat != V4L2_PIX_FMT_RGB24 && pixelformat != V4L2_PIX_FMT_JPEG)
    {
        std::cout<<"Pixel Format not supported"<<pixelformat<<std::endl;
        if (pixelformat == V4L2_PIX_FMT_MJPEG)
        {
            std::cout<<"Pixel Format is MJPEG"<<std::endl;
        }
        if (pixelformat == V4L2_PIX_FMT_YUYV)
        {
            std::cout<<"Pixel Format is YUYV"<<std::endl;
        }
        //std::exit(EXIT_FAILURE);
    }

    resize();

    if (oldHeight != height || oldWidth != width)
    {
        resizeAfterOpen = true;
    }
}

void Video::stop()
{
    int rc;
    unsigned int i;
    v4l2_buf_type type(V4L2_BUF_TYPE_VIDEO_CAPTURE);

    if (fd < 0)
    {
        return;
    }

    lastFrameIndex = -1;

    rc = ioctl(fd, VIDIOC_STREAMOFF, &type);
    if (rc)
    {
        std::cout<<"Failed to stop streaming"<<strerror(errno)<<std::endl;
    }

    for (i = 0; i < buffers.size(); ++i)
    {
        if (buffers[i].data)
        {
            munmap(buffers[i].data, buffers[i].size);
            buffers[i].data = nullptr;
            buffers[i].queued = false;
        }
    }

    close(fd);
    fd = -1;
}

} // namespace ikvm
