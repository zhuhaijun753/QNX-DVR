#include <stdio.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>

#include <sys/neutrino.h>
#include <net/bpf.h>
#include <net/if.h>
#include <errno.h>

#include "avb.h"
#include "trace.h"

#include "process.h"
#include "save.h"

#define CAMERA_ID 0x3357//0x3351
#define MAC_TYPE 0x22F0

#define BPF_NODE "/dev/bpf"
#define DEFAULT_BUFSIZE 102400

#define AVB_BUFFER_NUM 30

typedef struct avb_buffer {
    uint8_t *buf;
    int len;
    int proc_lock;
    int recv_lock;
    struct avb_buffer *next;
} avb_buffer;
avb_buffer *avb_buf = NULL;

avb_buffer avb_bufs[AVB_BUFFER_NUM];

/* Receive thread mutex lock and condition lock. */
pthread_cond_t avb_recv_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t avb_recv_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Receive thread mutex lock and condition lock. */
pthread_cond_t start_recv_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t start_recv_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Procesee thread mutex lock and condition lock. */
extern pthread_cond_t avb_proc_cond;
extern pthread_mutex_t avb_proc_mutex;

int recorder_stop_flag = 1;

/* ifreq Structure of the system definition
struct ifreq   
{  
    char ifrn_name[IFNAMSIZ]; // if name, e.g. "eth0"   
    union   
    {  
        struct sockaddr ifru_addr;  
        struct sockaddr ifru_dstaddr;  
        struct sockaddr ifru_broadaddr;  
        struct sockaddr ifru_netmask;  
        struct sockaddr ifru_hwaddr;  
        short ifru_flags;  
        int ifru_ivalue;  
        int ifru_mtu;  
        struct ifmap ifru_map;  
        char ifru_slave[IFNAMSIZ]; // Just fits the size   
        char ifru_newname[IFNAMSIZ];  
        void * ifru_data;  
        struct if_settings ifru_settings;  
    } ifr_ifru;  
}; */

struct avb_buffer *get_avb_recv_buf(void)
{
    return avb_bufs;
}

/**
 * [avb_init Initialized to circular linked list.]
 * @return [description]
 */
int avb_init(void)
{
    int i = 0;

    for (i = 0; i < AVB_BUFFER_NUM; i++)
    {
        memset(&avb_bufs[i], 0, sizeof(avb_buffer));
        
        avb_bufs[i].buf = NULL;
        avb_bufs[i].proc_lock = 0;
        avb_bufs[i].recv_lock = 1;
        /* Circular linked list. */
        avb_bufs[i].next = &avb_bufs[(i + 1) % AVB_BUFFER_NUM];
    }

    DEBUG("Avb buffer init OK\n");

    return 0;
}

/**
 * [avb_recv_signal description]
 * @param  lock [lock = 1 sent signal]
 * @return      [description]
 */
int avb_recv_signal(int lock)
{
    if (lock == 1)
    {
        pthread_mutex_lock(&start_recv_mutex);
        pthread_cond_signal(&start_recv_cond);
        pthread_mutex_unlock(&start_recv_mutex);
    }

    recorder_stop_flag = lock;

    return 0;
}

/**
 * [avb_recv_thread description]
 * @param  arg [description]
 * @return     [description]
 */
void *avb_recv_thread(void *arg)
{
	const char *devStr = "dm0";
	int fd;
    int i = 0;
    int read_len = 0;
    uint32_t buf_len = 0;

    /* ifreq Structure of the system definition. */
	struct ifreq ifr;
	struct bpf_program fp1, fp2;

    struct bpf_insn insn;
    insn.code = (u_short)(BPF_RET | BPF_K);
    insn.jt = 0;
    insn.jf = 0;
    insn.k = 65535;

    fp1.bf_len = 1;
    fp1.bf_insns = &insn;

    /**
     * struct bpf_insn {
        u_short      code;
        u_char       jt;
        u_char       jf;
        bpf_int32    k;
        };
     */
    struct bpf_insn insns[] = {
		BPF_STMT(BPF_LD+BPF_H+BPF_ABS, 12),
		BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, MAC_TYPE, 0, 1),
        BPF_STMT(BPF_LD+BPF_H+BPF_ABS, 24),
        BPF_JUMP(BPF_JMP+BPF_JEQ+BPF_K, CAMERA_ID, 0, 1),
		BPF_STMT(BPF_RET+BPF_K, (u_int)-1),
		BPF_STMT(BPF_RET+BPF_K, 0)
    };

    fp2.bf_len = 6;
    fp2.bf_insns = insns;

    strncpy(ifr.ifr_name, devStr, sizeof(ifr.ifr_name));

    avb_buf = avb_bufs;

start:
    DEBUG("Avb thread sleep!\n");
    /* Wait pps command sent recorder::start, thread first start. */
    pthread_mutex_lock(&start_recv_mutex);
    pthread_cond_wait(&start_recv_cond, &start_recv_mutex);
    pthread_mutex_unlock(&start_recv_mutex);

    /* Open bpf device. */
    fd = open(BPF_NODE, O_RDONLY);
    if (fd < 0)
    {
    	ERROR("Open bpf device fail: %s", strerror(errno));
    }

    /* BIOCGBLEN, Get buffer length. */
    if (ioctl(fd, BIOCGBLEN, (caddr_t)&buf_len) < 0)
    {
        ERROR("ioctl BIOCGBLEN fail: %s", strerror(errno));
        close(fd);
	    goto exit;    
    }
    DEBUG("Get buf_len = %d", buf_len);

    if (buf_len < DEFAULT_BUFSIZE)
    {
	    buf_len = DEFAULT_BUFSIZE;

		for (;buf_len > 0; buf_len >>= 1)
		{
		    /*  BIOCSBLEN, Set buffer length. */
		    if (ioctl(fd, BIOCSBLEN, (caddr_t)&buf_len) < 0)
		    {
				DEBUG("ioctl BIOCSBLEN fail: %s", strerror(errno));
		    }

		    /* BIOCSETIF, Add device to the network interface. struct ifreq */
            /* If the device add success, break. */
		    if (ioctl(fd, BIOCSETIF, (caddr_t)&ifr) >= 0)
		    {
				DEBUG("Set buf_len = %d", buf_len);

                /* BIOCGBLEN, Get buffer length again. */
                if (ioctl(fd, BIOCGBLEN, (caddr_t)&buf_len) < 0)
                {
                    ERROR("ioctl BIOCGBLEN fail: %s", strerror(errno));
                    close(fd);
                    goto exit;
                }
                DEBUG("get buf_len = %d", buf_len);

				break;
		    }

            /* ENOBUFS=[no buffer space]. */
            /* The buffer is too large, buf_len >>= 1. */
            if (errno != ENOBUFS)
            {
                ERROR("ioctl BIOCSETIF fail: %s", strerror(errno));
                close(fd);
                goto exit;
            }

		}
    }
    else
    {
        /* BIOCSETIF, Add device to the network interface. struct ifreq */
		if (ioctl(fd, BIOCSETIF, (caddr_t)&ifr) < 0) 
		{
		    ERROR("ioctl BIOCSETIF fail: %s", strerror(errno));
		    close(fd);
		    goto exit;
	    }
    }

    /* Allocate space of buf in the list. */
    for (i = 0; i < AVB_BUFFER_NUM; i++)
    {
        if (avb_bufs[i].buf == NULL)
        {
            //DEBUG("s_bufs[%d].buf = NULL", i);
            avb_bufs[i].buf = (uint8_t*)malloc(buf_len);
            /* malloc allocate memory fail, return NULL. */
            if (avb_bufs[i].buf == NULL)
            {
                ERROR("malloc fail: %s", strerror(errno));
                close(fd);
                goto exit;
            }
        }
        avb_bufs[i].len = 0;
    }

    /* BIOCPROMISC, Set mixed mode. */
    if (ioctl(fd, BIOCPROMISC, NULL) < 0)
    {
        ERROR("ioctl BIOCPROMISC fail: %s", strerror(errno));
        close(fd);
	    goto exit;
    }

    /* BIOCSETF, Install BPF program. */
    if (ioctl(fd, BIOCSETF, (caddr_t)&fp1) < 0)
    {
        ERROR("ioctl BIOCSETF fail: %s", strerror(errno));
        close(fd);
        goto exit;
    }

    /* BIOCSETF, Install BPF program. */
    if (ioctl(fd, BIOCSETF, (caddr_t)&fp2) < 0)
    {
        ERROR("ioctl BIOCSETF fail: %s", strerror(errno));
        close(fd);
        goto exit;
    }

    while(recorder_stop_flag)
    {
        /* Fisrt initialize proc_lock = 0, it is not perform for the first time. */ 
        /* If the avb_proc_thread is not complete, block wait. */
        if (avb_buf->proc_lock)
        {
            DEBUG("proccessing not fast enough");

            pthread_mutex_lock(&avb_proc_mutex);
            pthread_cond_wait(&avb_proc_cond, &avb_proc_mutex);
            pthread_mutex_unlock(&avb_proc_mutex);
        }
again:
#if TIME_TSET
        struct timeval rtpstart, rtpend;
        float rtimeuse;

        gettimeofday(&rtpstart, NULL);
#endif
        /* Read bpf data. */
        read_len = read(fd, avb_buf->buf, buf_len);
        if (read_len <= 0)
        {
            ERROR("read fail: %s", strerror(errno));
            
            /* EINTR = interrupted. */
            if (errno == EINTR && recorder_stop_flag)
            //if (errno == EINTR)
            {
                goto again;
            }

            ERROR("break the recv loop");

            break;
        }
#if TIME_TSET
        gettimeofday(&rtpend, NULL);
        rtimeuse = 1000000 * (rtpend.tv_sec - rtpstart.tv_sec) + rtpend.tv_usec - rtpstart.tv_usec;  
        rtimeuse /= 1000000;  
        DEBUG("Used   Time:%f\n ", rtimeuse);
#endif
        /* Notify the avb_proc_thread extract H.264 data. */
        pthread_mutex_lock(&avb_recv_mutex);
        avb_buf->len = read_len;
        avb_buf->recv_lock = 0;
        avb_buf->proc_lock = 1;
        pthread_cond_signal(&avb_recv_cond);
        pthread_mutex_unlock(&avb_recv_mutex);

        avb_buf = avb_buf->next;
    }

    close(fd);
    goto start;

exit:

    DEBUG("Avb thread exit!");
    exit(0);
    pthread_exit(0);
}
