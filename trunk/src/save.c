#include "save.h"
#include "trace.h"
#include "process.h"
#include "avb.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <fcntl.h>
#include <net/if.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <unistd.h>

#include <signal.h>
#include <sys/time.h>

#define SAVE_DATA_FILE_PATH "/work/mnt/"
#define MODE (S_IRWXU | S_IRWXG | S_IRWXO)

/* Most support files. */
#define ARRAY_NUM 100
/* File name length. */
#define FILE_NAME_LEN 34
/* Free size. */
#define FREE_SIZE (10*1024*1024)
/* Timer out. */
#define TIMER_OUT (5*60)
#define TIMER_CLOSE 0

/* Save data thread mutex lock and condition lock. */
pthread_cond_t avb_save_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t avb_save_mutex = PTHREAD_MUTEX_INITIALIZER;

extern pthread_cond_t proc_to_save_cond;
extern pthread_mutex_t proc_to_save_mutex;

typedef struct save_buffer
{
    uint8_t *buf;
    long int s_h264_len;
    int unpack_lock;
    int save_lock;
    struct save_buffer *next;
} save_buffer;
save_buffer * s_buf_data;

typedef struct file_attr
{
    char *file_name;
    long int ctime;
} file_attr;
file_attr *file_sttrs[ARRAY_NUM];

/* SD card manage. */
pthread_cond_t sd_manage_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t sd_manage_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Save fd. */
pthread_mutex_t save_fd_mutex = PTHREAD_MUTEX_INITIALIZER;

int save_fd = -1;
int time_out_flag = 0;

struct itimerval *tickp;

int get_file_name(const char *pathname, char *time_str, int len)
{
    time_t timep;
    //int len = 0;
    /*
    struct   tm
    {
        　int   tm_sec;//seconds   0-61
        　int   tm_min;//minutes   1-59
        　int   tm_hour;//hours   0-23
        　int   tm_mday;//day   of   the   month   1-31
        　int   tm_mon;//months   since   jan   0-11
        　int   tm_year;//years   from   1900
        　int   tm_wday;//days   since   Sunday,   0-6
        　int   tm_yday;//days   since   Jan   1,   0-365
         int   tm_isdst;//Daylight   Saving   time   indicator
    };*/
    struct tm *time_now;

    //char *time_str = (char *)malloc(sizeof(char)*FILE_NAME_LEN);

    memset(time_str, 0, len);

    time(&timep);
    //DEBUG("%s", asctime(gmtime(&timep)));

    time_now = localtime(&timep);
    //DEBUG("%s", asctime(time_now));

    len = sprintf(time_str, "%s%4d-%02d-%02d_%02d-%02d-%02d.h264", pathname, (time_now->tm_year + 1900),
        (time_now->tm_mon + 1), time_now->tm_mday, time_now->tm_hour, time_now->tm_min, time_now->tm_sec);
    DEBUG("%s len=%d", time_str, len);

    if (len != FILE_NAME_LEN)
    {
        DEBUG("len = %d", len);
        return -1;
    }

    return 0;
}

int create_file(void)
{
    char *filp = (char *)malloc(sizeof(char)*FILE_NAME_LEN);

    int fd = -1;
    int ret = -1;

    /* Get file name. */
    ret = get_file_name(SAVE_DATA_FILE_PATH, filp, FILE_NAME_LEN);
    if (ret == -1)
    {
        DEBUG("Get file name fail!");
        return -1;
    }

    /* QNX does not support this format: 2000-01-01_08:09:48.h264*/
    if ((fd = open(filp, O_RDWR | O_CREAT, 0666)) < 0)
    {
        DEBUG("open recorder save file error: %s", strerror(errno));
        return -1;
    }

    free(filp);
    filp = NULL;

    return fd;
}

/* In the system definition.
struct dirent
{
    ino_t d_ino;               //d_ino 此目录进入点的inode
    ff_t d_off;                //d_off 目录文件开头至此目录进入点的位移
    signed short int d_reclen; //d_reclen _name 的长度, 不包含NULL 字符
    unsigned char d_type;      //d_type d_name 所指的文件类型 d_name 文件名
    har d_name[256];
};

struct stat 
{
    mode_t    st_mode;      //文件对应的模式，文件，目录等:文件权限和文件类型信息 
    ino_t     st_ino;       //inode节点号
    dev_t     st_dev;       //设备号码
    dev_t     st_rdev;      //特殊设备号码
    nlink_t   st_nlink;     //文件的连接数
    uid_t     st_uid;       //文件所有者
    gid_t     st_gid;       //文件所有者对应的组
    off_t     st_size;      //普通文件，对应的文件字节数
    time_t    st_atime;     //文件最后被访问的时间
    time_t    st_mtime;     //文件内容最后被修改的时间
    time_t    st_ctime;     //文件状态改变时间
    blksize_t st_blksize;   //文件内容对应的块大小
    blkcnt_t  st_blocks;    //伟建内容对应的块数量
};
*/

char *get_first_file_name(file_attr *file_attr_s[], int number)
{
    int i = 0;
    int offset = 0;

    long int min_value = file_attr_s[0]->ctime;

    /* The largest number of files. */
    if (number > ARRAY_NUM)
    {
        number = ARRAY_NUM;
    }

    /*  */
    for (i = 1; i < number; i++)
    {
        if (file_attr_s[i]->ctime < min_value)
        {
            min_value = file_attr_s[i]->ctime;
            offset = i;
        }
    }

     DEBUG("\n\nfile_attr_s[%d].file_name:%s\n", offset, file_attr_s[offset]->file_name);

    return file_attr_s[offset]->file_name;
} 

/****************
int setitimer(int which, const struct itimerval *value, struct itimerval *ovalue));

　　struct itimerval {
    　　struct timeval it_interval;  // interval time
    　　struct timeval it_value;     // initial time
　　};

　　struct timeval {
    　　long tv_sec;                 // s
    　　long tv_usec;                // us
　　};
*********************/

int set_timer(struct itimerval *tick, long sec)
{
    /* Timeout to run first time. */
    tick->it_value.tv_sec = sec;
    tick->it_value.tv_usec = 0;

    /* After first, the interval time for clock. */
    tick->it_interval.tv_sec = sec;
    tick->it_interval.tv_usec = 0;

    if (setitimer(ITIMER_REAL, tick, NULL) < 0)
    {
        ERROR("Set timer failed!");
        return -1;
    }

    return 0;
}

void timer_close(void)
{
    set_timer(tickp, TIMER_CLOSE);
}

void timer_start(void)
{
    set_timer(tickp, TIMER_OUT);
}

int new_file(void)
{
    static int first_time = 1;

    //pthread_mutex_lock(&save_fd_mutex);
    if (!first_time)
    {
        close(save_fd);
    }
    first_time = 0;

    save_fd = create_file();
    if (save_fd < 0)
    {
        return -1;
    }
    //pthread_mutex_unlock(&save_fd_mutex);

    return 0;
}

void time_fun(int signo)
{
    time_out_flag = 1;
}

void *save_data_thread(void *arg)
{
    int ret;
    DIR *dir;

    int i;

    s_buf_data = get_save_data();

    tickp = (struct itimerval *)malloc(sizeof(struct itimerval));
    memset(tickp, 0, sizeof(struct itimerval));

    signal(SIGALRM, time_fun);

    /* opentir: open directory */
    if ((dir = opendir(SAVE_DATA_FILE_PATH)) == NULL)
    {
        /* Create directory. */
        if (mkdir(SAVE_DATA_FILE_PATH, MODE) == 0)
        {
            dir = opendir(SAVE_DATA_FILE_PATH);
        }
        else
        {
            DEBUG("Create %s fail!", SAVE_DATA_FILE_PATH);
            goto exit;
        }
    }
    closedir(dir);
    
	while(1)
	{
		/* Fisrt initialize unpack_lock = 1 */
		if (s_buf_data->unpack_lock)
        {
        	//DEBUG("Save thread sleep\n");
			pthread_mutex_lock(&avb_save_mutex);
		    pthread_cond_wait(&avb_save_cond, &avb_save_mutex);
		    pthread_mutex_unlock(&avb_save_mutex);
		}
#if TIME_TSET
        struct timeval wtpstart, wtpend;
        float wtimeuse;

        gettimeofday(&wtpstart, NULL);
#endif
        //pthread_mutex_lock(&save_fd_mutex);
		ret = write(save_fd, s_buf_data->buf, s_buf_data->s_h264_len);
        //pthread_mutex_unlock(&save_fd_mutex);
        if (ret < 0)
        {
            /* EFBIG [file too large] */
            if (errno == EFBIG)
            {
                DEBUG("%s", strerror(errno));
            }
            //else if (errno == ENOBUFS) // ENOBUFS [no buffer space]
            else if (errno == ENOSPC)    //ENOSPC [no space on device]
            {
                DEBUG("%s", strerror(errno));
            }
            else if (errno == EIO)   //EIO [io error]: SD card was uprooted.
            {
                DEBUG("%s", strerror(errno));
            }
            else
            {
                ERROR("%s", strerror(errno));
            }
        }
#if TIME_TSET
        gettimeofday(&wtpend, NULL);
        wtimeuse = 1000000 * (wtpend.tv_sec - wtpstart.tv_sec) + wtpend.tv_usec - wtpstart.tv_usec;  
        wtimeuse /= 1000000;  
        DEBUG("Used   Time:%f\n ", wtimeuse);
#endif
        pthread_mutex_lock(&sd_manage_mutex);
        pthread_cond_signal(&sd_manage_cond);
        pthread_mutex_unlock(&sd_manage_mutex);

        pthread_mutex_lock(&proc_to_save_mutex);
        s_buf_data->unpack_lock = 1;
        s_buf_data->save_lock = 0;
        s_buf_data->s_h264_len = 0;
        pthread_cond_signal(&proc_to_save_cond);
        pthread_mutex_unlock(&proc_to_save_mutex);

        s_buf_data = s_buf_data->next;
	}

exit:
    for(i = 0; i < ARRAY_NUM; i++)
    {
        free(file_sttrs[i]);
    }

    DEBUG("Save thread exit!");

    exit(0);

    pthread_exit(0);
}

/**
struct statfs { 
    long    f_type;     // 文件系统类型 
    long    f_bsize;    // 经过优化的传输块大小
    long    f_blocks;   // 文件系统数据块总数 
    long    f_bfree;    // 可用块数
    long    f_bavail;   // 非超级用户可获取的块数
    long    f_files;    // 文件结点总数
    long    f_ffree;    // 可用文件结点数 
    fsid_t  f_fsid;     // 文件系统标识
    long    f_namelen;  // 文件名的最大长度
}; 
*****/

/**
 * [sd_manage_thread description]
 * @param  arg [description]
 * @return     [description]
 */
void *sd_manage_thread(void *arg)
{
    DIR *dir;
    struct dirent *ptr;
    struct stat stat_buf;
    int i;
    char *remove_file_name;

    struct statvfs sd_info;

    for(i = 0; i < ARRAY_NUM; i++)
    {
        file_sttrs[i] = (struct file_attr*)malloc(sizeof(struct file_attr));
    }

    for(i = 0; i < ARRAY_NUM; i++)
    {
        file_sttrs[i]->file_name = (char *)malloc(sizeof(char)*FILE_NAME_LEN);
    }

    statvfs(SAVE_DATA_FILE_PATH, &sd_info);

    /* sd card block size. */
    unsigned long long block_size = sd_info.f_bsize;
    INFO("block_size = %lldByte", block_size);
    /* sd card total size. */
    unsigned long long total_size = block_size * sd_info.f_blocks;
    INFO("total_size = %lldM", total_size >> 20);
    /* sd card free size. */
    unsigned long long free_disk = sd_info.f_bfree * block_size;
    INFO("free_disk = %lldM", free_disk >> 20);

    if (free_disk < FREE_SIZE)
    {
        DEBUG("SD card no space!");
        goto exit;
    }

    while(1)
    {
        pthread_mutex_lock(&sd_manage_mutex);
        pthread_cond_wait(&sd_manage_cond, &sd_manage_mutex);
        pthread_mutex_unlock(&sd_manage_mutex);

        statvfs(SAVE_DATA_FILE_PATH, &sd_info);

        free_disk = sd_info.f_bfree * block_size;
        //INFO("free_disk = %lld", free_disk >> 20);
        if (free_disk < FREE_SIZE)
        {
            DEBUG("SD card no space!");
            /* cd SAVE_DATA_FILE_PATH directory. */
            chdir(SAVE_DATA_FILE_PATH);
            dir = opendir(SAVE_DATA_FILE_PATH);
            i = 0;
            /* List folder contents. */
            while(((ptr = readdir(dir)) != NULL)  && (i < ARRAY_NUM))
            {
                /* Get file attributes status. */
                lstat(ptr->d_name, &stat_buf);
                /* S_ISREG: Determine the file whether is a regular file. */
                if (S_ISREG(stat_buf.st_mode))
                {
                    //file_sttrs[i]->file_name = ptr->d_name;
                    memcpy(file_sttrs[i]->file_name, ptr->d_name, FILE_NAME_LEN);
                    file_sttrs[i]->ctime = stat_buf.st_ctime;
                    //DEBUG("d_name:%s st_ctime:%ld", file_sttrs[i]->file_name, file_sttrs[i]->ctime);
                    i++;
                }
            }
            remove_file_name = get_first_file_name(&file_sttrs[0], i);
            remove(remove_file_name);
            closedir(dir);
        } 
    }

exit:

    DEBUG("SD card manage thread exit!");
    exit(0);

    pthread_exit(0);
}

