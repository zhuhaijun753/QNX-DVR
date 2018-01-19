#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/select.h>
#include <hnm/pps.h>
#include <pthread.h>
#include <errno.h>

#include "trace.h"
#include "avb.h"
#include "process.h"
#include "save.h"

/* PPS file path. */
#define PPS_RECORDER_FINE "/pps/hinge-tech/recorder?delta"
//#define PPS_RECORDER_FINE "/work/pps_file/recorder?delta"

/* Recorder version. */
#define VERSION "recorder version: 00.01.00\n"

int pps_fd;

/**
 * [cmd_line_arg Display code version]
 * @param  argc [the number of command line parameters]
 * @param  argv [Parameter address pointer]
 * @return      [0:success other:exit]
 */
int cmd_line_arg(int argc, char *argv[])
{
    if (argc == 1)
    {
        return 0;
    }
    else if ((argc == 2) && (strcmp(argv[1], "--version") == 0))
    {
        INFO(VERSION);
        exit(0);
    }
    else
    {
        INFO("Cmd error!\n");
        exit(0);
    }

    return 0;
}

/**
 * [sigroutine receive signal]
 * @param signal [signal]
 */
void sigroutine(int signal)
{
   switch(signal)
   {
      case 2:
        write(pps_fd, "recorder::stop",  15);
        INFO("Exit recorder!\n");
        exit(0);
        break;
      default:
        ERROR("signal error\n");
        break;
   }
}

int pps_cmd_handle(char *cmd, char *cmd_status)
{
    if ((strcmp(cmd, "start") == 0) && (strcmp(cmd_status, "stop") == 0))
    {
        INFO("Start recorder\n");
    	memcpy(cmd_status, "start", sizeof(char)*5);

        if (new_file() < 0)
        {
            exit(0);
        }

        timer_start();

        avb_recv_signal(1);

        set_s_flag(0);
        reset_sn();
    }
    else if ((strcmp(cmd, "stop") == 0) && (strcmp(cmd_status, "start") == 0))
    {
        INFO("Stop recorder\n");
        memcpy(cmd_status, "stop", sizeof(char)*5);
        
        timer_close();
        avb_recv_signal(0);
        write(pps_fd, "recorder::stop",  15);
    }
    else
    {
        ERROR("Command error\n");
        return -1;
    }

    return 0;
}

/**
 * [main description]
 * @param  argc [the number of command line parameters]
 * @param  argv [Parameter address pointer]
 * @return      [description]
 */
int main(int argc, char *argv[])
{
    fd_set rfds;
    char *pps_buf;
    const char *pps_cmd;
    char *pps_cmd_status;
    int read_len = 0;

    pps_decoder_t decoder;
    pps_decoder_error_t err;

    pthread_t avb_recv_thread_id;
    pthread_t avb_proc_thread_id;
    pthread_t save_data_thread_id;
    pthread_t sd_manage_thread_id;

    /* Allocate memory. */
    //recorderp = (recorder_info *)malloc(sizeof(struct recorder));
    pps_buf = (char *)malloc(sizeof(char)*128);
    pps_cmd = (char *)malloc(sizeof(char)*5);
    pps_cmd_status = (char *)malloc(sizeof(char)*5);

    memcpy(pps_cmd_status, "stop", sizeof(char)*5);
    memset(pps_buf, 0, sizeof(char)*32);

    /* Open PPS file. */
    if ((pps_fd = open(PPS_RECORDER_FINE, O_RDWR)) <= 0)
    {
        INFO("/pps/hinge-tech/recorder recorder file open error!\n");
        goto fail;
    }
    read_len = read(pps_fd, pps_buf, sizeof(char)*128);
    if (read_len < 0)
    {
        DEBUG("Read /pps/hinge-tech/recorder fail\n");
    }
    printf("%s\n*********\n", pps_buf);


    /* Receive command line arguments. */
    cmd_line_arg(argc, argv);

    /* Receive signal. */
    signal(SIGINT, sigroutine);

    /* Initialize to circular linked list. */
    avb_init();
    save_buf_init();

    /* audio/video bridging (AVB)*/
    /* Create thread to receive network packets. */
    pthread_create(&avb_recv_thread_id, NULL, avb_recv_thread, NULL);
    /* Create thread to process network packet data. */
    pthread_create(&avb_proc_thread_id, NULL, avb_proc_thread, NULL);
    /* Create thread to save H.264 data to SD card. */
    pthread_create(&save_data_thread_id, NULL, save_data_thread, NULL);
    /* Create thread to save H.264 data to SD card. */
    pthread_create(&sd_manage_thread_id, NULL, sd_manage_thread, NULL);

#if 1
    /* Don't set the priority, the effect is better */
    if (pthread_setschedprio(avb_recv_thread_id, 60) != EOK)
    {
        ERROR("avb recv thread: pthread_setschedprio() failed");
    }
    
    if (pthread_setschedprio(avb_proc_thread_id, 80) != EOK)
    {
        ERROR("avb proc thread: pthread_setschedprio() failed");
    }

    /* The higher the value, the greater the priority. */
    if (pthread_setschedprio(save_data_thread_id, 100) != EOK)
    {
        ERROR("avb save thread: pthread_setschedprio() failed");
    }

    if (pthread_setschedprio(sd_manage_thread_id, 20) != EOK)
    {
        ERROR("sd card manage thread: pthread_setschedprio() failed");
    }
#endif

    while(1)
    {
        memset(pps_buf, 0, sizeof(char)*128);

        FD_ZERO(&rfds);
        FD_SET(pps_fd, &rfds);

        if (select(pps_fd + 1, &rfds, NULL, NULL, NULL) < 0)
        {
            continue;
        }

        if (FD_ISSET(pps_fd, &rfds) == 0)
        {
            continue;
        }

        read(pps_fd, pps_buf, sizeof(char)*128);
        INFO("\n%s\n*********\n", pps_buf);

        pps_decoder_initialize(&decoder, NULL);
        pps_decoder_parse_pps_str(&decoder, pps_buf);
        pps_decoder_push(&decoder, NULL);

        err = pps_decoder_get_string(&decoder, "recorder", &pps_cmd);
        if (err == PPS_DECODER_OK)
        {
             pps_cmd_handle((char *)pps_cmd, pps_cmd_status);
        }

        pps_decoder_pop(&decoder);
        pps_decoder_cleanup(&decoder);
    }

    free(pps_buf);
    free((char *)pps_cmd);
    free(pps_cmd_status);

    pps_buf = NULL;
    pps_cmd = NULL;
    pps_cmd_status = NULL;

fail:
    close(pps_fd);

    return 0;
}
