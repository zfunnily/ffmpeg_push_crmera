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
	if (avformat_open_input(&ifmt_ctx,device_name,ifmt,NULL) != 0) {  
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
	const char* out_path = "rtmp://192.168.1.22/live/test";
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

	//start push
	AVPacket pkt;
	int frame_index=0;
	int64_t start_time=av_gettime();
	while(1) {
		AVStream *in_stream, *out_stream;
		//Get an AVPacket
		ret = av_read_frame(ifmt_ctx, &pkt);
		if (ret < 0)
			break;
		//FIX：No PTS (Example: Raw H.264)
		//Simple Write PTS
		if(pkt.pts==AV_NOPTS_VALUE){
			//Write PTS
			AVRational time_base1=ifmt_ctx->streams[videoindex]->time_base;
			//Duration between 2 frames (us)
			int64_t calc_duration=(double)AV_TIME_BASE/av_q2d(ifmt_ctx->streams[videoindex]->r_frame_rate);
			//Parameters
			pkt.pts=(double)(frame_index*calc_duration)/(double)(av_q2d(time_base1)*AV_TIME_BASE);
			pkt.dts=pkt.pts;
			pkt.duration=(double)calc_duration/(double)(av_q2d(time_base1)*AV_TIME_BASE);
		}
		//Important:Delay
		if(pkt.stream_index==videoindex){
			AVRational time_base=ifmt_ctx->streams[videoindex]->time_base;
			AVRational time_base_q={1,AV_TIME_BASE};
			int64_t pts_time = av_rescale_q(pkt.dts, time_base, time_base_q);
			int64_t now_time = av_gettime() - start_time;
			if (pts_time > now_time)
				av_usleep(pts_time - now_time);
		}in_stream  = ifmt_ctx->streams[pkt.stream_index];
		out_stream = ofmt_ctx->streams[pkt.stream_index];
		/* copy packet */
		//Convert PTS/DTS
		pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
		pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AVRounding)(AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
		pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
		pkt.pos = -1;
		//Print to Screen
		if(pkt.stream_index==videoindex){
			printf("Send %8d video frames to output URL\n",frame_index);
			frame_index++;
		}
		//ret = av_write_frame(ofmt_ctx, &pkt);
		ret = av_interleaved_write_frame(ofmt_ctx, &pkt);

		if (ret < 0) {
			printf( "Error muxing packet\n");
			break;
		}

		av_free_packet(&pkt);

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