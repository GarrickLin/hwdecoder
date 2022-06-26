extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libswscale/swscale.h>
}

#include "hwdecoder.hpp"

static enum AVPixelFormat hw_pix_fmt;

int HWDecoder::hw_decoder_init(AVCodecContext *ctx, int type)
{
  int err = 0;

  if ((err = av_hwdevice_ctx_create(&hw_device_ctx, (enum AVHWDeviceType)type,
                                    NULL, NULL, 0)) < 0)
  {
    fprintf(stderr, "Failed to create specified HW device.\n");
    return err;
  }
  ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);

  return err;
}

static enum AVPixelFormat get_hw_format(AVCodecContext *ctx,
                                        const enum AVPixelFormat *pix_fmts)
{
  const enum AVPixelFormat *p;

  for (p = pix_fmts; *p != -1; p++)
  {
    if (*p == hw_pix_fmt)
      return *p;
  }

  fprintf(stderr, "Failed to get HW surface format.\n");
  return AV_PIX_FMT_NONE;
}

inline bool has_substring(const std::string &str, const std::string &substr)
{
  return str.find(substr) != std::string::npos;
}

HWDecoder::HWDecoder(const std::string &codename, const std::string &hwtype)
{
  avcodec_register_all();
  AVHWDeviceType type = av_hwdevice_find_type_by_name(hwtype.c_str());
  if (type == AV_HWDEVICE_TYPE_NONE)
  {
    fprintf(stderr, "Device type %s is not supported.\n", hwtype.c_str());
    fprintf(stderr, "Available device types:");
    while ((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE)
      fprintf(stderr, " %s", av_hwdevice_get_type_name(type));
    fprintf(stderr, "\n");
    return;
  }
  AVCodecID codeid;
  if (has_substring(codename, "264"))
  {
    codeid = AV_CODEC_ID_H264;
  }
  else if (has_substring(codename, "265") ||
           has_substring(codename, "hevc") ||
           has_substring(codename, "HEVC"))
  {
    codeid = AV_CODEC_ID_HEVC;
  }
  else
  {
    throw HWInitFailure("unsupported codec type: " + codename);
  }
  codec = avcodec_find_decoder(codeid);
  if (!codec)
    throw HWInitFailure("cannot find decoder");
  for (int i = 0;; i++)
  {
    const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
    if (!config)
    {
      throw HWInitFailure("decoder " +
                          std::string(codec->name) +
                          " does not support device type " +
                          av_hwdevice_get_type_name(type));
    }
    if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
        config->device_type == type)
    {
      hw_pix_fmt = config->pix_fmt;
      break;
    }
  }
  context = avcodec_alloc_context3(codec);
  if (!context)
    throw HWInitFailure("cannot allocate context");

  // Note: CODEC_CAP_TRUNCATED was prefixed with AV_...
  if (codec->capabilities & AV_CODEC_CAP_TRUNCATED)
  {
    context->flags |= AV_CODEC_FLAG_TRUNCATED;
  }
  context->get_format = get_hw_format;
  if (hw_decoder_init(context, type) < 0)
    throw HWInitFailure("hw_decoder_init failed");
  if (avcodec_open2(context, codec, nullptr) < 0)
    throw HWInitFailure("avcodec_open2 failed");
  parser = av_parser_init(AV_CODEC_ID_H264);
  if (!parser)
    throw HWInitFailure("cannot init parser");

  frame = av_frame_alloc();
  sw_frame = av_frame_alloc();
  if (!frame || !sw_frame)
    throw HWInitFailure("cannot allocate frame");

#if 1
  pkt = new AVPacket;
  if (!pkt)
    throw HWInitFailure("cannot allocate packet");
  av_init_packet(pkt);
#endif
}

HWDecoder::~HWDecoder()
{
  av_parser_close(parser);
  avcodec_close(context);
  av_free(context);
  av_frame_free(&frame);
  av_frame_free(&sw_frame);
  av_buffer_unref(&hw_device_ctx);
#if 1
  delete pkt;
#endif
}

ptrdiff_t HWDecoder::parse(const uint8_t *in_data, ptrdiff_t in_size)
{
  ptrdiff_t nread = av_parser_parse2(parser, context, &pkt->data, &pkt->size,
                                     in_data, in_size,
                                     0, 0, AV_NOPTS_VALUE);
  return nread;
}

bool HWDecoder::is_frame_available() const
{
  return pkt->size > 0;
}

const AVFrame &HWDecoder::decode_frame()
{
#if (LIBAVCODEC_VERSION_MAJOR > 56)
  int ret;
  if (pkt)
  {
    ret = avcodec_send_packet(context, pkt);
    if (!ret)
    {
      ret = avcodec_receive_frame(context, frame);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
      {
        av_frame_free(&frame);
        av_frame_free(&sw_frame);
      }
      if (ret)
      {
        return *sw_frame;
      }
    }
    if (frame->format == hw_pix_fmt)
    {
      /* retrieve data from GPU to CPU */
      ret = av_hwframe_transfer_data(sw_frame, frame, 0);
      if (ret < 0)
      {
        fprintf(stderr, "Error transferring the data to system memory\n");
      }
      return *sw_frame;
    }
  }
  throw HWDecodeFailure("error decoding frame");
#else
  int got_picture = 0;
  int nread = avcodec_decode_video2(context, frame, &got_picture, pkt);
  if (nread < 0 || got_picture == 0)
    throw HWDecodeFailure("error decoding frame\n");
  return *frame;
#endif
}

ConverterBGR24::ConverterBGR24()
{
  framebgr = av_frame_alloc();
  if (!framebgr)
    throw HWDecodeFailure("cannot allocate frame");
  context = nullptr;
}

ConverterBGR24::~ConverterBGR24()
{
  sws_freeContext(context);
  av_frame_free(&framebgr);
}

const AVFrame &ConverterBGR24::convert(const AVFrame &frame, uint8_t *out_rgb)
{
  int w = frame.width;
  int h = frame.height;
  int pix_fmt = frame.format;

  context = sws_getCachedContext(context,
                                 w, h, (AVPixelFormat)pix_fmt,
                                 w, h, AV_PIX_FMT_BGR24, SWS_BILINEAR,
                                 nullptr, nullptr, nullptr);
  if (!context)
    throw HWDecodeFailure("cannot allocate context");

  // Setup framebgr with out_rgb as external buffer. Also say that we want BGR24 output.
  av_image_fill_arrays(framebgr->data, framebgr->linesize, out_rgb, AV_PIX_FMT_BGR24, w, h, 1);
  // Do the conversion.
  sws_scale(context, frame.data, frame.linesize, 0, h,
            framebgr->data, framebgr->linesize);
  framebgr->width = w;
  framebgr->height = h;
  return *framebgr;
}

/*
Determine required size of framebuffer.

avpicture_get_size is used in http://dranger.com/ffmpeg/tutorial01.html
to do this. However, avpicture_get_size returns the size of a compact
representation, without padding bytes. Since we use av_image_fill_arrays to
fill the buffer we should also use it to determine the required size.
*/
int ConverterBGR24::predict_size(int w, int h)
{
  return av_image_fill_arrays(framebgr->data, framebgr->linesize, nullptr, AV_PIX_FMT_BGR24, w, h, 1);
}

std::pair<int, int> width_height(const AVFrame &f)
{
  return std::make_pair(f.width, f.height);
}

int row_size(const AVFrame &f)
{
  return f.linesize[0];
}

void disable_logging()
{
  av_log_set_level(AV_LOG_QUIET);
}