#pragma once
/*
This HW decoder class  is just a thin wrapper around libav 
functions to decode HW videos. It would have been easy to use 
libav directly in   the python module code but I like to keep these 
things separate.
 
It is mostly based on roxlu's code. See
http://roxlu.com/2014/039/decoding-HW-and-yuv420p-playback

However, in contrast to roxlu's code the color space conversion is
done by libav functions - so on the CPU, I suppose.

Most functions/members throw exceptions. This way, error states are 
conveniently forwarded to python via the exception translation 
mechanisms of boost::python.  
*/

// for ssize_t (signed int type as large as pointer type)
#include <cstdlib>
#include <stdexcept>
#include <utility>

struct AVCodecContext;
struct AVFrame;
struct AVCodec;
struct AVCodecParserContext;
struct SwsContext;
struct AVPacket;
struct AVBufferRef;


class HWException : public std::runtime_error
{
public:
  HWException(const char* s) : std::runtime_error(s) {}
};

class HWInitFailure : public HWException
{
public:
    HWInitFailure(const char* s) : HWException(s) {}
    HWInitFailure(const std::string& s) : HWException(s.c_str()) {}
};

class HWDecodeFailure : public HWException
{
public:
    HWDecodeFailure(const char* s) : HWException(s) {}
    HWDecodeFailure(const std::string& s) : HWException(s.c_str()) {}
};


class HWDecoder
{
  /* Persistent things here, using RAII for cleanup. */
  AVCodecContext        *context;
  AVFrame               *frame;
  AVFrame               *sw_frame;
  AVCodec               *codec;
  AVCodecParserContext  *parser;
  AVBufferRef           *hw_device_ctx = nullptr;
  /* In the documentation example on the github master branch, the 
packet is put on the heap. This is done here to store the pointers 
to the encoded data, which must be kept around  between calls to 
parse- and decode frame. In release 11 it is put on the stack, too. 
  */
  AVPacket              *pkt;
  int hw_decoder_init(AVCodecContext *ctx, int type);
public:
  HWDecoder(const std::string& codename, const std::string& hwtype);
  ~HWDecoder();
  /* First, parse a continuous data stream, dividing it into 
packets. When there is enough data to form a new frame, decode 
the data and return the frame. parse returns the number 
of consumed bytes of the input stream. It stops consuming 
bytes at frame boundaries.
  */
  ptrdiff_t parse(const uint8_t* in_data, ptrdiff_t in_size);
  bool is_frame_available() const;
  const AVFrame& decode_frame();
};

// TODO: Rename to OutputStage or so?!
class ConverterBGR24
{
  SwsContext *context;
  AVFrame *framebgr;
  
public:
  ConverterBGR24();
  ~ConverterBGR24();
   
  /*  Returns, given a width and height, 
      how many bytes the frame buffer is going to need. */
  int predict_size(int w, int h);
  /*  Given a decoded frame, convert it to BGR format and fill 
out_rgb with the result. Returns a AVFrame structure holding 
additional information about the BGR frame, such as the number of
bytes in a row and so on. */
  const AVFrame& convert(const AVFrame &frame, uint8_t* out_rgb);
};

void disable_logging();

/* Wrappers, so we don't have to include libav headers. */
std::pair<int, int> width_height(const AVFrame&);
int row_size(const AVFrame&);

/* all the documentation links
 * My version of libav on ubuntu 16 appears to be from the release/11 branch on github
 * Video decoding example: https://libav.org/documentation/doxygen/release/11/avcodec_8c_source.html#l00455
 * https://libav.org/documentation/doxygen/release/9/group__lavc__decoding.html
 * https://libav.org/documentation/doxygen/release/11/group__lavc__parsing.html
 * https://libav.org/documentation/doxygen/release/9/swscale_8h.html
 * https://libav.org/documentation/doxygen/release/9/group__lavu.html
 * https://libav.org/documentation/doxygen/release/9/group__lavc__picture.html
 * http://dranger.com/ffmpeg/tutorial01.html
 */
