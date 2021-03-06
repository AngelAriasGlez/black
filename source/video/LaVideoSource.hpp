#ifndef __LAVIDEOSOURCE_H
#define __LAVIDEOSOURCE_H

#include <iostream>

extern "C"{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
#include <libavutil/display.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}
#include "utils/utils.hpp"

#if defined(__APPLE__)
#define AV_NOPTS_VALUE_ ((int64_t)0x8000000000000000LL)
#else
#define AV_NOPTS_VALUE_ ((int64_t)AV_NOPTS_VALUE)
#endif

#include <chrono>
#include <vector>
#include <algorithm>

#include "VideoSource.hpp"

using namespace std::chrono;
class Frame {
public:
	uint8_t *data;
	Frame(uint8_t *buffer, int width, int height, int channels=4) {
		data = new uint8_t[width*height*channels];
		memcpy(data, buffer, width*height*channels * sizeof(uint8_t));
	}
	~Frame() {
		delete data;
	}
};


//new api https://blogs.gentoo.org/lu_zero/2016/03/29/new-avcodec-api/


class LaVideoSource :public VideoSource{
public:
	AVFormatContext* inctx = nullptr;
	AVCodec* vcodec = nullptr;
	int vstrm_idx;
	AVStream* vstrm;
	int astrm_idx;
	AVStream* astrm;
	SwsContext* swsctx;
	uint8_t *dst_data[4];
	int dst_linesize[4];
	int dst_bufsize;
	AVFrame* frame;
	int64_t picture_pts;

	double eps_zero = 0.000025;

	int dst_width;
	int dst_height;

	AVFrame* decframe;
	int64_t frame_number = 0;
	AVPacket pkt;

	bool reverse = false;

	std::string url;

	AVCodecContext * codec_ctx;

	/*AVFilterContext *fbuffersrc_ctx;
	AVFilterContext *fbuffersink_ctx;
	AVFilterContext *frotate_ctx;
	AVFilterContext *fformat_ctx;

	AVFilterGraph *filter_graph;*/
	double rotation;

	LaVideoSource() {
		av_register_all();
        frame = nullptr;
        decframe = nullptr;
        swsctx = nullptr;
        inctx = nullptr;
        vstrm = nullptr;
        pkt.buf = nullptr;
	}
	LaVideoSource(std::string infile) {
		load(infile);
	}
	std::string getUrl() {
		return url;
	}

    virtual void load (std::string infile) override{
		url = infile;

		picture_pts = AV_NOPTS_VALUE_;


		unload(); 




		//av_log_set_level(AV_LOG_DEBUG);
		int ret;
		inctx = nullptr;
		ret = avformat_open_input(&inctx, infile.c_str(), nullptr, nullptr);
		if (ret < 0) {
			std::cerr << "fail to avforamt_open_input(\"" << infile.c_str() << "\"): ret=" << ret;
			return;
		}
		ret = avformat_find_stream_info(inctx, nullptr);
		if (ret < 0) {
			std::cerr << "fail to avformat_find_stream_info: ret=" << ret;
			return;
		}
		ret = av_find_best_stream(inctx, AVMEDIA_TYPE_VIDEO, -1, -1, &vcodec, 0);
		if (ret < 0) {
			std::cerr << "fail to av_find_best_stream: ret=" << ret;
			return;
		}

		vstrm_idx = ret;
		vstrm = inctx->streams[vstrm_idx];
		codec_ctx = vstrm->codec;
		//codec_ctx->skip_frame = AVDISCARD_NONKEY;
		//codec_ctx->skip_loop_filter = AVDISCARD_ALL;
		//codec_ctx->skip_idct = AVDISCARD_ALL;

		ret = avcodec_open2(codec_ctx, vcodec, nullptr);
		if (ret < 0) {
			std::cerr << "fail to avcodec_open2: ret=" << ret;
			return;
		}

		std::cerr << "gopsize=" << codec_ctx->gop_size << ret;
		

		/*ret = av_find_best_stream(inctx, AVMEDIA_TYPE_AUDIO, -1, -1, &vcodec, 0);
		if (ret < 0) {
			std::cerr << "fail to av_find_best_stream: ret=" << ret;
			return;
		}
		astrm_idx = ret;
		astrm = inctx->streams[astrm_idx];
		ret = avcodec_open2(codec_ctx, vcodec, nullptr);
		if (ret < 0) {
			std::cerr << "fail to avcodec_open2: ret=" << ret;
			return;
		}*/
		//AVDictionaryEntry *tag = NULL;
		//while ((tag = av_dict_get(inctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
		         //printf("%s=%s\n", tag->key, tag->value);


		// print input video stream informataion
		/*std::cout
			<< "infile: " << infile.c_str() << "\n"
			<< "format: " << inctx->iformat->name << "\n"
			<< "vcodec: " << vcodec->name << "\n"
			<< "size:   " << codec_ctx->width << 'x' << codec_ctx->height << "\n"
			<< "fps:    " << av_q2d(vstrm->avg_frame_rate) << " [fps]\n"
			<< "length: " << getLengthMs()/1000. << " [sec]\n"
			<< "pixfmt: " << av_get_pix_fmt_name(codec_ctx->pix_fmt) << "\n"
			<< "frames:  " << getLength() << "\n"
			<< std::flush;*/
		

		// initialize sample scaler
		//dst_width = codec_ctx->width;
		//dst_height = codec_ctx->height;
		dst_width = Utils::min(codec_ctx->width, 720);
		dst_height = (double)codec_ctx->height * ((double)dst_width/ (double)codec_ctx->width);
		const AVPixelFormat dst_pix_fmt = AV_PIX_FMT_BGR24;
		swsctx = sws_getCachedContext(nullptr, codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt, dst_width, dst_height, dst_pix_fmt, SWS_BICUBIC, nullptr, nullptr, nullptr);
		if (!swsctx) {
			std::cerr << "fail to sws_getCachedContext";
			return;
		}
		std::cout << "output: " << dst_width << 'x' << dst_height << ',' << av_get_pix_fmt_name(dst_pix_fmt) << std::endl;


		frame = av_frame_alloc();
		if ((ret = av_image_alloc(dst_data, dst_linesize, dst_width, dst_height, dst_pix_fmt, 1)) < 0) {
			std::cerr << "Could not allocate destination image";
		}
		dst_bufsize = ret;


		//av_image_fill_arrays(frame->data, frame->linesize, framebuf.data(), dst_pix_fmt, dst_width, dst_height, 1);
		decframe = av_frame_alloc();



		// TAGS reading
		/*AVDictionaryEntry *tag = NULL;
		while (tag = av_dict_get(vstrm->metadata, "", tag, AV_DICT_IGNORE_SUFFIX))
		{
			printf("%s=%s\n", tag->key, tag->value);
		}*/
		
		rotation = get_rotation(vstrm);




       /*int n = 40;
        for(int i=0;i<n;i++){
            auto duration = std::chrono::system_clock::now().time_since_epoch();
            int rec = getLength()/n*i;
            seek(rec, false);
            int diff = rec - getPosition();
            int elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch() - duration).count();
            LOGE("time: %d ms, %d frames",  elapsed, diff);
        }
        

		for (auto f : buffer) {
			delete f;
		}
		buffer.clear();
        if(cache){
            seek(0);
            while(grabFrame()) {
                auto ptr = scale();
                buffer.push_back(new Frame(ptr, getWidth(), getHeight(), getChannels()));
            }
        }*/
		frame_number = 0;





		/*avfilter_register_all();
		filter_graph = avfilter_graph_alloc();



		//---------------Buffer
		char args[512];
		//buffer=width=320:height=240:pix_fmt=yuv410p:time_base=1/24:sar=1
		snprintf(args, sizeof(args),
			"width=%d:height=%d:pix_fmt=%d:time_base=%d/%d:sar=1/2",
			codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
			vstrm->time_base.num, vstrm->time_base.den, vstrm->sample_aspect_ratio.num, vstrm->sample_aspect_ratio.den);
		fbuffersrc_ctx = insert_filter2(nullptr, "buffer", args);
		

		auto prev = insert_filter2(fbuffersrc_ctx, "format", "pix_fmts=bgr24");

		if (fabs(rotation - 90) < 1.0) {
		ret = insert_filter("transpose", "clock");
		}
		else if (fabs(rotation - 180) < 1.0) {
		ret = insert_filter("hflip", NULL);
		if (ret >= 0) ret = insert_filter("vflip", NULL);
		}else if (fabs(rotation - 270) < 1.0) {
		ret = insert_filter("transpose", "cclock");
		}else if (fabs(rotation) > 1.0) {
		char rotate_buf[64];
		snprintf(rotate_buf, sizeof(rotate_buf), "%f*PI/180", rotation);
		ret = insert_filter("rotate", rotate_buf);
		}
		dst_width = codec_ctx->height;
		dst_height = codec_ctx->width;
		//prev = insert_filter2(prev, "transpose", "clock");


		//---------------SINK
		enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_BGR24, AV_PIX_FMT_NONE };
		fbuffersink_ctx = insert_filter2(prev, "buffersink", nullptr, pix_fmts);



		ret = avfilter_graph_config(filter_graph, NULL);
		if (ret < 0) {
			av_log(NULL, AV_LOG_ERROR, "error configuring the filter graph\n");

		}*/

	}

	/*int insert_filter(const char *filter_name, const char *args) {
		int ret = avfilter_graph_create_filter(&frotate_ctx, avfilter_get_by_name(filter_name), filter_name, args, NULL, filter_graph);
		if (ret < 0)
			return ret;
	}


	AVFilterContext * insert_filter2(AVFilterContext*prev_filter, const char *filter_name, const char *args, void *op = nullptr) {
		AVFilterContext *filter;
		int ret = avfilter_graph_create_filter(&filter, avfilter_get_by_name(filter_name), filter_name, args, op, filter_graph);
		if (ret < 0) return prev_filter;
		if (prev_filter != nullptr) {
			ret = avfilter_link(prev_filter, 0, filter, 0);
			if (ret < 0) return prev_filter;
		}
		return filter;
	}*/
	int getGopSize() {
		return codec_ctx->gop_size;
	}
	int isKeyFrame() {
		return decframe->key_frame == 1;
	}

	double getRotation() override{
		return rotation;
	}

	
	double get_rotation(AVStream *st)
	{
		uint8_t* displaymatrix = av_stream_get_side_data(st, AV_PKT_DATA_DISPLAYMATRIX, NULL);
		double theta = 0;
		if (displaymatrix)
			theta = -av_display_rotation_get((int32_t*)displaymatrix);

		theta -= 360 * floor(theta / 360 + 0.9 / 360);

		if (fabs(theta - 90 * round(theta / 90)) > 2)
			av_log(NULL, AV_LOG_WARNING, "Odd rotation angle.\n"
				"If you want to help, upload a sample "
				"of this file to ftp://upload.ffmpeg.org/incoming/ "
				"and contact the ffmpeg-devel mailing list. (ffmpeg-devel@ffmpeg.org)");

		return theta;
	}


	virtual void unload() {

		if (vstrm) {
			avcodec_close(codec_ctx);
		}

		if (inctx) {
			avformat_free_context(inctx);
			inctx = nullptr;
			vstrm = nullptr;
		}


		if(frame)
			av_frame_free(&frame);
		if (decframe)
			av_frame_free(&decframe);

		if(pkt.buf) av_free_packet(&pkt);

		if (swsctx) {
			sws_freeContext(swsctx);
			swsctx = nullptr;
		}

		first_frame_number = -1;
	}

	virtual ~LaVideoSource(){}
	uint64_t getLength() override{
		if (!vstrm) return 0;
		int64_t nbf = vstrm->nb_frames;
		if (nbf == 0)
		{
			nbf = (int64_t)floor((getLengthMs()/1000) * getFramerate() + 0.5);
		}
		return nbf;
	}
	double getLengthMs() {
		if (!vstrm) return 0;
		double sec = (double)inctx->duration / (double)AV_TIME_BASE;
		if (sec < eps_zero){
			sec = (double)vstrm->duration * av_q2d(vstrm->time_base);
		}
		if (sec < eps_zero){
			sec = (double)vstrm->duration * av_q2d(vstrm->time_base);
		}
		return sec*1000.;
	}


	double getFramerate() {
		if (!vstrm) return 0;
			double fps = av_q2d(vstrm->avg_frame_rate);

			if (fps < eps_zero)
			{
				fps = 1.0 / av_q2d(codec_ctx->time_base);
			}

			return fps;
	}
	unsigned short getChannels() { return 3; }


	int bufferLeft = 0;
	virtual uint8_t* read()  override {
		if (!vstrm) return nullptr;

        
		if (grabFrame()) {


			/*if (fbuffersrc_ctx) {

				// push the audio data from decoded frame into the filtergraph
				int err = av_buffersrc_write_frame(fbuffersrc_ctx, decframe);
				if (err < 0) {
					av_log(NULL, AV_LOG_ERROR, "error writing frame to buffersrc\n");
					return nullptr;
				}
				// pull filtered audio from the filtergraph
				for (;;) {
					int err = av_buffersink_get_frame(fbuffersink_ctx, decframe);
					if (err == AVERROR_EOF || err == AVERROR(EAGAIN))
						break;
					if (err < 0) {
						av_log(NULL, AV_LOG_ERROR, "error reading buffer from buffersink\n");
						return nullptr;
					}
				}
				dst_width = decframe->width;
				dst_height = decframe->height;
				return decframe->data[0];
			}*/
			return scale();
			
		}

		return nullptr;
	};
	virtual uint8_t* scale() {

		sws_scale(swsctx, decframe->data, decframe->linesize, 0, decframe->height, dst_data, dst_linesize);

		return dst_data[0];
	}

	bool grabFrame()
	{
		bool valid = false;
		int got_picture;

		int count_errs = 0;
		const int max_number_of_attempts = 1 << 9;

		//if (!ic || !video_st)  return false;

		if (vstrm->nb_frames > 0 &&
			frame_number > vstrm->nb_frames)
			return false;

		picture_pts = AV_NOPTS_VALUE_;


		// get the next frame
		while (!valid)
		{

			av_packet_unref(&pkt);


			int ret = av_read_frame(inctx, &pkt);
			if (ret == AVERROR(EAGAIN)) continue;

			/* else if (ret < 0) break; */

			if (pkt.stream_index != vstrm_idx)
			{
				av_packet_unref(&pkt);
				count_errs++;
				if (count_errs > max_number_of_attempts)
					break;
				continue;
			}

			avcodec_decode_video2(codec_ctx, decframe, &got_picture, &pkt);


			// Did we get a video frame?
			if (got_picture)
			{
				//picture_pts = decframe->time
				if (picture_pts == AV_NOPTS_VALUE_)
					picture_pts = decframe->pts != AV_NOPTS_VALUE_ && decframe->pts != 0 ? decframe->pts : decframe->pkt_dts;
					//picture_pts = av_frame_get_best_effort_timestamp(decframe);
				//std::cout << frame_number << '\r' << std::flush;  // dump progress
				frame_number++;
				valid = true;

				if (first_frame_number < 0) 
					first_frame_number = dts_to_frame_number(picture_pts);



			}
			else
			{
				count_errs++;
				if (count_errs > max_number_of_attempts)
					break;
			}
		}

		// return if we have a new picture or not
		return valid;
	}



	int64_t first_frame_number = -1;
	virtual void seek(uint64_t _frame_number, bool acurate = true) override
	{
		if (!vstrm) return;


		_frame_number = Utils::min(_frame_number, getLength());
		int delta = 16;

		// if we have not grabbed a single frame before first seek, let's read the first frame
		// and get some valuable information during the process
		if (first_frame_number < 0 && getLength() > 1)
			grabFrame();

		for (;;)
		{
			int64_t _frame_number_temp = Utils::max(_frame_number - delta, (int64_t)0);
			double sec = (double)_frame_number_temp / getFramerate();
			int64_t time_stamp = vstrm->start_time;
			double  time_base = av_q2d(vstrm->time_base);
			time_stamp += (int64_t)(sec / time_base + 0.5);
			//if (getLength() > 1) av_seek_frame(inctx, vstrm_idx, _frame_number -2, AVSEEK_FLAG_FRAME);
			if (getLength() > 1) av_seek_frame(inctx, vstrm_idx, time_stamp, AVSEEK_FLAG_BACKWARD);
			avcodec_flush_buffers(codec_ctx);
			if (_frame_number > 0)
			{
				grabFrame();

				if (_frame_number > 1)
				{
					frame_number = dts_to_frame_number(picture_pts) - first_frame_number;
					//printf("_frame_number = %d, frame_number = %d, delta = %d\n",
					       //(int)_frame_number, (int)frame_number, delta);
						   //printf("diff = %d\n", _frame_number - frame_number);

					if (!acurate) break;

					if (frame_number < 0 || frame_number > _frame_number - 1)
					{
						if (_frame_number_temp == 0 || delta >= INT_MAX / 4)
							break;
						delta = delta < 16 ? delta * 2 : delta * 3 / 2;
						continue;
					}
					while (frame_number < _frame_number - 1)
					{
						if (!grabFrame())
							break;
					}
					frame_number++;
					break;
				}
				else
				{
					frame_number = 1;
					break;
				}
			}
			else
			{
				frame_number = 0;
				break;
			}
		}
	};
	int64_t dts_to_frame_number(int64_t dts)
	{
		double sec = dts_to_sec(dts);
		return (int64_t)(getFramerate() * sec + 0.5);
	}
	double dts_to_sec(int64_t dts)
	{
		return (double)(dts - vstrm->start_time) *
			av_q2d(vstrm->time_base);
	}

	virtual uint64_t getPosition()  override { return frame_number; }
	virtual void setPosition(int position) {}


	uint64_t getWidth()  override {
		return dst_width;
	}
	uint64_t getHeight()  override {
		return dst_height;
	}




	static void  encodeToJpg(AVFrame *frame) {
		int ret;

		av_register_all();

		AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
		AVCodecContext* c = avcodec_alloc_context3(codec);


		//c->bit_rate = frame->width*frame->height*3;
		//c->compression_level = 12;
		c->width = frame->width;
		c->height = frame->height; 
		AVRational ra{ 1, 25 };
		c->time_base = ra;
		c->pix_fmt = AV_PIX_FMT_YUVJ420P;

		avcodec_open2(c, codec, NULL);

		
		AVPacket pkt;
		av_init_packet(&pkt);
		pkt.data = NULL;
		pkt.size = 0;

		int got_output = 0;
		ret = avcodec_encode_video2(c, &pkt, frame, &got_output);

		if (got_output){
			//fwrite(pkt.data, 1, pkt.size, f);
			auto uint8_t = pkt.data;
		}

		avcodec_close(c);
		av_free(c);


	}

};

#endif
