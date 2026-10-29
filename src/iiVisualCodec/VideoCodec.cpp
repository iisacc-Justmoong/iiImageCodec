#include "VideoCodec.h"
#include "Visual_p.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/display.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>
#include <libavutil/spherical.h>
#include <libavutil/stereo3d.h>
#include <libswscale/swscale.h>
}
#include <climits>
#include <array>
#include <cmath>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <numbers>
#include <string_view>
#include <utility>

namespace ii::visual::video {
namespace {
auto failure(std::string message,int code=0) {
    if(code<0){char text[AV_ERROR_MAX_STRING_SIZE]{};av_strerror(code,text,sizeof(text));message+=" : ";message+=text;}
    return detail::error(image::ErrorCode::codec_failure,std::move(message));
}
bool safe_license(){
    const std::string license=avcodec_license(),config=avcodec_configuration();
    return license.find("LGPL")!=std::string::npos && config.find("--enable-gpl")==std::string::npos && config.find("--enable-nonfree")==std::string::npos;
}
struct Input final {
    std::span<const std::uint8_t> bytes;std::size_t position=0;
    AVFormatContext* format=nullptr;AVIOContext* io=nullptr;
    ~Input(){if(format)avformat_close_input(&format);if(io){av_freep(&io->buffer);avio_context_free(&io);}}
    static int read(void* opaque,std::uint8_t* data,int count){
        auto& self=*static_cast<Input*>(opaque);if(self.position==self.bytes.size())return AVERROR_EOF;
        const auto size=std::min<std::size_t>(count,self.bytes.size()-self.position);
        std::memcpy(data,self.bytes.data()+self.position,size);self.position+=size;return int(size);
    }
    static std::int64_t seek(void* opaque,std::int64_t offset,int whence){
        auto& self=*static_cast<Input*>(opaque);if(whence==AVSEEK_SIZE)return std::int64_t(self.bytes.size());
        whence&=~AVSEEK_FORCE;
        std::int64_t base=whence==SEEK_SET?0:whence==SEEK_CUR?std::int64_t(self.position):whence==SEEK_END?std::int64_t(self.bytes.size()):-1;
        if(base<0 || offset < -base || offset>std::int64_t(self.bytes.size())-base)return AVERROR(EINVAL);
        self.position=std::size_t(base+offset);return std::int64_t(self.position);
    }
};
int deny_open(AVFormatContext*,AVIOContext**,const char*,int,AVDictionary**){return AVERROR(EPERM);}
image::Result<std::unique_ptr<Input>> open(std::span<const std::uint8_t> bytes,Limits limits){
    if(!safe_license())return detail::error(image::ErrorCode::unsupported_format,"Video backend requires a GPL/nonfree-disabled LGPL FFmpeg build");
    if(bytes.empty())return detail::error(image::ErrorCode::invalid_input,"Empty video input");
    if(bytes.size()>limits.input_bytes || bytes.size()>std::size_t(INT64_MAX))return detail::error(image::ErrorCode::limit_exceeded,"Compressed video exceeds input budget");
    auto input=std::make_unique<Input>();input->bytes=bytes;
    auto* buffer=static_cast<std::uint8_t*>(av_malloc(32768));if(!buffer)return failure("Cannot allocate video input buffer");
    input->io=avio_alloc_context(buffer,32768,0,input.get(),Input::read,nullptr,Input::seek);
    if(!input->io){av_free(buffer);return failure("Cannot allocate video input context");}
    input->format=avformat_alloc_context();if(!input->format)return failure("Cannot allocate demuxer");
    input->format->pb=input->io;input->format->flags|=AVFMT_FLAG_CUSTOM_IO;input->format->io_open=deny_open;
    input->format->probesize=std::min<std::int64_t>(std::int64_t(bytes.size()),5*1024*1024);
    input->format->max_analyze_duration=AV_TIME_BASE;
    input->format->max_streams=32;
    AVDictionary* dict=nullptr;
    av_dict_set(&dict,"format_whitelist","matroska,webm,mov,avi",0);
    av_dict_set(&dict,"protocol_whitelist","",0);
    auto code=avformat_open_input(&input->format,nullptr,nullptr,&dict);av_dict_free(&dict);
    if(code<0)return failure("Cannot open bounded video input",code);
    code=avformat_find_stream_info(input->format,nullptr);if(code<0)return failure("Cannot inspect video streams",code);
    return input;
}
Result<Info> information(Input& input,Limits limits,int& index){
    index=av_find_best_stream(input.format,AVMEDIA_TYPE_VIDEO,-1,-1,nullptr,0);
    if(index<0)return detail::error(image::ErrorCode::unsupported_format,"Input has no decodable video stream");
    auto* stream=input.format->streams[index];auto* par=stream->codecpar;
    if(par->width<=0 || par->height<=0 || std::uint64_t(par->width)>limits.image.dimension ||
       std::uint64_t(par->height)>limits.image.dimension || std::uint64_t(par->width)*par->height>limits.image.pixel_bytes/4)
        return detail::error(image::ErrorCode::limit_exceeded,"Video dimensions exceed pixel budget");
    Info info;info.width=par->width;info.height=par->height;
    info.time_base={stream->time_base.num,stream->time_base.den};
    auto rate=av_guess_frame_rate(input.format,stream,nullptr);info.frame_rate={rate.num,rate.den};
    info.duration=stream->duration==AV_NOPTS_VALUE?0:stream->duration;
    info.codec=avcodec_get_name(par->codec_id);
    if(auto* side=av_packet_side_data_get(par->coded_side_data,par->nb_coded_side_data,AV_PKT_DATA_STEREO3D);side && side->size>=sizeof(AVStereo3D)){
        auto* stereo=reinterpret_cast<const AVStereo3D*>(side->data);
        if(stereo->flags&AV_STEREO3D_FLAG_INVERT)return detail::error(image::ErrorCode::unsupported_format,"Inverted stereo eye order is unsupported");
        if(stereo->type==AV_STEREO3D_SIDEBYSIDE)info.spatial.stereo=spatial::StereoLayout::side_by_side;
        else if(stereo->type==AV_STEREO3D_TOPBOTTOM)info.spatial.stereo=spatial::StereoLayout::top_bottom;
        else if(stereo->type!=AV_STEREO3D_2D)return detail::error(image::ErrorCode::unsupported_format,"Unsupported stereo packing");
    }
    if(auto* side=av_packet_side_data_get(par->coded_side_data,par->nb_coded_side_data,AV_PKT_DATA_SPHERICAL);side && side->size>=sizeof(AVSphericalMapping)){
        auto* sphere=reinterpret_cast<const AVSphericalMapping*>(side->data);
        if(sphere->projection!=AV_SPHERICAL_EQUIRECTANGULAR)return detail::error(image::ErrorCode::unsupported_format,"Unsupported spherical projection");
        info.spatial.projection=spatial::Projection::equirectangular;
        // FFmpeg stores Euler angles as signed 16.16 degrees. Convert the
        // yaw/pitch/roll orientation to a camera-to-world quaternion.
        const double factor=std::numbers::pi/(180.0*65536.0*2);
        const double y=sphere->yaw*factor,p=sphere->pitch*factor,r=sphere->roll*factor;
        const double cy=std::cos(y),sy=std::sin(y),cp=std::cos(p),sp=std::sin(p),cr=std::cos(r),sr=std::sin(r);
        info.spatial.pose.rotation={cy*sp*cr+sy*cp*sr,sy*cp*cr-cy*sp*sr,cy*cp*sr-sy*sp*cr,cy*cp*cr+sy*sp*sr};
    }
    if(auto* entry=av_dict_get(stream->metadata,"stereo_mode",nullptr,0)){
        const std::string value=entry->value;
        if(value=="left_right")info.spatial.stereo=spatial::StereoLayout::side_by_side;
        else if(value=="top_bottom")info.spatial.stereo=spatial::StereoLayout::top_bottom;
    }
    return info;
}
struct Context final{AVCodecContext* ptr=nullptr;~Context(){avcodec_free_context(&ptr);}};
struct Packet final{AVPacket* ptr=av_packet_alloc();~Packet(){av_packet_free(&ptr);}};
struct AvFrame final{AVFrame* ptr=av_frame_alloc();~AvFrame(){av_frame_free(&ptr);}};
struct Scaler final{SwsContext* ptr=nullptr;~Scaler(){sws_freeContext(ptr);}};
struct Output final {
    AVFormatContext* format=nullptr;AVIOContext* io=nullptr;std::vector<std::uint8_t> bytes;std::size_t position=0,limit=0;bool exceeded=false;
    ~Output(){if(format)avformat_free_context(format);if(io){av_freep(&io->buffer);avio_context_free(&io);}}
    static int write(void* opaque,const std::uint8_t* data,int count){
        auto& self=*static_cast<Output*>(opaque);
        if(count<0 || std::size_t(count)>self.limit-self.position){self.exceeded=true;return AVERROR(ENOSPC);}
        try{const auto end=self.position+std::size_t(count);if(end>self.bytes.size())self.bytes.resize(end);std::memcpy(self.bytes.data()+self.position,data,count);self.position=end;return count;}
        catch(...){return AVERROR(ENOMEM);}
    }
    static std::int64_t seek(void* opaque,std::int64_t offset,int whence){
        auto& self=*static_cast<Output*>(opaque);if(whence==AVSEEK_SIZE)return std::int64_t(self.bytes.size());whence&=~AVSEEK_FORCE;
        const std::int64_t base=whence==SEEK_SET?0:whence==SEEK_CUR?std::int64_t(self.position):whence==SEEK_END?std::int64_t(self.bytes.size()):-1;
        if(base<0 || offset< -base || offset>std::int64_t(self.limit)-base)return AVERROR(EINVAL);
        self.position=std::size_t(base+offset);return std::int64_t(self.position);
    }
};
AVCodecID codec_id(Codec codec){
    switch(codec){case Codec::ffv1:return AV_CODEC_ID_FFV1;case Codec::mpeg4:return AV_CODEC_ID_MPEG4;case Codec::mjpeg:return AV_CODEC_ID_MJPEG;
    case Codec::h264:return AV_CODEC_ID_H264;case Codec::hevc:return AV_CODEC_ID_HEVC;case Codec::vp9:return AV_CODEC_ID_VP9;case Codec::av1:return AV_CODEC_ID_AV1;}
    return AV_CODEC_ID_NONE;
}
const char* container_name(Container c){switch(c){case Container::matroska:return "matroska";case Container::mp4:return "mp4";case Container::mov:return "mov";case Container::avi:return "avi";}return nullptr;}
std::optional<AVRational> constant_rate(std::span<const FrameView> frames,Rational time_base){
    const auto duration=frames.front().duration;
    for(std::size_t i=0;i<frames.size();++i){
        if(frames[i].duration!=duration || (i && frames[i].pts-frames[i-1].pts!=duration))return {};
    }
    if(duration>INT64_MAX/time_base.numerator)return {};
    int numerator=0,denominator=0;
    if(!av_reduce(&numerator,&denominator,time_base.denominator,
                  std::int64_t(time_base.numerator)*duration,INT_MAX))return {};
    return AVRational{numerator,denominator};
}
Result<void> set_spatial(AVStream* stream,const spatial::Metadata& metadata){
    auto valid=spatial::validate(metadata);if(!valid)return valid;
    if(metadata.pose.translation!=std::array<double,3>{} || metadata.pose.rotation!=std::array<double,4>{0,0,0,1})
        return detail::error(image::ErrorCode::unsupported_format,"Video export supports identity spatial pose; transform the view before encoding");
    if(metadata.stereo!=spatial::StereoLayout::mono){
        auto* side=av_packet_side_data_new(&stream->codecpar->coded_side_data,&stream->codecpar->nb_coded_side_data,AV_PKT_DATA_STEREO3D,sizeof(AVStereo3D),0);
        if(!side)return failure("Cannot allocate stereo metadata");
        auto* stereo=reinterpret_cast<AVStereo3D*>(side->data);*stereo={};
        stereo->type=metadata.stereo==spatial::StereoLayout::side_by_side?AV_STEREO3D_SIDEBYSIDE:AV_STEREO3D_TOPBOTTOM;
        // Packing is carried by the Matroska stereo mode / MP4 st3d box.
        // An additional Apple "eyes" box describes native views, not this
        // packing, and FFmpeg would replace st3d with an unspecified layout.
        stereo->view=AV_STEREO3D_VIEW_UNSPEC;
        av_dict_set(&stream->metadata,"stereo_mode",metadata.stereo==spatial::StereoLayout::side_by_side?"left_right":"top_bottom",0);
    }
    if(metadata.projection==spatial::Projection::equirectangular){
        auto* side=av_packet_side_data_new(&stream->codecpar->coded_side_data,&stream->codecpar->nb_coded_side_data,AV_PKT_DATA_SPHERICAL,sizeof(AVSphericalMapping),0);
        if(!side)return failure("Cannot allocate spherical metadata");
        auto* sphere=reinterpret_cast<AVSphericalMapping*>(side->data);*sphere={};sphere->projection=AV_SPHERICAL_EQUIRECTANGULAR;
    }
    return {};
}
}
BackendInfo backend_info(){return {av_version_info(),avcodec_license(),avcodec_configuration()};}
Result<Info> inspect(std::span<const std::uint8_t> bytes,Limits limits)try{
    auto input=open(bytes,limits);if(!input)return std::unexpected(input.error());int index=0;return information(**input,limits,index);
}catch(const std::bad_alloc&){return detail::error(image::ErrorCode::limit_exceeded,"Video inspection allocation failed");}
Result<DisplayInfo> inspect_display(std::span<const std::uint8_t> bytes,Limits limits)try{
    auto input=open(bytes,limits);if(!input)return std::unexpected(input.error());
    int index=0;auto info=information(**input,limits,index);if(!info)return std::unexpected(info.error());
    auto* format=(*input)->format;auto* stream=format->streams[index];auto* parameters=stream->codecpar;
    DisplayInfo result;result.video=std::move(*info);
    const auto aspect=av_guess_sample_aspect_ratio(format,stream,nullptr);
    if(aspect.num>0 && aspect.den>0)result.sample_aspect_ratio={aspect.num,aspect.den};
    if(format->iformat && format->iformat->name)result.container=format->iformat->name;
    if(parameters->format>=0)if(const auto* value=av_get_pix_fmt_name(AVPixelFormat(parameters->format)))result.pixel_format=value;
    for(unsigned i=0;i<format->nb_streams;++i)
        if(format->streams[i]->codecpar && format->streams[i]->codecpar->codec_type==AVMEDIA_TYPE_AUDIO)++result.audio_stream_count;
    if(const auto* tag=av_dict_get(stream->metadata,"rotate",nullptr,0);tag && tag->value){
        std::size_t length=0;
        while(length<=128 && tag->value[length]!='\0')++length;
        if(length>128)return detail::error(image::ErrorCode::limit_exceeded,"Video rotation tag exceeds its byte budget");
        result.rotation_tag.assign(tag->value,length);
    }
    if(const auto* side=av_packet_side_data_get(parameters->coded_side_data,parameters->nb_coded_side_data,AV_PKT_DATA_DISPLAYMATRIX)){
        if(side->size<sizeof(std::int32_t)*result.display_matrix.size())
            return detail::error(image::ErrorCode::invalid_input,"Video display matrix is truncated");
        std::memcpy(result.display_matrix.data(),side->data,sizeof(std::int32_t)*result.display_matrix.size());
        result.rotation_degrees=av_display_rotation_get(result.display_matrix.data());
        if(!std::isfinite(result.rotation_degrees))return detail::error(image::ErrorCode::invalid_input,"Video display matrix is singular");
        result.has_display_matrix=true;
    }
    return result;
}catch(const std::bad_alloc&){return detail::error(image::ErrorCode::limit_exceeded,"Video display inspection allocation failed");}
Result<TimedVideo> decode_with_timing(std::span<const std::uint8_t> bytes,Limits limits,
                                     const std::function<bool()>& cancelled)try{
    auto cancelled_error=[](){return detail::error(image::ErrorCode::cancelled,"Video decoding was cancelled");};
    if(cancelled && cancelled())return cancelled_error();
    auto input=open(bytes,limits);if(!input)return std::unexpected(input.error());int index=0;auto info=information(**input,limits,index);if(!info)return std::unexpected(info.error());
    if(cancelled && cancelled())return cancelled_error();
    auto* stream=(*input)->format->streams[index];auto* codec=avcodec_find_decoder(stream->codecpar->codec_id);
    std::array<std::int32_t,9> stream_matrix{65536,0,0,0,65536,0,0,0,1073741824};
    constexpr std::size_t matrix_bytes=9*sizeof(std::int32_t);
    const auto* stream_side=av_packet_side_data_get(stream->codecpar->coded_side_data,
        stream->codecpar->nb_coded_side_data,AV_PKT_DATA_DISPLAYMATRIX);
    const bool stream_matrix_valid=!stream_side || (stream_side->data && stream_side->size>=matrix_bytes);
    if(stream_side && stream_matrix_valid)std::memcpy(stream_matrix.data(),stream_side->data,matrix_bytes);
    if(!codec)return detail::error(image::ErrorCode::unsupported_format,"No decoder for video codec");
    Context context{avcodec_alloc_context3(codec)};if(!context.ptr)return failure("Cannot allocate decoder");
    auto code=avcodec_parameters_to_context(context.ptr,stream->codecpar);if(code<0)return failure("Cannot configure decoder",code);
    context.ptr->thread_count=1;context.ptr->max_pixels=limits.image.pixel_bytes/4;context.ptr->err_recognition=AV_EF_CAREFUL|AV_EF_EXPLODE;
    code=avcodec_open2(context.ptr,codec,nullptr);if(code<0)return failure("Cannot open decoder",code);
    Packet packet;AvFrame frame;Scaler scaler;if(!packet.ptr || !frame.ptr)return failure("Cannot allocate decoded frame");
    TimedVideo timed{{*info,{}},false,false};auto& result=timed.video;std::size_t decoded_size=0,packets=0;
    auto receive=[&]()->Result<void>{
        for(;;){
            if(cancelled && cancelled())return cancelled_error();
            auto status=avcodec_receive_frame(context.ptr,frame.ptr);if(status==AVERROR(EAGAIN)||status==AVERROR_EOF)return {};
            if(status<0)return failure("Cannot decode frame",status);
            if(frame.ptr->width<=0 || frame.ptr->height<=0 || std::uint64_t(frame.ptr->width)>limits.image.dimension ||
               std::uint64_t(frame.ptr->height)>limits.image.dimension || std::uint64_t(frame.ptr->width)*frame.ptr->height>limits.image.pixel_bytes/4)
                return detail::error(image::ErrorCode::limit_exceeded,"Decoded frame exceeds dimensions");
            const auto size=std::size_t(frame.ptr->width)*frame.ptr->height*4;
            if(result.frames.size()>=limits.frames || size>limits.decoded_bytes-decoded_size)
                return detail::error(image::ErrorCode::limit_exceeded,"Video frame count or decoded byte budget exceeded");
            if(frame.ptr->flags&AV_FRAME_FLAG_CORRUPT)return failure("Corrupt decoded video frame");
            if(const auto* side=av_frame_get_side_data(frame.ptr,AV_FRAME_DATA_DISPLAYMATRIX)){
                if(!stream_matrix_valid || !side->data || side->size<matrix_bytes ||
                   std::memcmp(side->data,stream_matrix.data(),matrix_bytes)!=0)
                    timed.frame_display_matrix_differs=true;
            }
            image::Image image{std::uint32_t(frame.ptr->width),std::uint32_t(frame.ptr->height),std::vector<std::uint8_t>(size)};
            scaler.ptr=sws_getCachedContext(scaler.ptr,frame.ptr->width,frame.ptr->height,AVPixelFormat(frame.ptr->format),frame.ptr->width,frame.ptr->height,AV_PIX_FMT_RGBA,SWS_BILINEAR,nullptr,nullptr,nullptr);
            if(!scaler.ptr)return failure("Unsupported decoded pixel format");
            std::uint8_t* output[]{image.rgba.data()};int strides[]{frame.ptr->width*4};
            if(sws_scale(scaler.ptr,frame.ptr->data,frame.ptr->linesize,0,frame.ptr->height,output,strides)!=frame.ptr->height)return failure("Cannot convert decoded frame");
            auto pts=frame.ptr->best_effort_timestamp;
            if(pts==AV_NOPTS_VALUE){
                timed.used_fallback_timestamps=true;
                if(result.frames.empty())pts=0;
                else{
                    const auto step=std::max<std::int64_t>(1,result.frames.back().duration);
                    if(result.frames.back().pts>INT64_MAX-step)
                        return detail::error(image::ErrorCode::invalid_input,"Synthesized video timestamp overflows");
                    pts=result.frames.back().pts+step;
                }
            }
            result.frames.push_back({std::move(image),pts,frame.ptr->duration});decoded_size+=size;av_frame_unref(frame.ptr);
        }
    };
    for(;;){
        if(cancelled && cancelled())return cancelled_error();
        code=av_read_frame((*input)->format,packet.ptr);if(code<0)break;
        if(++packets>limits.packets)return detail::error(image::ErrorCode::limit_exceeded,"Demuxed packet budget exceeded");
        if(packet.ptr->stream_index==index){
            if(packet.ptr->flags&AV_PKT_FLAG_CORRUPT)return failure("Corrupt encoded video packet");
            auto status=avcodec_send_packet(context.ptr,packet.ptr);if(status<0)return failure("Cannot submit video packet",status);
            auto received=receive();if(!received)return std::unexpected(received.error());
        }
        av_packet_unref(packet.ptr);
    }
    if(code!=AVERROR_EOF)return failure("Video demuxing failed",code);
    code=avcodec_send_packet(context.ptr,nullptr);if(code<0)return failure("Cannot flush video decoder",code);
    auto received=receive();if(!received)return std::unexpected(received.error());
    if(cancelled && cancelled())return cancelled_error();
    if(result.frames.empty())return failure("Video contains no complete frames");
    return timed;
}catch(const std::bad_alloc&){return detail::error(image::ErrorCode::limit_exceeded,"Video decoding allocation failed");}
 catch(const std::exception& error){return detail::error(image::ErrorCode::codec_failure,error.what());}
 catch(...){return detail::error(image::ErrorCode::codec_failure,"Video decode callback failed");}
Result<Video> decode(std::span<const std::uint8_t> bytes,Limits limits){
    auto timed=decode_with_timing(bytes,limits);
    if(!timed)return std::unexpected(std::move(timed.error()));
    return std::move(timed->video);
}
Result<std::vector<std::uint8_t>> encode(std::span<const FrameView> frames,EncodeOptions options,Limits limits)try{
    if(!safe_license())return detail::error(image::ErrorCode::unsupported_format,"Video backend requires an LGPL build without GPL/nonfree options");
    if(frames.empty() || options.time_base.numerator<=0 || options.time_base.denominator<=0 || options.bit_rate<=0 || !container_name(options.container))
        return detail::error(image::ErrorCode::invalid_input,"Invalid video frames, time base, bitrate or container");
    if(frames.size()>limits.frames || limits.output_bytes>std::size_t(INT64_MAX))return detail::error(image::ErrorCode::limit_exceeded,"Video output or frame count exceeds budget");
    const auto width=frames[0].image.width,height=frames[0].image.height;std::size_t total=0;
    for(std::size_t i=0;i<frames.size();++i){
        auto step=detail::stride(frames[i].image,limits.image);if(!step)return std::unexpected(step.error());
        const auto size=std::size_t(width)*height*4;
        if(size>limits.decoded_bytes-total)return detail::error(image::ErrorCode::limit_exceeded,"Input video pixels exceed byte budget");total+=size;
        if(frames[i].image.width!=width || frames[i].image.height!=height || frames[i].pts<0 || frames[i].duration<=0 ||
           frames[i].pts>INT64_MAX-frames[i].duration || (i && frames[i].pts<=frames[i-1].pts))
            return detail::error(image::ErrorCode::invalid_input,"Frames require fixed dimensions and increasing nonnegative timestamps");
    }
    auto valid=spatial::validate(options.spatial);if(!valid)return std::unexpected(valid.error());
    const auto eye_width=width/(options.spatial.stereo==spatial::StereoLayout::side_by_side?2:1);
    const auto eye_height=height/(options.spatial.stereo==spatial::StereoLayout::top_bottom?2:1);
    if((options.spatial.stereo==spatial::StereoLayout::side_by_side && width%2) || (options.spatial.stereo==spatial::StereoLayout::top_bottom && height%2) ||
       (options.spatial.projection==spatial::Projection::equirectangular && std::uint64_t(eye_height)*2!=eye_width))
        return detail::error(image::ErrorCode::invalid_input,"Invalid packed stereo or panorama aspect ratio");
    if(options.container==Container::avi && (options.spatial.stereo!=spatial::StereoLayout::mono || options.spatial.projection!=spatial::Projection::rectilinear))
        return detail::error(image::ErrorCode::unsupported_format,"AVI cannot carry the spatial metadata contract");
    if(options.container==Container::mov && options.spatial.stereo!=spatial::StereoLayout::mono)
        return detail::error(image::ErrorCode::unsupported_format,"Packed stereo export requires MP4 or Matroska; MOV native view metadata does not retain the packing layout");
    const auto id=codec_id(options.codec);
    const auto* codec=options.codec==Codec::h264?avcodec_find_encoder_by_name("libopenh264"):nullptr;
    if(!codec)codec=avcodec_find_encoder(id);
    if(!codec)return detail::error(image::ErrorCode::unsupported_format,"Requested encoder is unavailable in the selected backend");
    if(std::string_view(codec->name)=="libopenh264" && (width<16 || height<16))
        return detail::error(image::ErrorCode::invalid_input,"The OpenH264 encoder requires width and height of at least 16 pixels");
    Output output;output.limit=limits.output_bytes;
    auto code=avformat_alloc_output_context2(&output.format,nullptr,container_name(options.container),nullptr);if(code<0 || !output.format)return failure("Cannot create video container",code);
    // FFmpeg gates the established Spherical Video v2 st3d/sv3d MP4 boxes
    // behind its unofficial-extensions mode. Without this, the muxer silently
    // omits the metadata even when the codec parameters contain it.
    if(options.container==Container::mp4 && (options.spatial.stereo!=spatial::StereoLayout::mono || options.spatial.projection!=spatial::Projection::rectilinear))
        output.format->strict_std_compliance=FF_COMPLIANCE_UNOFFICIAL;
    if(avformat_query_codec(output.format->oformat,id,FF_COMPLIANCE_NORMAL)==0)return detail::error(image::ErrorCode::unsupported_format,"Codec is unsupported in the requested container");
    auto* buffer=static_cast<std::uint8_t*>(av_malloc(32768));if(!buffer)return failure("Cannot allocate encoded output");
    output.io=avio_alloc_context(buffer,32768,1,&output,nullptr,Output::write,Output::seek);if(!output.io){av_free(buffer);return failure("Cannot allocate output context");}
    output.format->pb=output.io;output.format->flags|=AVFMT_FLAG_CUSTOM_IO;output.format->io_open=deny_open;
    auto* stream=avformat_new_stream(output.format,nullptr);if(!stream)return failure("Cannot allocate encoded stream");
    Context context{avcodec_alloc_context3(codec)};if(!context.ptr)return failure("Cannot allocate encoder");
    context.ptr->width=width;context.ptr->height=height;context.ptr->time_base={options.time_base.numerator,options.time_base.denominator};
    const auto rate=constant_rate(frames,options.time_base);
    context.ptr->framerate=rate.value_or(AVRational{options.time_base.denominator,options.time_base.numerator});context.ptr->bit_rate=options.bit_rate;
    context.ptr->thread_count=1;context.ptr->gop_size=12;context.ptr->max_b_frames=0;
    if(std::string_view(codec->name)=="libopenh264")context.ptr->profile=AV_PROFILE_H264_BASELINE;
    context.ptr->pix_fmt=options.codec==Codec::ffv1?AV_PIX_FMT_BGRA:options.codec==Codec::mjpeg?AV_PIX_FMT_YUVJ420P:AV_PIX_FMT_YUV420P;
    if(context.ptr->pix_fmt==AV_PIX_FMT_YUV420P || context.ptr->pix_fmt==AV_PIX_FMT_YUVJ420P){if(width%2 || height%2)return detail::error(image::ErrorCode::invalid_input,"4:2:0 encoders require even dimensions");}
    if(output.format->oformat->flags&AVFMT_GLOBALHEADER)context.ptr->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;
    code=avcodec_open2(context.ptr,codec,nullptr);if(code<0)return failure("Cannot open video encoder",code);
    stream->time_base=context.ptr->time_base;if(rate)stream->avg_frame_rate=*rate;
    code=avcodec_parameters_from_context(stream->codecpar,context.ptr);if(code<0)return failure("Cannot export codec parameters",code);
    auto metadata=set_spatial(stream,options.spatial);if(!metadata)return std::unexpected(metadata.error());
    code=avformat_write_header(output.format,nullptr);if(code<0)return output.exceeded?detail::error(image::ErrorCode::limit_exceeded,"Encoded output exceeds byte budget"):failure("Cannot write video header",code);
    Packet packet;AvFrame frame;Scaler scaler;if(!packet.ptr || !frame.ptr)return failure("Cannot allocate encoder frame");
    frame.ptr->width=width;frame.ptr->height=height;frame.ptr->format=context.ptr->pix_fmt;
    code=av_frame_get_buffer(frame.ptr,32);if(code<0)return failure("Cannot allocate encoder pixels",code);
    auto drain=[&]()->Result<void>{for(;;){
        auto status=avcodec_receive_packet(context.ptr,packet.ptr);if(status==AVERROR(EAGAIN)||status==AVERROR_EOF)return {};
        if(status<0)return failure("Cannot receive encoded packet",status);
        if(packet.ptr->duration<=0){
            auto found=std::lower_bound(frames.begin(),frames.end(),packet.ptr->pts,[](const FrameView& frame,std::int64_t pts){return frame.pts<pts;});
            packet.ptr->duration=found!=frames.end() && found->pts==packet.ptr->pts?found->duration:1;
        }
        av_packet_rescale_ts(packet.ptr,context.ptr->time_base,stream->time_base);packet.ptr->stream_index=stream->index;
        status=av_interleaved_write_frame(output.format,packet.ptr);av_packet_unref(packet.ptr);
        if(status<0)return output.exceeded?detail::error(image::ErrorCode::limit_exceeded,"Encoded output exceeds byte budget"):failure("Cannot write encoded packet",status);
    }};
    for(const auto& source:frames){
        code=av_frame_make_writable(frame.ptr);if(code<0)return failure("Cannot reuse encoder frame",code);
        auto step=detail::stride(source.image,limits.image);if(!step)return std::unexpected(step.error());
        if(*step>INT_MAX)return detail::error(image::ErrorCode::limit_exceeded,"Video input stride exceeds backend range");
        const auto format=source.image.format==image::PixelFormat::rgb8?AV_PIX_FMT_RGB24:AV_PIX_FMT_RGBA;
        scaler.ptr=sws_getCachedContext(scaler.ptr,width,height,format,width,height,context.ptr->pix_fmt,SWS_BILINEAR,nullptr,nullptr,nullptr);
        if(!scaler.ptr)return failure("Cannot create encoder colour converter");
        const std::uint8_t* data[]{source.image.pixels.data()};int strides[]{int(*step)};
        if(sws_scale(scaler.ptr,data,strides,0,height,frame.ptr->data,frame.ptr->linesize)!=int(height))return failure("Cannot convert encoder pixels");
        frame.ptr->pts=source.pts;frame.ptr->duration=source.duration;
        code=avcodec_send_frame(context.ptr,frame.ptr);if(code<0)return failure("Cannot submit encoder frame",code);
        auto drained=drain();if(!drained)return std::unexpected(drained.error());
    }
    code=avcodec_send_frame(context.ptr,nullptr);if(code<0)return failure("Cannot flush video encoder",code);
    auto drained=drain();if(!drained)return std::unexpected(drained.error());
    code=av_write_trailer(output.format);avio_flush(output.io);
    if(output.exceeded)return detail::error(image::ErrorCode::limit_exceeded,"Encoded output exceeds byte budget");
    if(code<0 || output.io->error<0)return failure("Cannot finish video container",code<0?code:output.io->error);
    return std::move(output.bytes);
}catch(const std::bad_alloc&){return detail::error(image::ErrorCode::limit_exceeded,"Video encoding allocation failed");}
}
