#include "video_reader.hpp"

bool video_reader_open(VideoReaderState* state, const char* filename) {
    avdevice_register_all();
    
    // Unpack members of state
    auto& width = state->width;
    auto& height = state->height;
    auto& time_base = state->time_base;
    auto& av_format_ctx = state->av_format_ctx;
    auto& av_codec_ctx = state->av_codec_ctx;
    auto& video_stream_index = state->video_stream_index;
    auto& av_frame = state->av_frame;
    auto& av_packet = state->av_packet;
    
    // Open the file using libavformat
    av_format_ctx = avformat_alloc_context();
    if (!av_format_ctx) {
        printf("Couldn't creted AVFormatContext\n");
        return false;
    }
    
    AVInputFormat* av_input_format = av_find_input_format("avfoundation");
    if (!av_input_format) {
        printf("Couldn't find AVFoundation input format to get webcam\n");
        return false;
    }
    
    // record webcam
//    AVDictionary* options = NULL;
//    av_dict_set(&options, "framerate", "25", 0);
//    av_dict_set(&options, "video_size", "1280x720", 0);
//    av_dict_set(&options, "pixel_fmt", "rgb0", 0);
//
//    if (avformat_open_input(&av_format_ctx, "default:none", av_input_format, &options) != 0) {
//        printf("Couldn't open video file\n");
//        return false;
//    }
    
    // open local file
    if (avformat_open_input(&av_format_ctx, filename, NULL, NULL) != 0) {
        printf("Couldn't open video file\n");
        return false;
    }
    
    // Find the first valid video stream inside the file
    video_stream_index = -1;
    AVCodecParameters* av_codec_params;
    AVCodec* av_codec;
    
    for (int i = 0; i < av_format_ctx->nb_streams; i++) {
        av_codec_params = av_format_ctx->streams[i]->codecpar;
        av_codec = avcodec_find_decoder(av_codec_params->codec_id);
        
        if (!av_codec) {
            continue;
        }
        if (av_codec_params->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            width = av_codec_params->width;
            height = av_codec_params->height;
            time_base = av_format_ctx->streams[i]->time_base;
            break;
        }
    }
    
    if (video_stream_index == -1) {
        printf("Couldn't find valid video stream inside file\n");
        return false;
    }
    
    // Set up a codec context for the decoder
    av_codec_ctx = avcodec_alloc_context3(av_codec);
    if (!av_codec_ctx) {
        printf("Couldn't create AVCodecContext\n");
        return false;
    }
    
    if (avcodec_parameters_to_context(av_codec_ctx, av_codec_params) < 0) {
        printf("Couldn't initialize AVCodecContext\n");
        return false;
    }
    
    if (avcodec_open2(av_codec_ctx, av_codec, NULL) < 0) {
        printf("Couldn't open codec\n");
        return false;
    }
    
    av_frame = av_frame_alloc();
    if (!av_frame) {
        printf("Couldn't allocate AVFrame\n");
        return false;
    }
    av_packet = av_packet_alloc();
    if (!av_packet) {
        printf("Couldn't allocate AVPacket\n");
        return false;
    }

    return true;
}
    
bool video_reader_read_frame(VideoReaderState* state, uint8_t* frame_bufer, int64_t* pts) {
    // Unpack members of state
    auto& width = state->width;
    auto& height = state->height;
    auto& av_format_ctx = state->av_format_ctx;
    auto& av_codec_ctx = state->av_codec_ctx;
    auto& video_stream_index = state->video_stream_index;
    auto& av_frame = state->av_frame;
    auto& av_packet = state->av_packet;
    auto& sws_scaler_ctx = state->sws_scaler_ctx;
    
    // Decode one frame
    int res;
    while (av_read_frame(av_format_ctx, av_packet) >= 0) {
        if (av_packet->stream_index != video_stream_index) {
            av_packet_unref(av_packet);
            continue;
        }
        
        res = avcodec_send_packet(av_codec_ctx, av_packet);
        if (res < 0) {
            printf("Failed to decode packet: %s\n", av_err2str(res));
            return false;
        }
        
        res = avcodec_receive_frame(av_codec_ctx, av_frame);
        if (res == AVERROR(EAGAIN) || res == AVERROR_EOF) {
            av_packet_unref(av_packet);
            continue;
        } else if (res < 0) {
            printf("Failed to decode frame: %s\n", av_err2str(res));
            return false;
        }
        
        av_packet_unref(av_packet);
        break;
    }
    
    *pts = av_frame->pts;
    
    printf("Frame %c (%d) pts %d dts %d key_frame %d [coded_picture_number %d, display_picture_number %d]\n",
           av_get_picture_type_char(av_frame->pict_type),
           av_codec_ctx->frame_number,
           av_frame->pts,
           av_frame->pkt_dts,
           av_frame->key_frame,
           av_frame->coded_picture_number,
           av_frame->display_picture_number
    );

    // Set up sws scaler
    sws_scaler_ctx = sws_getContext(width, height, av_codec_ctx->pix_fmt,
                                    width, height, AV_PIX_FMT_RGB0,
                                    SWS_BILINEAR, NULL, NULL, NULL);
    if (!sws_scaler_ctx) {
        printf("Couldn't initialize sw scaler\n");
        return false;
    }

    uint8_t* dest[4] = { frame_bufer, NULL, NULL, NULL };
    int dest_linesize[4] = { width * 4, 0, 0, 0 };
    sws_scale(sws_scaler_ctx, av_frame->data, av_frame->linesize, 0, av_frame->height, dest, dest_linesize);
    
    return true;
}

void video_reader_seek_frame(VideoReaderState* state, int64_t ts) {
    av_seek_frame(state->av_format_ctx, state->video_stream_index, ts, 0);
}

void video_reader_close(VideoReaderState* state) {
    avformat_close_input(&state->av_format_ctx);
    avformat_free_context(state->av_format_ctx);
    av_frame_free(&state->av_frame);
    av_packet_free(&state->av_packet);
    avcodec_free_context(&state->av_codec_ctx);
    sws_freeContext(state->sws_scaler_ctx);
}
