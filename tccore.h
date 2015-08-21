#ifndef __TCCORE_H__
#define __TCCORE_H__

#ifdef __cplusplus
extern "C" {
#endif


#define SEND_BUF_SIZE (188 * 1000 * 10)
#define MAX_DEST_NUM 16

typedef struct _tccore tccore_t;

typedef struct _tctask
{
    char filename[1024];
    int loopfile; //0-->不循环, -1-->无限循环, > 0 -->循环次数
    int ttl;
    int bitrate;
    int tcp;

    char dest[MAX_DEST_NUM][32];

    int lost_rate[MAX_DEST_NUM];
    int drop_num[MAX_DEST_NUM];

    int rtp_header[MAX_DEST_NUM];
    uint32_t rtp_ssrc[MAX_DEST_NUM];
    int rtp_seq_start[MAX_DEST_NUM];

    //初始播放时seek到相应的位置, 循环播放时依然从头开始
    int64_t seek_time;
    int64_t seek_pos;

    //循环播放时依然从start_pos或start_time开始
    int64_t start_pos;
    int64_t end_pos;
    int64_t start_time;
    int64_t end_time;

    int speed_scale; //发包速率, 100为pcr速率

    int log_level_pcr;
    int log_file_pos;
    int min_pkt_size;
    int max_pkt_size; //不应超过mtu, 尤其是rtp时

    int packet_size;
} tctask_t;

tccore_t* tccore_create(tctask_t *task);
void tccore_destory(tccore_t *h);

//在当前线程中执行, 如果不希望被阻塞, 应单独创建线程调用
void tccore_start(tccore_t *h);
int tccore_stop(tccore_t *h);
int tccore_pause(tccore_t *h);
int tccore_resume(tccore_t *h);

//0-->stoped, 1-->running, 2-->paused
int tccore_get_state(tccore_t *h);

tctask_t* tccore_get_task(tccore_t *h);

//当前已循环播放的次数, 第一次为0
int tccore_get_loop(tccore_t *h);

//读取本地循环文件位置的百分比
int tccore_get_percent(tccore_t *h);


#ifdef __cplusplus
}
#endif

#endif /* __TCCORE_H__ */

