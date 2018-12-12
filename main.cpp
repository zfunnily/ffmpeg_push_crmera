extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavdevice/avdevice.h"
#include "libavutil/time.h"
};
#include <stddef.h>
#include <stdint.h>

int flush_encoder(AVFormatContext *ifmt_ctx, AVFormatContext *ofmt_ctx, unsigned int stream_index, int framecnt);

void show_dshow_device()
{
	AVFormatContext *pFmtCtx = avformat_alloc_context();
	AVDeviceInfoList *device_info = NULL;
	AVDictionary* options = NULL;
	av_dict_set(&options, "list_devices", "true", 0);
	AVInputFormat *iformat = av_find_input_format("dshow");
	printf("Device Info=============\n");
	avformat_open_input(&pFmtCtx, "video=dummy", iformat, &options);
	printf("========================\n");

}

int main ()
{
	avdevice_register_all();
	avformat_network_init();

	
	av_register_all();
	//Register Device
	avdevice_register_all();
	avformat_network_init();

	//Show Dshow Device  
	show_dshow_device();
	const char* out_path = "rtmp://192.168.1.22:1935/live/test";

	char capture_name[100] = {0},device_name[100] ={0};

	printf("\nChoose capture device: ");
	if (gets(capture_name) == 0)
	{
		printf("Error in gets()\n");
		return -1;
	}
	sprintf(device_name, "video=%s", capture_name);
	AVInputFormat* ifmt = av_find_input_format("dshow");//根据名称查找链表当中的AVInputFormat
	AVFormatContext * ifmt_ctx = avformat_alloc_context();

	//=================
	//1.avformat_open_input()该函数用于打开多媒体数据
	//set own video device name
	//2.avcodec_open2初始化一个视音频编解码器的AVCodecContext
	//3.vcodec_find_encoder()和avcodec_find_decoder()。
	//avcodec_find_encoder()用于查找FFmpeg的编码器，avcodec_find_decoder()用于查找FFmpeg的解码器
	//=================
	AVDictionary *device_param = 0;
	if (avformat_open_input(&ifmt_ctx,device_name,ifmt,&device_param) != 0) {  
		printf("Couldn't open input stream.（无法打开输入流）\n");
		return -1;
	}
	
	//input initialize
	if (avformat_find_stream_info(ifmt_ctx,NULL) < 0) {  //获取流信息
		printf("Couldn't find stream information.（无法获取流信息）\n");
		return -1;
	}
	int videoindex = -1;
	for (int i =0; i < ifmt_ctx->nb_streams; ++i) {
		if (ifmt_ctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			videoindex = i;
			break;
		}
	}
	if (videoindex == -1) {
		printf("Couldn't find a video stream.（没有找到视频流）\n");
		return -1;
	}
	if (avcodec_open2(ifmt_ctx->streams[videoindex]->codec,avcodec_find_decoder(ifmt_ctx->streams[videoindex]->codec->codec_id),NULL) < 0) {
		printf("Could not open codec.（无法打开解码器）\n");
		return -1;
	}

	//==============================
	//output initialize
	/*ffmpeg将网络协议和文件同等看待，同时因为使用RTMP协议进行传输，这里我们指定输出为flv格式，编码器使用H.264*/
	//==============================
	AVFormatContext* ofmt_ctx = avformat_alloc_context();
	AVOutputFormat *ofmt = NULL;
	avformat_alloc_output_context2(&ofmt_ctx,NULL,"flv",out_path);

	//output encode initialize 
	AVCodec* pCodec  = avcodec_find_encoder(AV_CODEC_ID_H264);
	if (!pCodec) {
		printf("Can not find encoder! (没有找到合适的编码器！)\n");
		return -1;
	}
	AVCodecContext * pCodecCtx = avcodec_alloc_context3(pCodec); //初始化
	pCodecCtx->pix_fmt = PIX_FMT_YUV420P;
	pCodecCtx->width = ifmt_ctx->streams[videoindex]->codec->width;
	pCodecCtx->height = ifmt_ctx->streams[videoindex]->codec->height;
	pCodecCtx->time_base.num = 1;
	pCodecCtx->time_base.den = 25;
	pCodecCtx->bit_rate = 40000;
	pCodecCtx->gop_size = 250;

	ofmt = ofmt_ctx->oformat;
	if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
		pCodecCtx->flags |= CODEC_FLAG_GLOBAL_HEADER;
	}
	pCodecCtx->qmin = 10;
	pCodecCtx->qmax = 51;
	pCodecCtx->max_b_frames = 3;
	AVDictionary * param = 0;
	av_dict_set(&param,"preset", "fast", 0);//Set the given entry in *pm, overwriting an existing entry(覆盖现有输入).
	av_dict_set(&param,"tune","zerolatency",0);

	if (avcodec_open2(pCodecCtx,pCodec,&param) < 0) {
		printf("Failed to open encoder! (编码器打开失败！)\n");
		return -1;
	}
	AVStream* video_st = avformat_new_stream(ofmt_ctx,pCodec);//将新流添加到媒体文件。
	if (video_st == NULL) {
				return -1;
	}
	video_st->time_base.num = 1;
	video_st->time_base.den = 25;
	video_st->codec  = pCodecCtx;
	if (avio_open(&ofmt_ctx->pb,out_path,AVIO_FLAG_READ_WRITE) < 0) {  //创建并初始化AVIOContext以访问url指示的资源。
		printf("Failed to open output file! (输出文件打开失败！)\n");
		return -1;
	}

	int  ret;
	av_dump_format(ofmt_ctx,0,out_path,1);
	ret = avformat_write_header(ofmt_ctx,NULL);
	if (ret < 0) {
		printf( "Error occurred when opening output URL\n");
		goto end;
	}

	AVPacket *dec_pkt, enc_pkt;
	 dec_pkt = (AVPacket *)av_malloc(sizeof(AVPacket));

	 	//camera data may has a pix fmt of RGB or sth else,convert it to YUV420
	struct SwsContext * img_convert_ctx = sws_getContext(ifmt_ctx->streams[videoindex]->codec->width, ifmt_ctx->streams[videoindex]->codec->height,
		 ifmt_ctx->streams[videoindex]->codec->pix_fmt, pCodecCtx->width, pCodecCtx->height, PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

	//Initialize the buffer to store YUV frames to be encoded.
	AVFrame *pFrameYUV = av_frame_alloc();
	uint8_t *out_buffer = (uint8_t *)av_malloc(avpicture_get_size(PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height));
	avpicture_fill((AVPicture *)pFrameYUV, out_buffer, PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height);
	 printf("\n --------call started----------\n");



	//start push
	int frame_index = 0;
	int start_time=av_gettime();
	AVFrame *pframe;
	int dec_got_frame, enc_got_frame;
	int framecnt = 0;
	AVRational time_base_q = { 1, AV_TIME_BASE };
	int vid_next_pts = 0;
	int aud_next_pts = 0;
	while (1) {
		if ((ret=av_read_frame(ifmt_ctx, dec_pkt)) >= 0){

			av_log(NULL, AV_LOG_DEBUG, "Going to reencode the frame\n");
			pframe = av_frame_alloc();
			if (!pframe) {
				ret = AVERROR(ENOMEM);
				return ret;
			}
			ret = avcodec_decode_video2(ifmt_ctx->streams[dec_pkt->stream_index]->codec, pframe,
				&dec_got_frame, dec_pkt);
			if (ret < 0) {
				av_frame_free(&pframe);
				av_log(NULL, AV_LOG_ERROR, "Decoding failed\n");
				break;
			}
			if (dec_got_frame){
				sws_scale(img_convert_ctx, (const uint8_t* const*)pframe->data, pframe->linesize, 0, pCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);
				pFrameYUV->width = pframe->width;
				pFrameYUV->height = pframe->height;
				pFrameYUV->format = PIX_FMT_YUV420P;

				enc_pkt.data = NULL;
				enc_pkt.size = 0;
				av_init_packet(&enc_pkt);
				ret = avcodec_encode_video2(pCodecCtx, &enc_pkt, pFrameYUV, &enc_got_frame);
				av_frame_free(&pframe);
				if (enc_got_frame == 1){
					//printf("Succeed to encode frame: %5d\tsize:%5d\n", framecnt, enc_pkt.size);
					framecnt++;
					enc_pkt.stream_index = video_st->index;						

					//Write PTS
					AVRational time_base = ofmt_ctx->streams[0]->time_base;//{ 1, 1000 };
					AVRational r_framerate1 = ifmt_ctx->streams[videoindex]->r_frame_rate;//{ 50, 2 }; 
					//Duration between 2 frames (us)
					int64_t calc_duration = (double)(AV_TIME_BASE)*(1 / av_q2d(r_framerate1));	//内部时间戳
					//Parameters
					//enc_pkt.pts = (double)(framecnt*calc_duration)*(double)(av_q2d(time_base_q)) / (double)(av_q2d(time_base));
					enc_pkt.pts = av_rescale_q(framecnt*calc_duration, time_base_q, time_base);
					enc_pkt.dts = enc_pkt.pts;
					enc_pkt.duration = av_rescale_q(calc_duration, time_base_q, time_base); //(double)(calc_duration)*(double)(av_q2d(time_base_q)) / (double)(av_q2d(time_base));
					enc_pkt.pos = -1;
					//printf("video pts : %d\n", enc_pkt.pts);

					vid_next_pts=framecnt*calc_duration; //general timebase

					//Delay
					int64_t pts_time = av_rescale_q(enc_pkt.pts, time_base, time_base_q);
					int64_t now_time = av_gettime() - start_time;						
					if ((pts_time > now_time) && ((vid_next_pts + pts_time - now_time)<aud_next_pts))
						av_usleep(pts_time - now_time);

					ret = av_interleaved_write_frame(ofmt_ctx, &enc_pkt);
					av_free_packet(&enc_pkt);
				}
			}else {
				av_frame_free(&pframe);
			}
		av_free_packet(dec_pkt);
		}
	}
		//Flush Encoder
		ret = flush_encoder(ifmt_ctx, ofmt_ctx, 0, framecnt);
		if (ret < 0) {
			printf("Flushing encoder failed\n");
			return -1;
		}

		//Write file trailer
		av_write_trailer(ofmt_ctx);

end:
	avformat_close_input(&ifmt_ctx);
	/* close output */
	if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
		avio_close(ofmt_ctx->pb);
	avformat_free_context(ofmt_ctx);
	if (ret < 0 && ret != AVERROR_EOF) {
		printf( "Error occurred.\n");
		return -1;
	}
	return 0;
}

int flush_encoder(AVFormatContext *ifmt_ctx, AVFormatContext *ofmt_ctx, unsigned int stream_index, int framecnt){
	int ret;
	int got_frame;
	AVPacket enc_pkt;
	if (!(ofmt_ctx->streams[stream_index]->codec->codec->capabilities &
		CODEC_CAP_DELAY))
		return 0;
	while (1) {
		enc_pkt.data = NULL;
		enc_pkt.size = 0;
		av_init_packet(&enc_pkt);
		ret = avcodec_encode_video2(ofmt_ctx->streams[stream_index]->codec, &enc_pkt,
			NULL, &got_frame);
		av_frame_free(NULL);
		if (ret < 0)
			break;
		if (!got_frame){
			ret = 0;
			break;
		}
		printf("Flush Encoder: Succeed to encode 1 frame!\tsize:%5d\n", enc_pkt.size);
		framecnt++;
		//Write PTS
		AVRational time_base = ofmt_ctx->streams[stream_index]->time_base;//{ 1, 1000 };
		AVRational r_framerate1 = ifmt_ctx->streams[0]->r_frame_rate;// { 50, 2 };
		AVRational time_base_q = { 1, AV_TIME_BASE };
		//Duration between 2 frames (us)
		int64_t calc_duration = (double)(AV_TIME_BASE)*(1 / av_q2d(r_framerate1));	//内部时间戳
		//Parameters
		enc_pkt.pts = av_rescale_q(framecnt*calc_duration, time_base_q, time_base);
		enc_pkt.dts = enc_pkt.pts;
		enc_pkt.duration = av_rescale_q(calc_duration, time_base_q, time_base);

		/* copy packet*/
		//转换PTS/DTS（Convert PTS/DTS）
		enc_pkt.pos = -1;

		//ofmt_ctx->duration = enc_pkt.duration * framecnt;

		/* mux encoded frame */
		ret = av_interleaved_write_frame(ofmt_ctx, &enc_pkt);
		if (ret < 0)
			break;
	}
	return ret;
}