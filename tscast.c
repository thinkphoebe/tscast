#ifdef WIN32
#include <winsock2.h>
#endif

#include <stdlib.h> //NULL
#include <string.h> //strncpy, memset
#include <stdio.h> //FILE
#include <stdint.h> //for uint32_t...
#include <unistd.h> //for usleep
#include <getopt.h>
#include <pthread.h> //-lpthread

#include "utils.h"
#include "tccore.h"

pthread_t m_thrd;
tccore_t *m_core;

static const char *short_options = "hi:d:l:";

static const struct option long_options[] =
{
    { "help", 0, NULL, 'h' },

    { "input", 1, NULL, 'i' },
    { "dest", 1, NULL, 'd' },
    { "loopfile", 1, NULL, 'l' },
    { "ttl", 1, NULL, 0 },
    { "bitrate", 1, NULL, 0 },

    { "rtpheader", 1, NULL, 0 },
    { "rtpssrc", 1, NULL, 0 },
    { "rtpseqstart", 1, NULL, 0 },

    { "lostrate", 1, NULL, 0 },
    { "dropnum", 1, NULL, 0 },

    { "seekpos", 1, NULL, 0 },
    { "seektime", 1, NULL, 0 },

    { "startpos", 1, NULL, 0 },
    { "endpos", 1, NULL, 0 },
    { "starttime", 1, NULL, 0 },
    { "endtime", 1, NULL, 0 },

    { "speedscale", 1, NULL, 0 },

    { NULL, 0, NULL, 0 }
};


static void print_help(const char *name, FILE *stream)
{
    fprintf(stream, "tscast [ts caster command line edition]\n"
            "Usage\n"

            "\t-h --help:\tDisplay this usage information.\n\n"

            "\t-i --input:\tSet the input file.\n"
            "\t-d --dest:\tSet destinations, such as -d 225.2.2.2:6000,172.16.6.99:12000.\n"
            "\t-l --loopfile:\t-1-->always loop, 0-->no loop, 1-->loop times.\n"
            "\t--ttl:\t\tTime to live value.\n"
            "\t--bitrate:\tForce bitrate. if not set, use auto bitrate.\n\n"

            "\t--rtpheader:\tWhether to add rtp header, 0-->not add, 1-->add.\n"
                "\t\t\tCan be set for each destination respectively, such as \"--rtpheader 1, 0\"\n"
            "\t--rtpssrc:\tCan be set for each destination respectively\n"
            "\t--rtpseqstart:\tRtp sequence number will begin with rtpseqstart\n"
                "\t\t\tCan be set for each destination respectively\n\n"

            "\t--lostrate:\tCan be set for each destination respectively\n"
            "\t--dropnum:\tHow many packets droped each time. can be set for each destination respectively\n\n"

            "\t--seekpos:\tStart offset by byte. No effect on loop\n"
            "\t--seektime:\tStart offset by second. NO effect on loop\n"

            "\t--startpos:\tStart offset by byte.\n"
            "\t--endpos:\tEnd offset by byte.\n"
            "\t--starttime:\tStart offset by second.\n"
            "\t--endtime:\tEnd offset by second.\n"

            "\t--speedscale:\tSend faster or slower. For example, 100 original speed, 50 half speed, 200 double speed.\n"
            );
}


static void* run_core(void *arg)
{
    tccore_start(m_core);
    return NULL;
}


//TODO release resource on failed?
static int parser_opt(int argc, char *const*argv)
{
    int ret;
    int long_index;
    tctask_t task;

    memset(&task, 0, sizeof(tctask_t));
    task.log_file_pos = 1;
    task.log_level_pcr = 1;
    task.min_pkt_size = 1316;
    task.max_pkt_size = 1400;

    optind = 0; //ATTENTION: reset getopt_long
    while ((ret = getopt_long(argc, argv, short_options, long_options, &long_index)) != -1)
    {
        switch (ret)
        {
            case 0:
                //long only options
                if (strncmp(long_options[long_index].name, "ttl", strlen("ttl")) == 0)
                {
                    task.ttl = atoi(optarg);
                    print_log("OPT", LOG_INFO, "ttl: %d\n", task.ttl);
                }
                else if (strncmp(long_options[long_index].name, "bitrate", strlen("biterate")) == 0)
                {
                    task.bitrate = atoi(optarg);
                    print_log("OPT", LOG_INFO, "bitrate: %dK\n", task.bitrate / 1024);
                }
                else if (strncmp(long_options[long_index].name, "rtpheader", strlen("rtpheader")) == 0)
                {
                    const char *p, *q;
                    int i;
                    p = optarg;
                    i = 0;
                    for (i = 0; i < MAX_DEST_NUM; i++)
                    {
                        q = strstr(p, ",");
                        if (atoi(p) > 0)
                            task.rtp_header[i] = 1;
                        else
                            task.rtp_header[i] = 0;
                        print_log("OPT", LOG_INFO, "rtp header (%d): %d\n", i, task.rtp_header[i]);
                        if (q != NULL)
                            p = q + 1;
                    }
                }
                else if (strncmp(long_options[long_index].name, "rtpssrc", strlen("rtpssrc")) == 0)
                {
                    const char *p, *q;
                    int i;
                    p = optarg;
                    i = 0;
                    for (i = 0; i < MAX_DEST_NUM; i++)
                    {
                        q = strstr(p, ",");
                        task.rtp_ssrc[i] = atoi(p);
                        print_log("OPT", LOG_INFO, "rtp ssrc (%d): %u\n", i, task.rtp_ssrc[i]);
                        if (q != NULL)
                            p = q + 1;
                    }
                }
                else if (strncmp(long_options[long_index].name, "rtpseqstart", strlen("rtpseqstart")) == 0)
                {
                    const char *p, *q;
                    int i;
                    p = optarg;
                    i = 0;
                    for (i = 0; i < MAX_DEST_NUM; i++)
                    {
                        q = strstr(p, ",");
                        task.rtp_seq_start[i] = atoi(p);
                        print_log("OPT", LOG_INFO, "rtp seq start (%d): %u\n", i, task.rtp_seq_start[i]);
                        if (q != NULL)
                            p = q + 1;
                    }
                }
                else if (strncmp(long_options[long_index].name, "lostrate", strlen("lostrate")) == 0)
                {
                    const char *p, *q;
                    int i;
                    p = optarg;
                    i = 0;
                    for (i = 0; i < MAX_DEST_NUM; i++)
                    {
                        if (p != NULL)
                        {
                            q = strstr(p, ",");
                            task.lost_rate[i] = atoi(p);
                        }
                        else
                        {
                            task.lost_rate[i] = 0;
                            q = NULL;
                        }
                        print_log("OPT", LOG_INFO, "(%d) lost rate:%d\n", i, task.lost_rate[i]);
                        if (q != NULL)
                            p = q + 1;
                        else 
                            p = NULL;
                    }
                }
                else if (strncmp(long_options[long_index].name, "dropnum", strlen("dropnum")) == 0)
                {
                    const char *p, *q;
                    int i;
                    p = optarg;
                    i = 0;
                    for (i = 0; i < MAX_DEST_NUM; i++)
                    {
                        if (p != NULL)
                        {
                            q = strstr(p, ",");
                            task.drop_num[i] = atoi(p);
                        }
                        else
                        {
                            task.drop_num[i] = 1;
                            q = NULL;
                        }
                        print_log("OPT", LOG_INFO, "(%d) drop num:%d\n", i, task.drop_num[i]);
                        if (q != NULL)
                            p = q + 1;
                        else
                            p = NULL;
                    }
                }
                else if (strncmp(long_options[long_index].name, "seekpos", strlen("seekpos")) == 0)
                {
                    task.seek_pos = atoi(optarg);
                    print_log("OPT", LOG_INFO, "seekpos: %d\n", task.seek_pos);
                }
                else if (strncmp(long_options[long_index].name, "seektime", strlen("seektime")) == 0)
                {
                    task.seek_time = atoi(optarg);
                    print_log("OPT", LOG_INFO, "seektime: %d\n", task.seek_time);
                }
                else if (strncmp(long_options[long_index].name, "startpos", strlen("startpos")) == 0)
                {
                    task.start_pos = atoi(optarg);
                    print_log("OPT", LOG_INFO, "startpos: %d\n", task.start_pos);
                }
                else if (strncmp(long_options[long_index].name, "endpos", strlen("endpos")) == 0)
                {
                    task.end_pos = atoi(optarg);
                    print_log("OPT", LOG_INFO, "endpos: %d\n", task.end_pos);
                }
                else if (strncmp(long_options[long_index].name, "starttime", strlen("starttime")) == 0)
                {
                    task.start_time = atoi(optarg);
                    print_log("OPT", LOG_INFO, "starttime: %d\n", task.start_time);
                }
                else if (strncmp(long_options[long_index].name, "endtime", strlen("endtime")) == 0)
                {
                    task.end_time = atoi(optarg);
                    print_log("OPT", LOG_INFO, "endtime: %d\n", task.end_time);
                }
                else if (strncmp(long_options[long_index].name, "speedscale", strlen("speedscale")) == 0)
                {
                    task.speed_scale = atoi(optarg);
                    print_log("OPT", LOG_INFO, "speedscale: %d\n", task.speed_scale);
                }
                break;
            case 'h':
                print_help(argv[0], stdout);
                //return 0;
                exit(0);
            case 'i':
                print_log("OPT", LOG_INFO, "input: %s\n", optarg);
                task.filename[1023] = '\0';
                strncpy(task.filename, optarg, 1023);
                break;
            case 'd':
                print_log("OPT", LOG_INFO, "destination: %s\n", optarg);
                {
                    const char *p, *q;
                    int i;
                    p = optarg;
                    i = 0;
                    for (; ;)
                    {
                        q = strstr(p, ",");
                        if (q == NULL)
                        {
                            strncpy(task.dest[i], p, 31);
                            break;
                        }
                        else
                        {
                            memcpy(task.dest[i], p, q - p);
                            p = q + 1;
                        }
                        i++;
                    }
                }
                break;
            case 'l':
                task.loopfile = atoi(optarg);
                print_log("OPT", LOG_INFO, "loopfile: %d\n", task.loopfile);
                break;
            case ':':
                print_log("OPT", LOG_ERROR, "parameter reqired!\n");
                print_help(argv[0], stderr);
                return -1;
            case '?':
                print_log("OPT", LOG_ERROR, "unspecified options!\n");
                print_help(argv[0], stderr);
                return -1;
            case -1:
                //处理完毕 
                break;
            default:
                abort();
        }
    }

    if (task.filename[0] != 0)
    {
        m_core = tccore_create(&task);
        if (m_core == NULL)
        {
            print_log("TSCAST", LOG_ERROR, "(%s) tccore_create FAILED!\n", __FUNCTION__);
            return -1;
        }
        if (pthread_create(&m_thrd, NULL, run_core, 0) != 0)
        {
            print_log("TSCAST", LOG_ERROR, "(%s) thread create FAILED!\n", __FUNCTION__);
            return -1;
        }
    }

    return 0;
}


static int tscast_init()
{
#ifdef WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 1), &wsaData) != 0) 
        return -1;
#endif
    return 0;
}


static void tscast_exit()
{
    //TODO stop and release channels

#ifdef WIN32
    WSACleanup();
#endif
}


extern int g_log_level;
extern FILE *g_fp_log;
static int state = 0;
static char cmd[1024];

static void process_cmd_log()
{
}

static void process_cmd()
{
    int log_level_bak = g_log_level;
    for (; ;)
    {
        memset(cmd, 0, 1024);
        fgets(cmd, 1023, stdin);

        if (strcmp(cmd, "q\n") == 0)
        {
            printf("======> quit!\n");
            //TODO close tcchannel
            return;
        }
        else if (strcmp(cmd, "p\n") == 0)
        {
            printf("======> pause!\n");
            tccore_pause(m_core);
            continue;
        }
        else if (strcmp(cmd, "c\n") == 0)
        {
            printf("======> continue!\n");
            tccore_resume(m_core);
            continue;
        }

        if (state == 0)
        {
            if (cmd[0] == '\n')
            {
                log_level_bak = g_log_level;
                g_log_level = LOG_ERROR;
            }
            state = 1;
            printf("======> command mode, input command or press return log mode...\n>");

            if (strncasecmp(cmd, "log", strlen("log")) == 0)
                process_cmd_log();
        }
        else if (state == 1)
        {
            if (cmd[0] == '\n')
                g_log_level = log_level_bak;
            state = 0;
        }
        usleep(100000);
    }
}


int main(int argc, char **argv)
{
    int ret;

    if (tscast_init() != 0)
        return -1;

    if ((ret = parser_opt(argc, argv)) != 0)
    {
        print_log("TCCORE", LOG_ERROR, "parser opt failed!\n");
        return ret;
    }

    process_cmd();

    tscast_exit();
    return 0;
}

