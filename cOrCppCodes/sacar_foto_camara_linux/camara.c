#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <linux/videodev2.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

char out[300] = "salida_camara.jpg";
char dev[300] = "/dev/video0";
int want_width = 1280;
int want_height = 720;
enum AVPixelFormat outPixJPGE = AV_PIX_FMT_YUVJ422P;

int parse_args(int argc, char **argv)
{
	int opt;
	while((opt = getopt(argc, argv, "v:w:h:o:")) != -1){
		switch(opt){
			case 'v':
				strncpy(dev, optarg, 299);
				break;
			case 'w':
				want_width = atoi(optarg);
				break;
			case 'h':
				want_height = atoi(optarg);
				break;
			case 'o':
				strncpy(out, optarg, 299);
				break;
			default:
				fprintf(stderr, "Uso:\n%s -v /dev/videoN -w ancho -h alto -o salida_camara.jpg\n", argv[0]);
				return -1;
		}

	}
	return 0;
}

int abrir(char *f)
{
	int i;

	if(!f)
		return -1;

	printf("Abriendo %s\n", f);
	i = open(f, O_RDWR);
	if(i < 0){
		fprintf(stderr, "No pude abrir %s. %s\n", f, strerror(errno));
	}
	return i;
}

int query_caps(int f, uint32_t *capability)
{
	struct v4l2_capability cap;

	if(ioctl(f, VIDIOC_QUERYCAP, &cap)){
		fprintf(stderr, "Fallo VIDIOC_QUERYCAP. %s\n", strerror(errno));
		return -1;
	}

	printf("Driver: %s\nCard: %s\nBus info: %s\n", cap.driver, cap.card, cap.bus_info);

	if((cap.device_caps & V4L2_CAP_VIDEO_CAPTURE) == 0){
		fprintf(stderr, "No captura video\n");
		return -1;
	}
	*capability = cap.device_caps;

	return 0;
}

int select_input(int f)
{
	struct v4l2_input input;
	int i;

	for(input.index = 0; !ioctl(f, VIDIOC_ENUMINPUT, &input); input.index++){
		printf("Input: %d\n\tName: %s\n", input.index, input.name);
	}
	
	i = 0;
	if(ioctl(f, VIDIOC_S_INPUT, &i)){
		fprintf(stderr, "No pude seleccionar input %d. %s\n", i, strerror(errno));
		return -1;
	}

	return 0;
}

int select_format(int fd, uint32_t *pixelformat, int *read_width, int *read_height)
{
	int i = -1;
	struct v4l2_fmtdesc fmtdesc;
	struct v4l2_format format;

	fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	for(fmtdesc.index = 0; ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0; fmtdesc.index++){
		if(fmtdesc.pixelformat == V4L2_PIX_FMT_RGB24 || fmtdesc.pixelformat == V4L2_PIX_FMT_BGR24 || fmtdesc.pixelformat == V4L2_PIX_FMT_YUYV){
			*pixelformat = fmtdesc.pixelformat;
			i = fmtdesc.index;
		}
		printf("Index: %d\n\tDesc: %s\n", fmtdesc.index, fmtdesc.description);
	}

	if(i < 0){
		fprintf(stderr, "No soporto ningun formato\n");
		return -1;
	}
	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if(ioctl(fd, VIDIOC_G_FMT, &format)){
		fprintf(stderr, "No pude pedir el formato por defecto. %s\n", strerror(errno));
		return -1;
	}
	format.fmt.pix.pixelformat = *pixelformat;
	format.fmt.pix.width = want_width;
	format.fmt.pix.height = want_height;
	if(ioctl(fd, VIDIOC_S_FMT, &format)){
		fprintf(stderr, "No pude setear el formato. %s\n", strerror(errno));
		return -1;
	}
	if(ioctl(fd, VIDIOC_G_FMT, &format)){
		fprintf(stderr, "No pude pedir el formato seteado. %s\n", strerror(errno));
		return -1;
	}
	*read_width = format.fmt.pix.width;
	*read_height = format.fmt.pix.height;
	printf("Camara retorna imagen en %dx%d\n", format.fmt.pix.width, format.fmt.pix.height);
	return 0;
}

int read_frame(int fd, void **buf, size_t *size, uint32_t capability, uint32_t pixelformat, int read_width, int read_height)
{
	struct v4l2_requestbuffers reqbuf;
	struct v4l2_buffer v4l2buf;
	void *p;
	size_t r;
	int buftype;

	if(capability & V4L2_CAP_READWRITE){
		switch(pixelformat){
			case V4L2_PIX_FMT_RGB24:
			case V4L2_PIX_FMT_BGR24:
				*size = read_width * read_height * 3;
				break;
			case V4L2_PIX_FMT_YUYV:
				*size = read_width * read_height * 2;
				break;
			default:
				fprintf(stderr, "No soporto pixelformat %" PRIu32 "\n", pixelformat);
		}
		*buf = malloc(*size);
		r = read(fd, *buf, *size);
		if(r != *size){
			fprintf(stderr, "Esperaba poder leer %zd bytes, no pude, lei %zd\n", *size, r);
			return -1;
		}
	}
	else if(capability & V4L2_CAP_STREAMING){
		memset(&reqbuf, 0, sizeof(reqbuf));
		reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		reqbuf.memory = V4L2_MEMORY_MMAP;
		reqbuf.count = 1;
		if(ioctl(fd, VIDIOC_REQBUFS, &reqbuf)){
			fprintf(stderr, "No pude pedir buffers. %s\n", strerror(errno));
			return -1;
		}

		memset(&v4l2buf, 0, sizeof(v4l2buf));
		v4l2buf.index = 0;
		v4l2buf.memory = V4L2_MEMORY_MMAP;
		v4l2buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if(ioctl(fd, VIDIOC_QUERYBUF, &v4l2buf)){
			fprintf(stderr, "No pude pedir buffers. %s\n", strerror(errno));
			return -1;
		}

		p = mmap(NULL, v4l2buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, v4l2buf.m.offset);
		if(p == MAP_FAILED){
			fprintf(stderr, "No pude ejecutar mmap. %s\n", strerror(errno));
			return -1;
		}

		buftype = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if(ioctl(fd, VIDIOC_STREAMON, &buftype)){
			fprintf(stderr, "No pude iniciar captura. %s\n", strerror(errno));
			return -1;
		}

		memset(&v4l2buf, 0, sizeof(v4l2buf));
		v4l2buf.index = 0;
		v4l2buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		v4l2buf.memory = V4L2_MEMORY_MMAP;
		if(ioctl(fd, VIDIOC_QBUF, &v4l2buf)){
			fprintf(stderr, "No pude encolar buffer. %s\n", strerror(errno));
			return -1;
		}
		memset(&v4l2buf, 0, sizeof(v4l2buf));
		v4l2buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		v4l2buf.memory = V4L2_MEMORY_MMAP;
		if(ioctl(fd, VIDIOC_DQBUF, &v4l2buf)){
			fprintf(stderr, "No pude sacar buffer. %s\n", strerror(errno));
			return -1;
		}
		*size = v4l2buf.length;
		*buf = malloc(*size);
		memcpy(*buf, p, *size);
		munmap(p, v4l2buf.length);

		buftype = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if(ioctl(fd, VIDIOC_STREAMOFF, &buftype)){
			fprintf(stderr, "No pude parar la captura. %s\n", strerror(errno));
		}

	}
	else{
		fprintf(stderr, "No se como leer el frame\n");
		return -1;
	}
	return 0;
}

int resize(void *buf, int inWidth, int inHeight, enum AVPixelFormat inPix, void **out, size_t *size, int outWidth, int outHeight, enum AVPixelFormat outPix)
{
	struct SwsContext *ctx;
	void *outbuf; //puntero de la imagen de salida
	size_t outsize; //tama~no de la imagen de salida
	uint8_t *in_data[4]; //punteros que usa ffmpeg para la entrada
	int in_linesize[4]; //punteros que usa ffmpeg para la entrada
	uint8_t *out_data[4]; //punteros que usa ffmpeg para la salida
	int out_linesize[4]; //punteros que usa ffmpeg para la salida

	ctx = sws_getContext(inWidth, inHeight, inPix, outWidth, outHeight, outPix, SWS_BICUBIC, NULL, NULL, NULL);
	if(ctx == NULL){ //error
		fprintf(stderr, "No pude crear contexto de resize %dx%d@%d -> %dx%d@%d\n", inWidth, inHeight, inPix, outWidth, outHeight, outPix);
		return -1;
	}

	av_image_fill_arrays(in_data, in_linesize, buf, inPix, inWidth, inHeight, 1); //llenamos los punteros que usa ffmpeg con nuestro buf que tiene la imagen de entrada

	outsize = av_image_get_buffer_size(outPix, outWidth, outHeight, 1); //obtengo cuanto va a ocupar la imagen de salida
	outbuf = av_malloc(outsize); //obtengo memoria para la imagen
	av_image_fill_arrays(out_data, out_linesize, outbuf, outPix, outWidth, outHeight, 1); //leno los punteros que usa ffmpeg con nuestro buffer de salida

	sws_scale(ctx, (const uint8_t * const*)in_data, in_linesize, 0, inHeight, out_data, out_linesize); //resizeo

	sws_freeContext(ctx); //libero el contexto que ya no lo voy a usar

	*out = outbuf; //acordarse del que llama a resize() que libere *out con av_freep()
	*size = outsize;

	printf("Resize de frame %dx%d@%d -> %dx%d@%d\n", inWidth, inHeight, inPix, outWidth, outHeight, outPix);

	return 0;
}

int encode_jpeg(void *buf, size_t size, int width, int height, enum AVPixelFormat pix, void **jpegbuf, size_t *jpegsize)
{
	int gotframe;
	AVFrame *frame;
	AVPacket pkt;
	AVCodec *codec;
	AVCodecContext *ctx;

	avcodec_register_all();

	codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
	if(codec == NULL){
		return -1; //error
	}

	ctx = avcodec_alloc_context3(codec);
	if(ctx == NULL){
		return -1; //error
	}

	ctx->codec_id = AV_CODEC_ID_MJPEG;
	ctx->width = width;
	ctx->height = height;
	ctx->pix_fmt = pix;
	ctx->qmin = 1;
	ctx->qmax = 10; 
	ctx->framerate.num = 1;
	ctx->framerate.den = 1;
	ctx->time_base.num = 1;
	ctx->time_base.den = 1;

	if(avcodec_open2(ctx, codec, NULL) < 0){ 
		avcodec_free_context(&ctx);
		return -1; //error
	}
	
	frame = av_frame_alloc();
	av_image_fill_arrays(frame->data, frame->linesize, buf, pix, width, height, 1);
	frame->width = width;
	frame->height = height;
	frame->format = (int)pix;

	av_init_packet(&pkt);
	pkt.data = 0;
	pkt.size = 0;

	if(avcodec_encode_video2(ctx, &pkt, frame, &gotframe) < 0){
		avcodec_free_context(&ctx);
		return -1; //error
	}
	if(!gotframe){
		avcodec_free_context(&ctx);
		return -1; //error
	}
	//Ahora tenemos la imagen encodeada en pkt.data y pkt.size

	av_frame_free(&frame);
	avcodec_close(ctx); //cierro contexto
	avcodec_free_context(&ctx); //libero contexto

	//una vez usada la imagen encodeada, recordar liberarla con av_freep()
	*jpegbuf = pkt.data;
	*jpegsize = pkt.size;

	printf("Imagen encodeada a JPEG\n");

	return 0;
}


int write_jpeg(void *buf, size_t size)
{
	int fd;

	fd = open(out, O_WRONLY | O_TRUNC | O_CREAT, 0664);
	if(fd < 0){
		fprintf(stderr, "No pude crear la salida %s. %s\n", out, strerror(errno));
		return -1;
	}
	if(write(fd, buf, size) != size){
		fprintf(stderr, "No pude escribir todo el archivo %s. %s\n", out, strerror(errno));
		close(fd);
		return -1;
	}
	printf("Escribi jpeg en %s\n", out);

	close(fd);

	return 0;
}

int main(int argc, char **argv)
{
	int read_width;
	int read_height;
	uint32_t capability;
	uint32_t pixelformat;
	enum AVPixelFormat inPix;

	void *buf;
	size_t size;
	int fd;
	void *outbuf;
	size_t outsize;

	avcodec_register_all();

	if(parse_args(argc, argv))
		return -1;

	fd = abrir(dev);
	if(fd < 0)
		return -1;

	if(query_caps(fd, &capability))
		return -1;

	if(select_input(fd))
		return -1;

	if(select_format(fd, &pixelformat, &read_width, &read_height))
		return -1;

	switch(pixelformat){
		case V4L2_PIX_FMT_RGB24:
			inPix = AV_PIX_FMT_RGB24;
			break;
		case V4L2_PIX_FMT_BGR24:
			inPix = AV_PIX_FMT_BGR24;
			break;
		case V4L2_PIX_FMT_YUYV:
			inPix = AV_PIX_FMT_YUYV422;
			break;
		default:
			fprintf(stderr, "No soporto pixelformat %" PRIu32 "\n", pixelformat);
	}

	if(read_frame(fd, &buf, &size, capability, pixelformat, read_width, read_height))
		return -1;

	if(resize(buf, read_width, read_height, inPix, &outbuf, &outsize, want_width, want_height, outPixJPGE))
		return -1;

	free(buf);
	buf = outbuf;
	size = outsize;

	if(encode_jpeg(buf, size, want_width, want_height, outPixJPGE, &outbuf, &outsize))
		return -1;

	av_freep(&buf);
	buf = outbuf;
	size = outsize;

	if(write_jpeg(buf, size))
		return -1;

	av_freep(&buf);

	close(fd);

	return 0;
}

