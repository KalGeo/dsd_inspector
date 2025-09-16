#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <numeric>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <complex>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <fftw3.h>
#include <zlib.h>

// ----------------- Tiny PNG writers (grayscale & RGB) -----------------
static bool write_png_gray(const char* filename, int w, int h, const std::vector<unsigned char>& gray) {
    FILE* f = fopen(filename, "wb");
    if(!f) return false;
    auto wr32 = [&](uint32_t v){ unsigned char b[4]={(unsigned char)(v>>24),(unsigned char)(v>>16),(unsigned char)(v>>8),(unsigned char)v}; fwrite(b,1,4,f); };
    const unsigned char sig[8] = {137,80,78,71,13,10,26,10}; fwrite(sig,1,8,f);
    unsigned char ihdr[13];
    ihdr[0]= (w>>24)&255; ihdr[1]=(w>>16)&255; ihdr[2]=(w>>8)&255; ihdr[3]=w&255;
    ihdr[4]= (h>>24)&255; ihdr[5]=(h>>16)&255; ihdr[6]=(h>>8)&255; ihdr[7]=h&255;
    ihdr[8]=8; ihdr[9]=0; ihdr[10]=0; ihdr[11]=0; ihdr[12]=0; // grayscale
    wr32(13); fwrite("IHDR",1,4,f); fwrite(ihdr,1,13,f);
    { uLong c=crc32(0L,Z_NULL,0); c=crc32(c,(const Bytef*)"IHDR",4); c=crc32(c,ihdr,13); wr32((uint32_t)c); }
    std::vector<unsigned char> raw; raw.reserve((w+1)*h);
    for(int y=0;y<h;++y){ raw.push_back(0); raw.insert(raw.end(), gray.begin()+y*w, gray.begin()+y*w+w); }
    uLongf cb=compressBound(raw.size()); std::vector<unsigned char> comp(cb);
    if (compress2(comp.data(), &cb, raw.data(), raw.size(), Z_BEST_SPEED) != Z_OK) { fclose(f); return false; }
    comp.resize(cb);
    wr32((uint32_t)comp.size()); fwrite("IDAT",1,4,f); fwrite(comp.data(),1,comp.size(),f);
    { uLong c=crc32(0L,Z_NULL,0); c=crc32(c,(const Bytef*)"IDAT",4); c=crc32(c,comp.data(),comp.size()); wr32((uint32_t)c); }
    wr32(0); fwrite("IEND",1,4,f); { uLong c=crc32(0L,Z_NULL,0); c=crc32(c,(const Bytef*)"IEND",4); wr32((uint32_t)c); }
    fclose(f); return true;
}

static bool write_png_rgb(const char* filename, int w, int h, const std::vector<unsigned char>& rgb){
    FILE* f=fopen(filename,"wb"); if(!f) return false;
    auto wr32=[&](uint32_t v){ unsigned char b[4]={(unsigned char)(v>>24),(unsigned char)(v>>16),(unsigned char)(v>>8),(unsigned char)v}; fwrite(b,1,4,f); };
    const unsigned char sig[8]={137,80,78,71,13,10,26,10}; fwrite(sig,1,8,f);
    unsigned char ihdr[13]; ihdr[0]=(w>>24)&255; ihdr[1]=(w>>16)&255; ihdr[2]=(w>>8)&255; ihdr[3]=w&255; ihdr[4]=(h>>24)&255; ihdr[5]=(h>>16)&255; ihdr[6]=(h>>8)&255; ihdr[7]=h&255; ihdr[8]=8; ihdr[9]=2; ihdr[10]=0; ihdr[11]=0; ihdr[12]=0; // RGB
    wr32(13); fwrite("IHDR",1,4,f); fwrite(ihdr,1,13,f); { uLong c=crc32(0L,Z_NULL,0); c=crc32(c,(const Bytef*)"IHDR",4); c=crc32(c,ihdr,13); wr32((uint32_t)c);}    
    std::vector<unsigned char> raw; raw.reserve((w*3+1)*h); for(int y=0;y<h;++y){ raw.push_back(0); raw.insert(raw.end(), rgb.begin()+y*w*3, rgb.begin()+y*w*3+w*3); }
    uLongf cb=compressBound(raw.size()); std::vector<unsigned char> comp(cb); if(compress2(comp.data(), &cb, raw.data(), raw.size(), Z_BEST_SPEED)!=Z_OK){ fclose(f); return false;} comp.resize(cb);
    wr32((uint32_t)comp.size()); fwrite("IDAT",1,4,f); fwrite(comp.data(),1,comp.size(),f); { uLong c=crc32(0L,Z_NULL,0); c=crc32(c,(const Bytef*)"IDAT",4); c=crc32(c,comp.data(),comp.size()); wr32((uint32_t)c);}    
    wr32(0); fwrite("IEND",1,4,f); { uLong c=crc32(0L,Z_NULL,0); c=crc32(c,(const Bytef*)"IEND",4); wr32((uint32_t)c);} fclose(f); return true;
}
// ----------------------------------------------------------------------

static void make_hann(std::vector<double>& w){ const size_t N=w.size(); for(size_t n=0;n<N;++n){ w[n]=0.5*(1.0-std::cos(2*M_PI*n/(N-1))); } }

// ----------------- Minimal RGB drawing helpers -----------------
struct ImageRGB {
    int w=0,h=0; std::vector<unsigned char> px; // RGB
    ImageRGB(){}
    ImageRGB(int W,int H):w(W),h(H),px(W*H*3,0){}
    void resize(int W,int H){ w=W; h=H; px.assign(W*H*3,0); }
    void clear(unsigned char r,unsigned char g,unsigned char b){ for(int i=0;i<w*h;++i){ px[i*3+0]=r; px[i*3+1]=g; px[i*3+2]=b; } }
    inline void set(int x,int y,unsigned char r,unsigned char g,unsigned char b){ if((unsigned)x<(unsigned)w && (unsigned)y<(unsigned)h){ int o=(y*w+x)*3; px[o]=r; px[o+1]=g; px[o+2]=b; }}
};

// tiny 5x7 bitmap font (ASCII 32..127)
static const unsigned char FONT5x7[96][5]={
{0,0,0,0,0},{0x00,0x00,0x5f,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7f,0x14,0x7f,0x14},{0x24,0x2a,0x7f,0x2a,0x12},
{0x23,0x13,0x08,0x64,0x62},
{0x36,0x49,0x55,0x22,0x50},
{0x00,0x05,0x03,0x00,0x00},
{0x00,0x1c,0x22,0x41,0x00},
{0x00,0x41,0x22,0x1c,0x00},
{0x14,0x08,0x3e,0x08,0x14},
{0x08,0x08,0x3e,0x08,0x08},
{0x00,0x50,0x30,0x00,0x00},
{0x08,0x08,0x08,0x08,0x08},
{0x00,0x60,0x60,0x00,0x00},
{0x20,0x10,0x08,0x04,0x02},
{0x3e,0x51,0x49,0x45,0x3e},
{0x00,0x42,0x7f,0x40,0x00},
{0x42,0x61,0x51,0x49,0x46},
{0x21,0x41,0x45,0x4b,0x31},
{0x18,0x14,0x12,0x7f,0x10},
{0x27,0x45,0x45,0x45,0x39},{0x3c,0x4a,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},
{0x06,0x49,0x49,0x29,0x1e},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3e},{0x7e,0x11,0x11,0x11,0x7e},{0x7f,0x49,0x49,0x49,0x36},
{0x3e,0x41,0x41,0x41,0x22},{0x7f,0x41,0x41,0x22,0x1c},{0x7f,0x49,0x49,0x49,0x41},{0x7f,0x09,0x09,0x09,0x01},{0x3e,0x41,0x49,0x49,0x7a},
{0x7f,0x08,0x08,0x08,0x7f},{0x00,0x41,0x7f,0x41,0x00},{0x20,0x40,0x41,0x3f,0x01},{0x7f,0x08,0x14,0x22,0x41},{0x7f,0x40,0x40,0x40,0x40},
{0x7f,0x02,0x0c,0x02,0x7f},{0x7f,0x04,0x08,0x10,0x7f},{0x3e,0x41,0x41,0x41,0x3e},
{0x7f,0x09,0x09,0x09,0x06},
{0x3e,0x41,0x51,0x21,0x5e}, // Q

{0x7f,0x09,0x19,0x29,0x46}, // R (FIXED)

{0x46,0x49,0x49,0x49,0x31},
{0x01,0x01,0x7f,0x01,0x01},
{0x3f,0x40,0x40,0x40,0x3f},
{0x1f,0x20,0x40,0x20,0x1f},
{0x3f,0x40,0x38,0x40,0x3f},
{0x63,0x14,0x08,0x14,0x63},
{0x07,0x08,0x70,0x08,0x07},
{0x61,0x51,0x49,0x45,0x43}, // Z

{0x00,0x7f,0x41,0x41,0x00},// '['

{0x02,0x04,0x08,0x10,0x20}, // '\'
{0x00,0x41,0x41,0x7f,0x00},
{0x04,0x02,0x01,0x02,0x04},
{0x40,0x40,0x40,0x40,0x40},
{0x00,0x01,0x02,0x04,0x00},
{0x20,0x54,0x54,0x54,0x78},
{0x7f,0x48,0x44,0x44,0x38},
{0x38,0x44,0x44,0x44,0x20},
{0x38,0x44,0x44,0x48,0x7f},
{0x38,0x54,0x54,0x54,0x18},{0x08,0x7e,0x09,0x01,0x02},
{0x0c,0x52,0x52,0x52,0x3e},{0x7f,0x08,0x04,0x04,0x78},{0x00,0x44,0x7d,0x40,0x00},{0x20,0x40,0x44,0x3d,0x00},{0x7f,0x10,0x28,0x44,0x00},
{0x00,0x41,0x7f,0x40,0x00},{0x7c,0x04,0x18,0x04,0x78},{0x7c,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0x7c,0x14,0x14,0x14,0x08},
{0x08,0x14,0x14,0x18,0x7c},{0x7c,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},{0x04,0x3f,0x44,0x40,0x20},{0x3c,0x40,0x40,0x20,0x7c},
{0x1c,0x20,0x40,0x20,0x1c},{0x3c,0x40,0x30,0x40,0x3c},{0x44,0x28,0x10,0x28,0x44},{0x0c,0x50,0x50,0x50,0x3c},{0x44,0x64,0x54,0x4c,0x44},
{0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x7f,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x10,0x08,0x08,0x10,0x08}
};

static_assert(sizeof(FONT5x7)/sizeof(FONT5x7[0]) == 96, "FONT5x7 must have 96 rows");

// spot checks for common culprits
//static_assert(FONT5x7['A' - 32][0] == 0x7e, "'A' glyph wrong");
//static_assert(FONT5x7['R' - 32][0] == 0x7f, "'R' glyph wrong");
//static_assert(FONT5x7['S' - 32][0] == 0x46, "'S' glyph wrong");
//static_assert(FONT5x7['p' - 32][0] == 0x7c, "'p' glyph wrong");

// Replace any non-ASCII bytes with '?' so our 5x7 font never sees UTF-8.
static std::string ascii_sanitize(const std::string& s){
    std::string out; out.reserve(s.size());
    for (unsigned char ch : s){
        out.push_back((ch >= 32 && ch <= 126) ? char(ch) : '?');
    }
    return out;
}


// --- ASCII → glyph (no compensation) ---
static inline const unsigned char* glyph_for_ascii(char c){
    unsigned int uc = (unsigned char)c;
    if (uc < 32 || uc > 127) uc = '?';
    return FONT5x7[uc - 32];
}

// Temporary shim so old calls compile (but ideally unused):
static inline const unsigned char* glyph_for_scaled(char c){
    return glyph_for_ascii(c);
}


static void draw_char(ImageRGB& im,int x,int y,char c,
                      unsigned char r,unsigned char g,unsigned char b){
    const unsigned char* col = glyph_for_ascii(c);
    for(int cx=0; cx<5; ++cx){
        unsigned char bits = col[cx];
        for(int cy=0; cy<7; ++cy){
            if(bits & (1u<<cy)) im.set(x+cx, y+cy, r,g,b);
        }
    }
}


static void draw_text(ImageRGB& im, int x, int y, const std::string& s,
                      unsigned char r, unsigned char g, unsigned char b){
    int off = 0;
    for(char c : s){
        if (c == '\n'){ y += 9; off = 0; continue; }
        draw_char(im, x+off, y, c, r,g,b);
        off += 6;
    }
}



static void draw_line(ImageRGB& im,int x0,int y0,int x1,int y1,unsigned char r,unsigned char g,unsigned char b){
    int dx=std::abs(x1-x0), sx=x0<x1?1:-1; int dy=-std::abs(y1-y0), sy=y0<y1?1:-1; int err=dx+dy; while(true){ im.set(x0,y0,r,g,b); if(x0==x1 && y0==y1) break; int e2=2*err; if(e2>=dy){ err+=dy; x0+=sx;} if(e2<=dx){ err+=dx; y0+=sy;} }
}

static void colormap_turbo(double t, unsigned char& r, unsigned char& g, unsigned char& b){
    // t in [0,1]  -> r,g,b in [0,255]
    t = std::clamp(t, 0.0, 1.0);
    // Simple, good-looking “Turbo-like” polynomial approx that returns 0..255 directly.
    double rF =  34.61 + t*(1172.69 + t*(-10793.6 + t*(33300.0 + t*(-38394.5 + t*17425.7))));
    double gF =  23.31 + t*(  557.33 + t*(  1225.0 + t*( -3574.0 + t*(  4095.0 - t* 1550.0))));
    double bF =  27.20 + t*( 3211.10 + t*(-15328.0 + t*(27814.0 + t*(-22569.0 + t* 6838.0))));
    r = (unsigned char)std::clamp((int)std::lround(rF), 0, 255);
    g = (unsigned char)std::clamp((int)std::lround(gF), 0, 255);
    b = (unsigned char)std::clamp((int)std::lround(bF), 0, 255);
}

// Scale-blit a small text bitmap drawn with draw_text (correct glyphs)
static void draw_text_scaled_nn(ImageRGB& dst, int x, int y, const std::string& s,
                                unsigned char r, unsigned char g, unsigned char b, int scale)
{
    if (scale <= 1){ draw_text(dst, x, y, s, r, g, b); return; }

    const int small_h = 9;
    const int small_w = (int)s.size() * 6; // 5px glyph + 1px spacing
    if (small_w <= 0) return;

    ImageRGB small(small_w, small_h);
    small.clear(0,0,0);
    draw_text(small, 0, 0, s, 255,255,255); // render once at 1×

    // Nearest-neighbor expand into dst with desired color
    for(int sy=0; sy<small_h; ++sy){
        for(int sx=0; sx<small_w; ++sx){
            int o = (sy*small.w + sx)*3;
            if (small.px[o] | small.px[o+1] | small.px[o+2]){
                for(int dx=0; dx<scale; ++dx)
                    for(int dy=0; dy<scale; ++dy)
                        dst.set(x + sx*scale + dx, y + sy*scale + dy, r,g,b);
            }
        }
    }
}

// ----------------- App options & usage -----------------
struct Options {
    std::string input;
    std::string outdir = "./out";
    int target_sr = 176400;   // resample to 176.4 kHz
    int fft_size  = 4096;
    int hop_size  = 2048;
    int seconds   = 180;      // analyze first N seconds (0 = whole file)
};

static void usage(){
    std::cout << "\nDSD Inspector — spectrum, noise-shaping, DR\n\n"              << "Usage: dsd_inspector -i <input.dsf|dff|flac|wav> [--out outdir] [--sr 176400] [--fft 4096] [--hop 2048] [--sec 180] [--no-pretty]\n\n";
}

// ----------------- Decode helpers -----------------
static std::vector<float> decode_to_mono(const Options& opt, int& sr_out){
    std::vector<float> mono;
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, opt.input.c_str(), nullptr, nullptr) < 0)
        throw std::runtime_error("Failed to open input");
    if (avformat_find_stream_info(fmt, nullptr) < 0)
        throw std::runtime_error("Failed to find stream info");

    int astream = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (astream < 0) throw std::runtime_error("No audio stream");

    AVStream* st = fmt->streams[astream];
    const AVCodec* dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) throw std::runtime_error("No decoder");

    AVCodecContext* ctx = avcodec_alloc_context3(dec);
    if (!ctx) throw std::runtime_error("No codec ctx");
    if (avcodec_parameters_to_context(ctx, st->codecpar) < 0)
        throw std::runtime_error("param->ctx failed");

    if (avcodec_open2(ctx, dec, nullptr) < 0)
        throw std::runtime_error("open decoder failed");

    int in_channels = ctx->ch_layout.nb_channels ? ctx->ch_layout.nb_channels : ctx->channels;
    int64_t in_layout = ctx->ch_layout.nb_channels ? av_get_default_channel_layout(ctx->ch_layout.nb_channels)
                                                   : av_get_default_channel_layout(in_channels);

    SwrContext* swr = swr_alloc_set_opts(nullptr,
        AV_CH_LAYOUT_MONO, AV_SAMPLE_FMT_FLT, opt.target_sr,
        in_layout, ctx->sample_fmt, ctx->sample_rate, 0, nullptr);
    if (!swr || swr_init(swr) < 0) throw std::runtime_error("swr init failed");

    AVPacket* pkt = av_packet_alloc();
    AVFrame*  frm = av_frame_alloc();

    int64_t max_samples = (opt.seconds>0) ? (int64_t)opt.target_sr*opt.seconds : INT64_MAX;
    int64_t written=0;

    while (av_read_frame(fmt, pkt) >= 0){
        if (pkt->stream_index != astream){ av_packet_unref(pkt); continue; }
        if (avcodec_send_packet(ctx, pkt) < 0){ av_packet_unref(pkt); break; }
        av_packet_unref(pkt);
        while (avcodec_receive_frame(ctx, frm) >= 0){
            const uint8_t** in = (const uint8_t**)frm->extended_data;
            int outcount = av_rescale_rnd(swr_get_delay(swr, ctx->sample_rate) + frm->nb_samples, opt.target_sr, ctx->sample_rate, AV_ROUND_UP);
            std::vector<float> tmp(outcount);
            uint8_t* outptr = (uint8_t*)tmp.data();
            int conv = swr_convert(swr, &outptr, outcount, in, frm->nb_samples);
            if (conv>0){
                int ns = conv; tmp.resize(ns);
                int64_t can = std::min<int64_t>(ns, max_samples - written);
                if (can<=0){ av_frame_unref(frm); goto done; }
                mono.insert(mono.end(), tmp.begin(), tmp.begin()+can);
                written += can;
            }
            av_frame_unref(frm);
        }
        if (written>=max_samples) break;
    }
 done:
    sr_out = opt.target_sr;
    av_frame_free(&frm); av_packet_free(&pkt); swr_free(&swr); avcodec_free_context(&ctx); avformat_close_input(&fmt);
    return mono;
}

static std::vector<float> decode_to_stereo(const Options& opt, int& sr_out){
    std::vector<float> out;
    AVFormatContext* fmt = nullptr; if (avformat_open_input(&fmt, opt.input.c_str(), nullptr, nullptr) < 0) throw std::runtime_error("Failed to open input");
    if (avformat_find_stream_info(fmt, nullptr) < 0) throw std::runtime_error("Failed to find stream info");
    int astream = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0); if (astream < 0) throw std::runtime_error("No audio stream");
    AVStream* st = fmt->streams[astream]; const AVCodec* dec = avcodec_find_decoder(st->codecpar->codec_id); if(!dec) throw std::runtime_error("No decoder");
    AVCodecContext* ctx = avcodec_alloc_context3(dec); if(!ctx) throw std::runtime_error("No codec ctx"); if (avcodec_parameters_to_context(ctx, st->codecpar) < 0) throw std::runtime_error("param->ctx failed"); if (avcodec_open2(ctx, dec, nullptr) < 0) throw std::runtime_error("open decoder failed");
    int in_channels = ctx->ch_layout.nb_channels ? ctx->ch_layout.nb_channels : ctx->channels; int64_t in_layout = ctx->ch_layout.nb_channels ? av_get_default_channel_layout(ctx->ch_layout.nb_channels) : av_get_default_channel_layout(in_channels);
    
    SwrContext* swr = swr_alloc_set_opts(nullptr, AV_CH_LAYOUT_STEREO,
    AV_SAMPLE_FMT_FLT,                     // <-- correct
    opt.target_sr, in_layout, ctx->sample_fmt, ctx->sample_rate, 0, nullptr);
    
    //SwrContext* swr = swr_alloc_set_opts(nullptr, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FORMAT(AV_SAMPLE_FMT_FLT), opt.target_sr, in_layout, ctx->sample_fmt, ctx->sample_rate, 0, nullptr); 
    if(!swr || swr_init(swr)<0) throw std::runtime_error("swr init failed");
    AVPacket* pkt = av_packet_alloc(); AVFrame* frm = av_frame_alloc(); int64_t max_samples = (opt.seconds>0) ? (int64_t)opt.target_sr*opt.seconds : INT64_MAX; int64_t written=0;
    while (av_read_frame(fmt, pkt) >= 0){ if (pkt->stream_index != astream){ av_packet_unref(pkt); continue; } if (avcodec_send_packet(ctx, pkt) < 0){ av_packet_unref(pkt); break; } av_packet_unref(pkt); while (avcodec_receive_frame(ctx, frm) >= 0){ const uint8_t** in = (const uint8_t**)frm->extended_data; int outcount = av_rescale_rnd(swr_get_delay(swr, ctx->sample_rate)+frm->nb_samples, opt.target_sr, ctx->sample_rate, AV_ROUND_UP); std::vector<float> tmp(outcount*2); uint8_t* outptr = (uint8_t*)tmp.data(); int conv = swr_convert(swr, &outptr, outcount, in, frm->nb_samples); if(conv>0){ int ns=conv; tmp.resize(ns*2); int64_t can = std::min<int64_t>(ns, (max_samples - written)); if(can<=0){ av_frame_unref(frm); goto done2; } out.insert(out.end(), tmp.begin(), tmp.begin()+can*2); written+=can; } av_frame_unref(frm);} if(written>=max_samples) break; }
 done2:
    sr_out=opt.target_sr; av_frame_free(&frm); av_packet_free(&pkt); swr_free(&swr); avcodec_free_context(&ctx); avformat_close_input(&fmt); return out; }

// ----------------- Spectral analysis -----------------
struct SpectralOutputs{
    std::vector<double> freq;       // Hz
    std::vector<double> avg_mag_db; // dBFS
    std::vector<unsigned char> spectrogram_png; // grayscale heat
    int spec_w=0, spec_h=0;
};

static SpectralOutputs compute_spectrum_and_spectrogram(const std::vector<float>& x, int sr, int Nfft, int hop, const std::string&){
    SpectralOutputs out; int H = Nfft/2+1;
    std::vector<double> window(Nfft); make_hann(window);
    std::vector<double> in(Nfft);
    fftw_complex* cplx = (fftw_complex*)fftw_malloc(sizeof(fftw_complex)*H);
    fftw_plan plan = fftw_plan_dft_r2c_1d(Nfft, in.data(), cplx, FFTW_ESTIMATE);

    size_t frames = (x.size()>= (size_t)Nfft) ? (x.size()-Nfft)/hop + 1 : 0;
    std::vector<double> avg(H, 0.0);
    int W = (int)frames; int IMG_H = 512; std::vector<unsigned char> img(W*IMG_H, 0);
    auto magdb = [&](double re, double im){ double m = std::sqrt(re*re+im*im) / (Nfft/2.0); double db = 20.0*std::log10(m + 1e-12); return std::clamp(db, -120.0, 0.0); };

    for(size_t f=0; f<frames; ++f){
        size_t off = f*hop;
        for(int n=0;n<Nfft;++n){ in[n] = (double)x[off+n] * window[n]; }
        fftw_execute(plan);
        double maxbin= -1e9, minbin=1e9; std::vector<double> frame_db(H);
        for(int k=0;k<H;++k){ double db = magdb(cplx[k][0], cplx[k][1]); frame_db[k]=db; avg[k] += db; if(db>maxbin) maxbin=db; if(db<minbin) minbin=db; }
        double span = std::max(10.0, maxbin - minbin);
        for(int y=0;y<IMG_H;++y){ int k = (int)((double)y/IMG_H * (H-1)); double v = (frame_db[k]-minbin)/span; unsigned char g = (unsigned char)std::clamp((int)(v*255.0),0,255); img[(IMG_H-1-y)*W + (int)f] = g; }
    }

    if(frames>0){ for(int k=0;k<H;++k) avg[k]/= (double)frames; }
    out.freq.resize(H); for(int k=0;k<H;++k) out.freq[k] = (double)k * sr / (double)Nfft;
    out.avg_mag_db = std::move(avg); out.spectrogram_png = std::move(img); out.spec_w = W; out.spec_h = IMG_H;

    fftw_destroy_plan(plan); fftw_free(cplx); return out;
}

static void save_spectrogram_png(const SpectralOutputs& so, const std::string& path){ if(so.spec_w<=0 || so.spec_h<=0) return; write_png_gray(path.c_str(), so.spec_w, so.spec_h, so.spectrogram_png); }

static void save_average_spectrum_png(const SpectralOutputs& so, const std::string& path){
    const int W=1200, H=600; std::vector<unsigned char> img(W*H, 255);
    auto put = [&](int x,int y,unsigned char v){ if(x>=0&&x<W&&y>=0&&y<H) img[y*W+x]=v; }; auto line = [&](int x0,int y0,int x1,int y1){ int dx=std::abs(x1-x0), sx=x0<x1?1:-1; int dy=-std::abs(y1-y0), sy=y0<y1?1:-1; int err=dx+dy; while(true){ put(x0,y0,0); if(x0==x1&&y0==y1) break; int e2=2*err; if(e2>=dy){ err+=dy; x0+=sx;} if(e2<=dx){ err+=dx; y0+=sy;} } };
    for(int x=50;x<W-20;++x) put(x,H-40,0); for(int y=20;y<H-40;++y) put(50,y,0);
    double fmin=20.0, fmax = so.freq.back(); auto xmap = [&](double f){ double t = std::log10(std::max(f, fmin)/fmin) / std::log10(fmax/fmin); return 50 + (int)(t*(W-70)); }; auto ymap = [&](double db){ double t = (db+120.0)/120.0; t=std::clamp(t,0.0,1.0); return 20 + (int)((1.0-t)*(H-60)); };
    int prevx=-1, prevy=-1; for(size_t i=1;i<so.freq.size();++i){ int x = xmap(so.freq[i]); int y = ymap(so.avg_mag_db[i]); if(prevx>=0) line(prevx,prevy,x,y); prevx=x; prevy=y; }
    write_png_gray(path.c_str(), W, H, img);
}

// ----------------- Metrics & classification -----------------
struct Metrics { double cutoff_hz=0.0, cutoff_drop_db=0.0, noise_floor_30_50=0.0, noise_floor_50_80=0.0, noise_rise_db=0.0, crest_median_db=0.0, dr_like_db=0.0; };

static double band_avg(const SpectralOutputs& so, double f0, double f1){ size_t i0 = std::lower_bound(so.freq.begin(), so.freq.end(), f0) - so.freq.begin(); size_t i1 = std::lower_bound(so.freq.begin(), so.freq.end(), f1) - so.freq.begin(); i1 = std::min(i1, so.freq.size()); if(i0>=i1) return -120.0; double s=0; size_t n=0; for(size_t i=i0;i<i1;++i){ s+=so.avg_mag_db[i]; ++n; } return (n? s/n : -120.0); }

static Metrics analyze_metrics(const SpectralOutputs& so){ Metrics m; double min_db=0, min_f=0; for(size_t i=1;i<so.freq.size();++i){ double f=so.freq[i]; if(f<15000) continue; size_t j = i + (size_t)(1000.0 * so.freq.size()/so.freq.back()); if(j>=so.freq.size()) break; double drop = so.avg_mag_db[j]-so.avg_mag_db[i]; if(drop<min_db){ min_db=drop; min_f=f; } } if(min_db<-18.0){ m.cutoff_hz=min_f; m.cutoff_drop_db=min_db; } m.noise_floor_30_50=band_avg(so,30e3,50e3); m.noise_floor_50_80=band_avg(so,50e3,80e3); m.noise_rise_db=m.noise_floor_50_80-m.noise_floor_30_50; return m; }

static void compute_dynamic_metrics(const std::vector<float>& x, int sr, Metrics& m){ int W = std::max(1,3*sr); std::vector<double> crest_db; crest_db.reserve(x.size()/W+1); for(size_t off=0;off<x.size();off+=W){ size_t n=std::min<size_t>(W,x.size()-off); if(n<1000) break; double peak=0,sum2=0; for(size_t i=0;i<n;++i){ double v=x[off+i]; if(std::abs(v)>peak) peak=std::abs(v); sum2+=v*v; } double rms=std::sqrt(sum2/n); if(rms>0 && peak>0) crest_db.push_back(20*std::log10(peak/rms)); } if(!crest_db.empty()){ std::sort(crest_db.begin(), crest_db.end()); m.crest_median_db=crest_db[crest_db.size()/2]; } size_t B=sr; std::vector<double> peaks,rmses; for(size_t off=0; off<x.size(); off+=B){ size_t n=std::min<size_t>(B,x.size()-off); if(n<1000) break; double pk=0,s2=0; for(size_t i=0;i<n;++i){ double v=x[off+i]; if(std::abs(v)>pk) pk=std::abs(v); s2+=v*v;} peaks.push_back(20*std::log10(pk+1e-12)); double rms=std::sqrt(s2/n); rmses.push_back(20*std::log10(rms+1e-12)); } auto perc=[&](std::vector<double>& v,double p){ if(v.empty()) return -120.0; std::sort(v.begin(), v.end()); size_t idx=(size_t)std::clamp(p*(v.size()-1),0.0,(double)(v.size()-1)); return v[idx]; }; double p95=perc(peaks,0.95), p50=perc(rmses,0.50); m.dr_like_db=p95-p50; }

static std::string classify(const Metrics& m){ 
	std::string cls;
	
	if (m.cutoff_hz>21000 && m.cutoff_hz<23500) cls = "Likely 44.1 kHz PCM source (brickwall ~22 kHz)";
	else if (m.cutoff_hz>=23500 && m.cutoff_hz<26500) cls = "Likely 48 kHz PCM source (brickwall ~24 kHz)";
	else {
		if (m.noise_rise_db>6.0) cls = "Likely native DSD (rising ultrasonic noise 30-80 kHz)";
		else if (m.noise_floor_30_50>-60 && m.noise_rise_db>=-3 && m.noise_rise_db<=3)
			cls = "Likely Hi-Res PCM -> DSD (extended HF but flat ultrasonic noise)";
		else cls = "Inconclusive - could be carefully filtered hi-res or processed";
	}
	if (m.dr_like_db<=7.5 || m.crest_median_db<=7.0)
		cls += "; Note: probable heavy dynamic compression (loudness-war).";

	
	return cls; 
}

static void save_report(const std::string& path, const Options& opt, int sr, const Metrics& m, const std::string& cls){ std::ofstream f(path); f << "DSD Inspector Report\n"; f << "Input: " << opt.input << "\n"; f << "Resampled to: " << sr << " Hz mono\n"; f << "FFT: " << opt.fft_size << ", hop: " << opt.hop_size << ", analyzed seconds: " << opt.seconds << "\n\n"; f << "— Brickwall/cutoff: "; if(m.cutoff_hz>0) f << m.cutoff_hz << " Hz (drop " << m.cutoff_drop_db << " dB)\n"; else f << "none\n"; f << "— Noise floor 30–50 kHz: " << m.noise_floor_30_50 << " dBFS\n"; f << "— Noise floor 50–80 kHz: " << m.noise_floor_50_80 << " dBFS\n"; f << "— Ultrasonic noise rise (50–80 minus 30–50): " << m.noise_rise_db << " dB\n"; f << "— Crest factor (median): " << m.crest_median_db << " dB\n"; f << "— DR-like metric: " << m.dr_like_db << " dB\n\n"; f << "Classification: " << cls << "\n"; }

// ----------------- Pretty renderers -----------------
//static void save_pretty_spectrogram(const SpectralOutputs& so, int sr, const std::string& path){
    //int W = so.spec_w, H = so.spec_h; if(W<=0||H<=0) return; int PADL=60, PADB=40, PADR=10, PADT=10; ImageRGB im(W+PADL+PADR,H+PADT+PADB); im.clear(10,10,10);
    //for(int x=0;x<W;++x){ 
		//for(int y=0;y<H;++y){ 
			//unsigned char g = so.spectrogram_png[y*W + x];
			//// brighten midtones: gamma 0.75
			//double t = std::pow(g / 255.0, 0.75);
			//unsigned char r,gc,b;
			//colormap_turbo(t, r, gc, b);
			//im.set(PADL+x, PADT+y, r, gc, b);
		//}}
    //std::vector<int> gridk={1,2,5,10,15,20,25,30,35,40}; auto xmap=[&](double f){ double t = std::log10(std::max(20.0,f)/20.0)/std::log10((sr*0.5)/20.0); return PADL + (int)std::round(t*W); };
    //for(int k: gridk){ int x=xmap(k*1000.0); for(int y=PADT;y<PADT+H;++y) im.set(x,y,40,40,40);
		//draw_text(im,x-10,H+PADT+5,std::to_string(k)+"k",180,180,180); 
	//}
    //auto ymap=[&](double db){ double t=(db+120.0)/120.0; return PADT + (int)std::round((1.0-t)*H); };
    //for(int db=-108; db<=0; db+=12){ int y=ymap(db); for(int x=PADL;x<PADL+W;++x) im.set(x,y,40,40,40); draw_text(im,5,y-3,std::to_string(db),180,180,180); }
    //draw_text(im, (PADL+W)/2 - 10, PADT+H+20, "Hz", 220,220,220); draw_text(im, 5, PADT-5, "dB", 220,220,220);
    //write_png_rgb(path.c_str(), im.w, im.h, im.px);
//}

static void save_pretty_spectrogram(const SpectralOutputs& so, int sr, const std::string& path){
    int W = so.spec_w, H = so.spec_h;
    if (W <= 0 || H <= 0) return;

    int PADL = 60, PADB = 40, PADR = 10, PADT = 10;
    ImageRGB im(W + PADL + PADR, H + PADT + PADB);
    im.clear(10, 10, 10);

    // draw colormapped spectrogram (brighten with gamma)
    for (int x = 0; x < W; ++x) {
        for (int y = 0; y < H; ++y) {
            unsigned char g = so.spectrogram_png[y * W + x];
            double t = std::pow(g / 255.0, 0.75);   // brighten
            unsigned char r, gc, b;
            colormap_turbo(t, r, gc, b);
            im.set(PADL + x, PADT + y, r, gc, b);
        }
    }

    // grid (log-f on x)
    std::vector<int> gridk = {1,2,5,10,15,20,25,30,35,40};
    auto xmap = [&](double f){
        double t = std::log10(std::max(20.0, f)/20.0) / std::log10((sr*0.5)/20.0);
        return PADL + (int)std::round(t * W);
    };
    for (int k : gridk) {
        int x = xmap(k * 1000.0);
        for (int y = PADT; y < PADT + H; ++y) im.set(x, y, 40, 40, 40);
        // big tick label on x
		// x tick
		draw_text_scaled_nn(im, x - 12, PADT + H + 10,
							ascii_sanitize(std::to_string(k) + "k"),
							200, 200, 200, 2);
    }

    // y grid (dB)
    auto ymap = [&](double db){
        double t = (db + 120.0) / 120.0;
        return PADT + (int)std::round((1.0 - t) * H);
    };
    for (int db = -108; db <= 0; db += 12) {
        int y = ymap(db);
        for (int x = PADL; x < PADL + W; ++x) im.set(x, y, 40, 40, 40);
        // big tick label on y
		// y tick
		draw_text_scaled_nn(im, 8, y - 5,
							ascii_sanitize(std::to_string(db)),
							200, 200, 200, 2);
    }

	// axis labels
	draw_text_scaled_nn(im, (PADL + W) / 2 - 10, PADT + H + 20, ascii_sanitize("Hz"), 220,220,220, 2);
	draw_text_scaled_nn(im, 8,                   PADT -  2,     ascii_sanitize("dB"), 220,220,220, 2);

    write_png_rgb(path.c_str(), im.w, im.h, im.px);
}
static void draw_text_bold(ImageRGB& im, int x, int y, const std::string& s,
                           unsigned char r, unsigned char g, unsigned char b,
                           int thickness = 2)
{
    for (int dx = 0; dx < thickness; ++dx)
        for (int dy = 0; dy < thickness; ++dy)
            draw_text(im, x + dx, y + dy, s, r, g, b);
}





static void save_pretty_spectrum_overlay(const SpectralOutputs& L, const SpectralOutputs& R,
                                         int sr, const Metrics& m, const std::string& cls,
                                         const std::string& path)
{

	const std::string TITLE = ascii_sanitize("Spectrum (log-f) - L (magenta) / R (cyan)");
	const std::string BAND  = ascii_sanitize("DSD noise-shaping band");
	std::string cls_ascii   = ascii_sanitize(cls);

    const int W=1400, H=700;
    ImageRGB im(W,H); im.clear(0,0,0);

    const int PADL=70, PADB=60, PADR=15, PADT=15;
    const int PW=W-PADL-PADR, PH=H-PADT-PADB;

    // text scales (NN upscaled for larger labels)
    const int SCALE_TITLE = 2;
    const int SCALE_AXIS  = 2;
    const int MIN_DX_LABEL = 34;  // min x-spacing between x-axis labels (pixels)

    auto xmap=[&](double f){
        double t = std::log10(std::max(20.0, f)/20.0) / std::log10((sr*0.5)/20.0);
        return PADL + (int)std::round(t*PW);
    };
    auto ymap=[&](double db){
        double t = (db+120.0)/120.0;
        return PADT + (int)std::round((1.0-t)*PH);
    };

    // ----- horizontal grid + dB tick labels
    for(int db=-108; db<=0; db+=12){
        int y=ymap(db);
        for(int x=PADL; x<PADL+PW; ++x) im.set(x,y,40,40,40);
        draw_text(im, 8, y-5, ascii_sanitize(std::to_string(db)), 200,200,200);  // small font = crisp, no scaling
    }

    // ----- vertical grid on log-f; auto-thin labels
    std::vector<double> xticks;
    double ny = sr * 0.5;
    for(double decade=1e3; decade<=ny*1.01; decade*=10.0){
        for(double m : {1.0,2.0,5.0}){
            double f = m*decade;
            if(f<=ny*1.001) xticks.push_back(f);
        }
    }
    xticks.push_back(22050.0);
    xticks.push_back(24000.0);
    std::sort(xticks.begin(), xticks.end());
    xticks.erase(std::unique(xticks.begin(), xticks.end()), xticks.end());

    int last_label_x = -100000;
    auto fmt_khz = [](double f){
        if (f >= 1000.0) { int k = (int)std::round(f/1000.0); return std::to_string(k) + "k"; }
        return std::to_string((int)std::round(f));
    };

    for(double f : xticks){
        int x = xmap(f);
        for(int y=PADT; y<PADT+PH; ++y) im.set(x,y,40,40,40); // grid line

        if (x - last_label_x >= MIN_DX_LABEL) {
            std::string lab = ascii_sanitize(fmt_khz(f));
            int lab_w = (int)lab.size() * 6;  // small font width
            int lx = x - lab_w/2;
            if (lx < 2) lx = 2;
            if (lx + lab_w > W-2) lx = W-2 - lab_w;
            draw_text(im, lx, PADT+PH+10, lab, 200,200,200);
            last_label_x = x;
        }
    }

    // ----- shade the DSD noise-shaping band (22.05k..50k)
int x22 = xmap(22050.0), x50 = xmap(50000.0);
int xL = std::max(0, std::min(x22, W-1));
int xR = std::max(0, std::min(x50, W-1));
if (xL > xR) std::swap(xL, xR);

for (int x = xL; x <= xR; ++x){
    for (int y = PADT; y < PADT + PH; ++y){
        int o = (y*W + x) * 3;                // now guaranteed in range
        unsigned char r = im.px[o+0];
        unsigned char g = im.px[o+1];
        unsigned char b = im.px[o+2];
        im.set(x, y,
               (unsigned char)((r*3+20)/4),
               (unsigned char)((g*3+10)/4),
               (unsigned char)((b*3+ 0)/4));
    }
}

    // band label
    draw_text_scaled_nn(im, x22-280, PADT+11, BAND, 180,200,255, 2);
    
//// Center the band label horizontally between xL..xR, and 10 px lower than before
//const int SCALE_BAND = 2;
//int band_text_w = (int)BAND.size() * 6 * SCALE_BAND;   // 6 px per char at 1x
//int band_x = (xL + xR - band_text_w) / 2;              // center inside shaded band
//band_x = std::max(PADL + 4, std::min(band_x, W - 4 - band_text_w));
//int band_y = PADT + 15;                                 // was PADT + 5 → 10 px lower
//draw_text_scaled_nn(im, band_x, band_y, BAND, 180,200,255, SCALE_BAND);


    // ----- draw curves
    auto draw_curve=[&](const SpectralOutputs& S,unsigned char r,unsigned char g,unsigned char b){
        int prevx=-1, prevy=-1;
        for(size_t i=1;i<S.freq.size();++i){
            int x=xmap(S.freq[i]);
            int y=ymap(S.avg_mag_db[i]);
            if(prevx>=0){
                int dx=std::abs(x-prevx), sx=prevx<x?1:-1;
                int dy=-std::abs(y-prevy), sy=prevy<y?1:-1;
                int err=dx+dy; int x0=prevx,y0=prevy;
                while(true){
                    im.set(x0,y0,r,g,b);
                    if(x0==x && y0==y) break;
                    int e2=2*err;
                    if(e2>=dy){ err+=dy; x0+=sx; }
                    if(e2<=dx){ err+=dx; y0+=sy; }
                }
            }
            prevx=x; prevy=y;
        }
    };
    draw_curve(L,255,100,220); // magenta-ish (Left)
    draw_curve(R,120,200,255); // cyan-ish (Right)

    // ----- special markers at 22.05k & 24k
    for(double mk : {22050.0, 24000.0}){
        int x = xmap(mk);
        for(int y=PADT; y<PADT+PH; ++y) im.set(x,y,100,100,100);
        std::string lab = (mk==22050.0) ? "22.05k" : "24k";
        int lab_w = (int)lab.size()*6;
        int lx = std::min(std::max(x+3, 2), W-2-lab_w);
        // special markers
        std::string lab2 = ascii_sanitize(mk==22050.0 ? "22.05k" : "24k");
        draw_text(im, lx, (mk==22050.0)?(PADT+15):(PADT+28), lab2, 200,200,200);
    }

		// title / axes / classification
	draw_text_scaled_nn(im, PADL,  6,    TITLE,    230,230,230, 2);
	draw_text_scaled_nn(im, PADL,  H-24, ascii_sanitize("Hz"),  220,220,220, 2);
	draw_text_scaled_nn(im, 8,     PADT-2, ascii_sanitize("dB"),220,220,220, 2);
	draw_text_scaled_nn(im, std::max(PADL, W - 10 - (int)cls_ascii.size()*6*2),
                    6, cls_ascii, 255,210,120, 2);


    write_png_rgb(path.c_str(), W, H, im.px);
}


static SpectralOutputs compute_avg_spectrum_of_channel(const std::vector<float>& stereo, int sr, int Nfft, int hop, int ch){ std::vector<float> x; x.reserve(stereo.size()/2); for(size_t i=ch;i<stereo.size(); i+=2) x.push_back(stereo[i]); return compute_spectrum_and_spectrogram(x, sr, Nfft, hop, ""); }

// ----------------- main -----------------
int main(int argc, char** argv){
    av_log_set_level(AV_LOG_ERROR);
    Options opt; bool pretty=true;
    for(int i=1;i<argc;++i){ std::string a=argv[i];
        if(a=="-i" && i+1<argc){ opt.input=argv[++i]; }
        else if(a=="--out" && i+1<argc){ opt.outdir=argv[++i]; }
        else if(a=="--sr" && i+1<argc){ opt.target_sr=std::stoi(argv[++i]); }
        else if(a=="--fft" && i+1<argc){ opt.fft_size=std::stoi(argv[++i]); }
        else if(a=="--hop" && i+1<argc){ opt.hop_size=std::stoi(argv[++i]); }
        else if(a=="--sec" && i+1<argc){ opt.seconds=std::stoi(argv[++i]); }
        else if(a=="--no-pretty"){ pretty=false; }
        else { if(a=="-h"||a=="--help"){ usage(); return 0; } }
    }
    if(opt.input.empty()){ usage(); return 1; }

    std::filesystem::create_directories(opt.outdir);

    try{
        int sr=0; auto mono = decode_to_mono(opt, sr);
        auto so = compute_spectrum_and_spectrogram(mono, sr, opt.fft_size, opt.hop_size, opt.outdir);
        save_spectrogram_png(so, opt.outdir+"/spectrogram.png");
        save_average_spectrum_png(so, opt.outdir+"/spectrum_avg.png");
        Metrics m = analyze_metrics(so); compute_dynamic_metrics(mono, sr, m);
        auto cls = classify(m); save_report(opt.outdir+"/report.txt", opt, sr, m, cls);

        if(pretty){ int sr2=0; auto stereo = decode_to_stereo(opt, sr2); auto soL = compute_avg_spectrum_of_channel(stereo, sr2, opt.fft_size, opt.hop_size, 0); auto soR = compute_avg_spectrum_of_channel(stereo, sr2, opt.fft_size, opt.hop_size, 1); save_pretty_spectrogram(so, sr2, opt.outdir+"/spectrogram_pretty.png"); save_pretty_spectrum_overlay(soL, soR, sr2, m, cls, opt.outdir+"/spectrum_overlay.png"); }

        std::cout << "Done. Wrote:\n " << opt.outdir << "/spectrogram.png\n " << opt.outdir << "/spectrum_avg.png\n " << opt.outdir << "/report.txt\n"; if(pretty){ std::cout << "  " << opt.outdir << "/spectrogram_pretty.png\n " << opt.outdir << "/spectrum_overlay.png\n"; }
        return 0;
    } catch(const std::exception& e){ std::cerr << "Error: " << e.what() << "\n"; return 2; }
}
