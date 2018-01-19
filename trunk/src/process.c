#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>

#include <net/bpf.h>
#include <net/if.h>
//#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "process.h"
#include "avb.h"
#include "save.h"
#include "trace.h"

#define SAVE_DATA_FILE "/tmp/recorder.h264"

/**
struct bpf_hdr 
{
    struct timeval bh_tstamp;   // time stamp
    u_long bh_caplen;           // length of captured portion
    u_long bh_datalen;          // original length of packet
    u_short bh_hdrlen;          // length of bpf header (this struct plus alignment paddiing)
}*/
#define bhp ((struct bpf_hdr *)bp)

#define AVB_STREAMID_BASE    0x3350
#define AVTP_HEADER_LENGTH   (14 + 36 + 4) // Mac Header (14byte) + AVTP Header (36Byte) + CRC (4Byte)
#define AVTP_PAYLOAD_LENGTH  (992)

#define S_SAVE_BUF_SUM 30
#define BUFFER_SIZE 102400

/* Receive thread mutex lock and condition lock. */
extern pthread_cond_t avb_recv_cond;
extern pthread_mutex_t avb_recv_mutex;

/* Save data thread mutex lock and condition lock. */
extern pthread_cond_t avb_save_cond;
extern pthread_mutex_t avb_save_mutex;

extern int time_out_flag;

/* Procesee thread mutex lock and condition lock. */
pthread_cond_t avb_proc_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t avb_proc_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_cond_t proc_to_save_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t proc_to_save_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct avb_buffer 
{
    uint8_t *buf;
    int len;
    int proc_lock;
    int recv_lock;
    struct avb_buffer *next;
} avb_buffer;
avb_buffer *buf;

typedef struct save_buffer
{
    uint8_t *buf;
    long int s_h264_len;
    int unpack_lock;
    int save_lock;
    struct save_buffer *next;
} save_buffer;

save_buffer s_save_buf[S_SAVE_BUF_SUM];
save_buffer *s_buf = s_save_buf;

int s_flag = 0;
/* sequence number. */
int s_sn = -1;

void set_s_flag(int flag)
{
    s_flag = flag;
}

/**
 * [reset_sn reset sequence.]
 */
void reset_sn(void)
{
    s_sn = -1;
}

//int save_fd = -1;
struct save_buffer *get_save_data(void)
{
    return s_save_buf;
}

int save_buf_init(void)
{
    int i;

    for (i = 0; i < S_SAVE_BUF_SUM; i++)
    {
        memset(&s_save_buf[i], 0, sizeof(struct save_buffer));

        s_save_buf[i].buf = (uint8_t *)malloc(sizeof(char)*BUFFER_SIZE);
        s_save_buf[i].s_h264_len = 0;
        s_save_buf[i].unpack_lock = 1;
        s_save_buf[i].save_lock = 0;
        s_save_buf[i].next = &s_save_buf[(i + 1) % S_SAVE_BUF_SUM];
    }

    DEBUG("Save buffer init OK\n");

    return 0;
}

static void avb_packet(int len, const u_char * packet)
{
    //int streamId = 0;
    /* sequence number. */
    int sn = 0;
	const uint8_t *h264 = NULL;
	int h264_len = 0;

    //static int s_flag = 0;

    if (len > AVTP_HEADER_LENGTH)
    {
        //streamId = packet[24] << 8;
        //streamId += packet[25];
        //channel = (streamId - AVB_STREAMID_BASE - 1) >> 1; //streamId: 3351 3353 3355 3357
        //if(channel != 0)
        //{
            //ERROR("channel = %d, not support", channel);
            //return;
        //}

        h264_len = packet[34] << 8;
        h264_len += packet[35];

        sn = packet[16];

        /* For the first time: (1 != 0 - (-1)) && (255 != -1 - 0) && (-1 != -1). */
        /* The second time: (1 != 1 - (0)) && (255 != 0 - 1) && (0 != -1). */
        /* error example: sn = 145, pre = 133, ((1 != 145 - (133)) && (255 != 133 - 145) && (133 != -1)).*/
        /* 0-255, (1 != sn - s_sn), (255 != s_sn - sn):255-0  */
        if ((1 != sn - s_sn) && (255 != s_sn - sn) && (s_sn != -1))
        {
            /* Hardware lost frame. */
            ERROR("sequence number = %d, s_sn = %d", sn, s_sn);
            //decode_reset(channel);
            s_sn = sn;
            s_flag = 0;
            s_buf->s_h264_len = 0;

            return;
        }

        s_sn = sn;

        if (len < AVTP_HEADER_LENGTH + h264_len)
        {
            ERROR("avb data error\n");
            return;
        }

        h264 = packet + AVTP_HEADER_LENGTH;

        /* h264 protocol header. */
        if ((!s_flag) && (h264[0] == 0x0 && h264[1] == 0x0 && h264[2] == 0x0 && h264[3] == 0x1
            && (h264[4] & 0x1F) == 0x7))
        {
            s_flag = 1;
        }

        if (s_flag)
        {
            memcpy((s_buf->buf + s_buf->s_h264_len), h264, h264_len);
            s_buf->s_h264_len = s_buf->s_h264_len + h264_len;
            //DEBUG("s_buf->s_h264_len = %ld\n", s_buf->s_h264_len);
        }
    }
    else
    { 
        ERROR("packet error, pkthdr->len = %d", len);
    }   
}

void *avb_proc_thread(void *arg)
{
    register uint8_t *bp, *ep;
    register int caplen, hdrlen;
    uint8_t *datap = NULL;

    buf = get_avb_recv_buf();

    while(1)
    {
        /* Fisrt initialize recv_lock = 1, block wait avb_recv_thread signal. */
        if (buf->recv_lock)
		{
            //DEBUG("Process thread sleep!\n");
		    pthread_mutex_lock(&avb_recv_mutex);
		    pthread_cond_wait(&avb_recv_cond, &avb_recv_mutex);
		    pthread_mutex_unlock(&avb_recv_mutex);
		}
        //DEBUG("Start unpack");
        
        if (buf->len != 0)
        {
            bp = buf->buf;
            ep = bp + buf->len;

            /* Fisrt initialize save_lock = 0 */
            if (s_buf->save_lock)
            {
                //DEBUG("Save data thread not fast enough!\n");
                pthread_mutex_lock(&proc_to_save_mutex);
                pthread_cond_wait(&proc_to_save_cond, &proc_to_save_mutex);
                pthread_mutex_unlock(&proc_to_save_mutex);
            }
#if TIME_TSET
            struct timeval ptpstart, ptpend;
            float ptimeuse;

            gettimeofday(&ptpstart, NULL);
#endif
            while (bp < ep)
            {
                caplen = bhp->bh_caplen;    // length of captured portion
                hdrlen = bhp->bh_hdrlen;    // original length of packet
                datap = bp + hdrlen;

                avb_packet(bhp->bh_datalen, datap);
                bp += BPF_WORDALIGN(caplen + hdrlen);
            }
#if TIME_TSET
            gettimeofday(&ptpend, NULL);
            ptimeuse = 1000000 * (ptpend.tv_sec - ptpstart.tv_sec) + ptpend.tv_usec - ptpstart.tv_usec;  
            ptimeuse /= 1000000;  
            DEBUG("Used   Time:%f\n ", ptimeuse);
#endif
            /* Wake up save thread to save data int sd card. */
            pthread_mutex_lock(&avb_save_mutex);
            s_buf->unpack_lock = 0;
            s_buf->save_lock = 1;
            pthread_cond_signal(&avb_save_cond);
            pthread_mutex_unlock(&avb_save_mutex);

            /* Time to regularly. */
            if (time_out_flag)
            {
                /* Waiting for buf data save to finish. */
                while(s_buf->save_lock);

                /* New file. */
                if (new_file() < 0)
                {
                    goto exit;
                }

                /* Judgment of h.264 data. */
                set_s_flag(0);

                time_out_flag = 0;
                //DEBUG("NEW FILE");
            }

            /* point to the next buffer. */
            s_buf = s_buf->next;
        }

        /* Wake up avb thread to read data. */
        pthread_mutex_lock(&avb_proc_mutex);
        buf->recv_lock = 1;
        buf->proc_lock = 0;
        buf->len = 0;
        pthread_cond_signal(&avb_proc_cond);
        pthread_mutex_unlock(&avb_proc_mutex);

        buf = buf->next;
    }

exit:

    DEBUG("Pro thread exit!");
    exit(0);

    pthread_exit(0);
}
