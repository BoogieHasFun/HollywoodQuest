#include "render/video_recorder.hpp"

#include "modloader/shared/modloader.hpp"

extern "C" {
#include "libavcodec/mediacodec.h"
#include "libavcodec/jni.h"
}

#include "UnityEngine/Time.hpp"


#pragma region FFMPEG_Debugging
void* iterate_data = NULL;

const char* Encoder_GetNextCodecName(bool onlyEncoders = true)
{
    AVCodec* current_codec = const_cast<AVCodec *>(av_codec_iterate(&iterate_data));
    while (current_codec != NULL)
    {
        if (onlyEncoders && !av_codec_is_encoder(current_codec))
        {
            current_codec = const_cast<AVCodec *>(av_codec_iterate(&iterate_data));
            continue;
        }
        return current_codec->name;
    }
    return "";
}

const char* Encoder_GetFirstCodecName()
{
    iterate_data = NULL;
    return Encoder_GetNextCodecName();
}

std::vector<std::string> GetCodecs()
{
    std::vector<std::string> l;

    auto s = std::string(Encoder_GetFirstCodecName());
    while (!s.empty())
    {
        l.push_back(s);
        s = std::string(Encoder_GetNextCodecName());
    }

    return l;
}
#pragma endregion

#pragma region Initialization
VideoCapture::VideoCapture(uint32_t width, uint32_t height, uint32_t fpsRate,
                           int bitrate, bool stabilizeFPS, std::string_view encodeSpeed,
                           std::string_view filepath,
                           std::string_view encoderStr,
                           AVPixelFormat pxlFormat)
        : AbstractVideoEncoder(width, height, fpsRate),
          bitrate(bitrate * 1000),
          stabilizeFPS(stabilizeFPS),
          encodeSpeed(encodeSpeed),
          encoderStr(encoderStr),
          filename(filepath),
          pxlFormat(pxlFormat) {
    HLogger.fmtLog<Paper::LogLevel::INF>("Setting up video at path %s", this->filename.c_str());
}

void VideoCapture::Init() {
    frameCounter = 0;
    int ret;

    // todo: should this be done?
    static bool jniEnabled = false;

    if (!jniEnabled) {
        // For MediaCodec Android, though it does not support encoding. We can do this anyways
        auto jni = Modloader::getJni();
        JavaVM *myVM;
        jni->GetJavaVM(&myVM);
        if (av_jni_set_java_vm(myVM, NULL) < 0) {
            HLogger.fmtLog<Paper::LogLevel::INF>("Unable to enable JNI");
        } else {
            HLogger.fmtLog<Paper::LogLevel::INF>("Successfully enabled JNI");
        }
        jniEnabled = true;
    }

    for (auto& s : GetCodecs()) {
        HLogger.fmtLog<Paper::LogLevel::INF>("codec: %s", s.c_str());
    }

    HLogger.fmtLog<Paper::LogLevel::INF>("Attempting to use %s", std::string(encoderStr).c_str());
    codec = avcodec_find_encoder_by_name(std::string(encoderStr).c_str());

    // codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec)
    {
        HLogger.fmtLog<Paper::LogLevel::INF>("Codec not found");
        return;
    }

    c = avcodec_alloc_context3(codec);
    if (!c)
    {
        HLogger.fmtLog<Paper::LogLevel::INF>("Could not allocate video codec context\n");
        return;
    }

    pkt = av_packet_alloc();
    if (!pkt)
        return;

    c->bit_rate = bitrate * 1000;
    c->width = width;
    c->height = height;
    c->time_base = (AVRational){1, (int) fpsRate};
    c->framerate = (AVRational){(int) fpsRate, 1};

    c->gop_size = 10;
    c->max_b_frames = 1;
    c->pix_fmt = pxlFormat;
    // c->pix_fmt = AV_PIX_FMT_YUV420P;

    if (codec->id == AV_CODEC_ID_H264) {
        av_opt_set(c->priv_data, "preset", encodeSpeed.c_str(), 0);
        // av_opt_set(c->priv_data, "tune", "zerolatency", 0);
    }

    ret = avcodec_open2(c, codec, NULL);
    if (ret < 0)
    {
        HLogger.fmtLog<Paper::LogLevel::INF>("Could not open codec: %s\n", av_err2str(ret));
        return;
    }

    HLogger.fmtLog<Paper::LogLevel::INF>("Successfully opened codec");

    f = std::ofstream(filename);
    if (!f)
    {
        HLogger.fmtLog<Paper::LogLevel::INF>("Could not open %s\n", filename.c_str());
        return;
    }

    frame = av_frame_alloc();
    if (!frame)
    {
        HLogger.fmtLog<Paper::LogLevel::INF>("Could not allocate video frame\n");
        return;
    }
    frame->format = c->pix_fmt;
    frame->width = width;
    frame->height = height;

    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0)
    {
        HLogger.fmtLog<Paper::LogLevel::INF>("Could not allocate the video frame data\n");
        return;
    }

    // swsCtx = sws_getContext(c->width, c->height, AV_PIX_FMT_RGB24, c->width, c->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, 0, 0, 0);

    initialized = true;
    HLogger.fmtLog<Paper::LogLevel::INF>("Finished initializing video at path %s", filename.c_str());

    encodingThread = std::thread(&VideoCapture::encodeFramesThreadLoop, this);

    emptyFrame = new rgb24[width * height];
}

#pragma endregion

#pragma region encode

void VideoCapture::encodeFramesThreadLoop() {
    HLogger.fmtLog<Paper::LogLevel::INF>("Starting encoding thread");

    while (initialized) {
        QueueContent frameData = nullptr;

        // Block instead?
        if (!framebuffers.try_dequeue(frameData)) {
            std::this_thread::yield();
            continue;
        }

        auto startTime = std::chrono::high_resolution_clock::now();
        this->AddFrame(frameData);
        auto currentTime = std::chrono::high_resolution_clock::now();
        int64_t duration = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime).count();

        // HLogger.fmtLog<Paper::LogLevel::INF>("Took %lldms to add and encode frame", (long long) duration);

        free(frameData);
    }
    HLogger.fmtLog<Paper::LogLevel::INF>("Ending encoding thread");
}




void VideoCapture::Encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt, std::ofstream& outfile, int framesToWrite = 1) {
    int ret;

    /* send the frame to the encoder */
    // if (frame)
    // {
        // HLogger.fmtLog<Paper::LogLevel::INF>("Send frame %i at time %li", frameCounter, frame->pts);
    // }

    ret = avcodec_send_frame(enc_ctx, frame);
    if (ret < 0)
    {
        HLogger.fmtLog<Paper::LogLevel::INF>("Error sending a frame for encoding\n");
        return;
    }

    while (ret >= 0)
    {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            return;
        }
        else if (ret < 0)
        {
            HLogger.fmtLog<Paper::LogLevel::INF>("Error during encoding\n");
            return;
        }

        // HLogger.fmtLog<Paper::LogLevel::INF>("Writing replay frames %d", pkt->size);

        // HLogger.fmtLog<Paper::LogLevel::INF>("Write packet %li (size=%i)\n", pkt->pts, pkt->size);
        for (int i = 0; i < framesToWrite; i++)
        {
            outfile.write(reinterpret_cast<const char *>(pkt->data), pkt->size);
        }
        av_packet_unref(pkt);
    }
}

void VideoCapture::AddFrame(rgb24 *data) {
    if(!initialized) return;

    if(startTime == 0) {
        // TODO: Use system time
        static auto get_time = il2cpp_utils::il2cpp_type_check::FPtrWrapper<&UnityEngine::Time::get_time>::get();
        startTime = get_time();
        HLogger.fmtLog<Paper::LogLevel::INF>("Video global time start is %f", startTime);
    }

    int framesToWrite = 1;

    // if (stabilizeFPS) {
    //     framesToWrite = std::max(0, int(TotalLength() / (1.0f / float(fps))) - frameCounter);
    //     HLogger.fmtLog<Paper::LogLevel::INF>("Frames to write: %i, equation is int(%f / (1 / %i)) - %i", framesToWrite, TotalLength(), fps, frameCounter);
    // }

    if(framesToWrite == 0) return;

    frameCounter += framesToWrite;

    int ret;

    /* make sure the frame data is writable */
    ret = av_frame_make_writable(frame);
    if (ret < 0)
    {
        HLogger.fmtLog<Paper::LogLevel::INF>("Could not make the frame writable: %s", av_err2str(ret));
        return;
    }

    // int inLinesize[1] = {3 * c->width};
    // sws_scale(swsCtx, (const uint8_t *const *)&data, inLinesize, 0, c->height, frame->data, frame->linesize);

    frame->data[0] = (uint8_t*) data;
    if (stabilizeFPS) {
        frame->pts = TotalLength();
    } else {
        frame->pts = (int) ((1.0f / (float) fpsRate) * (float) frameCounter);
    }
    /* encode the image */
    Encode(c, frame, pkt, f, framesToWrite);


    frame->data[0] = reinterpret_cast<uint8_t *>(emptyFrame);
//  iterating slow?  for(auto & i : frame->data) i = nullptr;
}

#pragma endregion





void VideoCapture::queueFrame(rgb24* queuedFrame, std::optional<float> timeOfFrame) {
    if(!initialized)
        throw std::runtime_error("Video capture is not initialized");

    while(!framebuffers.enqueue(queuedFrame)) {
        std::this_thread::yield();
    }
//    HLogger.fmtLog<Paper::LogLevel::INF>("Frame queue: %zu", flippedframebuffers.size_approx());
}


#pragma region Finish
void VideoCapture::Finish()
{
    if(!initialized) {
        HLogger.fmtLog<Paper::LogLevel::INF>("Attempted to finish video capture when capture wasn't initialized, returning");
        return;
    }
    //DELAYED FRAMES
    Encode(c, NULL, pkt, f);

    f.close();

    avcodec_free_context(&c);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    // sws_freeContext(swsCtx);

    initialized = false;
}

VideoCapture::~VideoCapture()
{
    if(initialized) Finish();

    if (encodingThread.joinable())
        encodingThread.join();

    delete[] emptyFrame;

    HLogger.fmtLog<Paper::LogLevel::INF>("Deleting video capture {}", fmt::ptr(this));

    QueueContent frame;
    while (framebuffers.try_dequeue(frame)) {
        free(frame);
    }
}

#pragma endregion