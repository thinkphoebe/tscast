#ifdef WIN32
#include <winsock.h>
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h> //for usleep
#include <inttypes.h> //for PRId64
#include <errno.h>

#include "utils.h"
#include "rtp.h"
#include "tccore.h"


struct _tccore
{
    tctask_t task;
    int state;
    int cmd; //0-->no cmd, 1-->pause, 2-->resume, 3-->stop

    uint8_t send_buf[SEND_BUF_SIZE]; //此大小的缓冲区内必须至少包括两个pcr
    uint8_t pkt[65536];

    int data_size; //当前缓冲区的数据量, 指向缓冲区的末尾
    int send_pos; //指向下一次发送数据的起始点
    int process_pos; //指向下一次处理pcr的位置

    uint64_t pcr; //当前使用的有效pcr, 即本次send的结束点
    int pcr_pos; 
    uint64_t last_pcr; //上一个使用的有效pcr
    int last_pcr_pos; 
    uint64_t rpcr; //上一个实际读取的pcr
    int rpcr_pos; 

    int64_t file_size;
    int loop_count;

    int eof; //0-->未读取到文件末尾, 1-->已读取到文件末尾, 2-->发送完最后一块数据
    int byterate; //每秒应发送的字节数
    uint64_t time_expect; //pcr_pos数据的理论发送时间, 根据pcr累加计算
    uint64_t time_begin; //记录每次从头播放时的初始time_expect, 用于计算当前数据的时间

    int drop_distance[MAX_DEST_NUM];
    int send_count[MAX_DEST_NUM];
    int drop_count[MAX_DEST_NUM];

    struct sockaddr_in peeraddr[MAX_DEST_NUM];
    int sockfd[MAX_DEST_NUM];
    rtp_t *rtp[MAX_DEST_NUM];
    FILE *fp;

    int pcr_pid;
};

static int reset(tccore_t *h, int timediff, int first_time);


static int update_rate(tccore_t *h, uint64_t new_pcr, uint64_t pcr)
{
    int lastrate;

    if (pcr <= 0)
        return 0;

    //force bitrate, nothing need to be computed!
    if (h->task.bitrate > 0)
    {
        h->byterate = h->task.bitrate / 8;
        return 0;
    }

    if (new_pcr <= pcr)
    {
        print_log("PCR", LOG_WARN, "pcr too small, last:%"PRId64", curr:%"PRId64", diff:%"PRId64"\n",
                pcr, new_pcr, new_pcr - pcr);
        return -1;
    }
    if (new_pcr > pcr + 2700000)
    {
        print_log("PCR", LOG_WARN, "pcr too big, last:%"PRId64", curr:%"PRId64", diff:%"PRId64"\n",
                pcr, new_pcr, new_pcr - pcr);
        return -1;
    }

    uint64_t curr = (uint64_t)get_tick() * 90;
    uint64_t expect = h->time_expect;

    if (h->byterate > 0)
        expect = expect - ((h->pcr_pos - h->send_pos) * 1000 * 90 / h->byterate);

    lastrate = h->byterate;

    int64_t diff = expect - curr;
    static int pcount = 0;
    if (diff > 500 || diff < -500)
    {
        print_log("PCR", 1, "expect:%"PRId64", curr:%"PRId64", pcount:%3d, timediff:%5"PRId64", pcrdiff:%7"PRId64"\n",
                expect, curr, pcount, diff / 90, diff);
        pcount = 0;
    }
    else
    {
       pcount++;
    }

    while ((int)(new_pcr - pcr + diff) <= 0 && (diff > 90 || diff < -90))
    {
        diff /= 2;
    }

    if ((int)(new_pcr - pcr + diff) <= 0)
    {
        printf("QQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQQ %"PRId64", %"PRId64", %"PRId64", %"PRId64", %d\n",
                new_pcr, pcr, diff, new_pcr - pcr, h->process_pos - h->pcr_pos);
        h->byterate = 12500000;
        //return -1;
    }
    else
        h->byterate = (int)((uint64_t)(h->process_pos - h->pcr_pos) * 90000 / (new_pcr - pcr + diff));

    //print_log("PCR", h->task.log_level_pcr, "update rate, lastpcr:%"PRId64", newpcr:%"PRId64", lastrate:%d, newrate:%d\n",
    //        pcr, new_pcr, lastrate / 128, h->byterate / 128);

    if (lastrate == 0)
    {
        if (h->byterate == 0)
        {
            printf("WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW %d,%d,%d,%"PRId64",%"PRId64",%"PRId64",%"PRId64"\n",
                    h->process_pos, h->pcr_pos, h->process_pos - h->pcr_pos, new_pcr, pcr, diff, new_pcr - pcr + diff);
            return -1;
        }

        h->time_expect += ((uint64_t)(h->process_pos - h->send_pos)) * 90000 / h->byterate;
    }
    h->time_expect += (new_pcr - pcr);

    if (h->task.end_time > 0 && (h->time_expect - h->time_begin) / 90000 > h->task.end_time - h->task.start_time
            && h->eof == 0)
    {
        print_log("SEEK", LOG_INFO, "reach end time:%d, file pos:%"PRId64"\n",
                (h->time_expect - h->time_begin) / 90000, ftello(h->fp));
        h->eof = 1;
    }

    return 0;
}


static void process_read(tccore_t *h)
{
    if (h->eof > 0)
        return;

    if (h->send_pos > 0)
    {
        int msize = h->data_size - h->send_pos;
        if (msize > 0)
            memmove(h->send_buf, h->send_buf + h->send_pos, msize);
        h->process_pos -= h->send_pos;
        h->data_size -= h->send_pos;
        if (h->pcr_pos >= h->send_pos)
            h->pcr_pos -= h->send_pos;
        else
            print_log("FILE", LOG_INFO, "1111111111111111111111111111111111111111111111111111 %d, %d\n", h->pcr_pos, h->send_pos);
        if (h->rpcr_pos >= h->send_pos)
            h->rpcr_pos -= h->send_pos;
        else
            print_log("FILE", LOG_INFO, "2222222222222222222222222222222222222222222222222222 %d, %d\n", h->rpcr_pos, h->send_pos);
        if (h->last_pcr_pos >= h->send_pos)
            h->last_pcr_pos -= h->send_pos;
        else
            print_log("FILE", LOG_INFO, "3333333333333333333333333333333333333333333333333333 %d, %d\n", h->last_pcr_pos, h->send_pos);
        h->send_pos = 0;
    }

    if (h->data_size - h->process_pos <= h->task.packet_size)
    {
        int readsize;
        int64_t fpos;

        if (SEND_BUF_SIZE - h->data_size <= 0)
        {
            print_log("FILE", LOG_INFO, "4444444444444444444444444444444444444444444444444444 %d, %d\n", h->data_size, h->process_pos);
            reset(h, 0, 0);
            return;
        }

        readsize = fread(h->send_buf + h->data_size, 1, SEND_BUF_SIZE - h->data_size, h->fp);

        if (readsize > 0)
            h->data_size += readsize;
        else
        {
            print_log("FILE", LOG_INFO, "read EOF!!!!!!!!!!!!!!!!\n");
            h->eof = 1;
        }

        fpos = ftello(h->fp);
        if (fpos < 1024 * 1024)
            print_log("FILE", h->task.log_file_pos, "loop count: %d, read pos: %.2fK, percent: %d%%\n",
                    h->loop_count, (double)fpos / 1024, fpos * 100 / h->file_size);
        else
            print_log("FILE", h->task.log_file_pos, "loop count: %d, read pos: %.2fM, percent: %"PRId64"%%\n",
                    h->loop_count, (double)fpos / 1024 / 1024, fpos * 100 / h->file_size);

        if (h->task.end_pos > 0 && fpos > h->task.end_pos)
        {
            print_log("SEEK", LOG_INFO, "reach end pos:%"PRId64", file pos:%"PRId64"\n",
                    (h->time_expect - h->time_begin) / 90000, fpos);
            h->eof = 1;
        }
    }
}


static void process_rate(tccore_t *h)
{
    while ((h->eof != 2 && h->pcr_pos - h->send_pos < h->task.max_pkt_size) || h->byterate == 0)
    {
        if (h->process_pos + h->task.packet_size > h->data_size && h->eof != 1)
            process_read(h);

        while (h->process_pos < h->data_size - h->task.packet_size)
        {
            if (h->send_buf[h->process_pos] == 0x47 && h->send_buf[h->process_pos + h->task.packet_size] == 0x47)
                break;
            h->process_pos++;
        }
        if (h->process_pos <= h->data_size - h->task.packet_size)
        {
            uint64_t pcr;
            if (parser_pcr(&h->pcr_pid, &h->send_buf[h->process_pos], &pcr) == 0)
            {
                if (h->task.speed_scale > 0 && h->task.speed_scale < 1000000)
                    pcr = pcr * 100 / h->task.speed_scale;

                if (update_rate(h, pcr, h->pcr) == 0)
                {
                    h->last_pcr = h->pcr;
                    h->last_pcr_pos = h->pcr_pos;
                    h->pcr = pcr;
                    h->pcr_pos = h->process_pos;
                }
                else
                {
                    if (pcr < h->pcr && pcr > h->rpcr)
                    {
                        print_log("PCR", LOG_WARN, "AAAAAAAAAAAAAAAA pcr reverse, pcr:%"PRId64", last pcr:%"PRId64", "
                                "last rpcr:%"PRId64", new pcr:%"PRId64"\n", h->pcr, h->last_pcr, h->rpcr, pcr);
                        h->pcr = h->rpcr;
                        h->pcr_pos = h->rpcr_pos;
                        if (update_rate(h, pcr, h->pcr) == 0)
                        {
                            h->last_pcr = h->pcr;
                            h->last_pcr_pos = h->pcr_pos;
                            h->pcr = pcr;
                            h->pcr_pos = h->process_pos;
                        }
                    }
                   else if (pcr > h->pcr + 900000 && h->rpcr > h->pcr + 900000
                           && pcr > h->rpcr && pcr < h->rpcr + 900000)
                   {
                        print_log("PCR", LOG_WARN, "AAAAAAAAAAAAAAAA pcr step, pcr:%"PRId64", last pcr:%"PRId64", "
                                "last rpcr:%"PRId64", new pcr:%"PRId64"\n", h->pcr, h->last_pcr, h->rpcr, pcr);
                        h->pcr = h->rpcr;
                        h->pcr_pos = h->rpcr_pos;
                        if (update_rate(h, pcr, h->pcr) == 0)
                        {
                            h->last_pcr = h->pcr;
                            h->last_pcr_pos = h->pcr_pos;
                            h->pcr = pcr;
                            h->pcr_pos = h->process_pos;
                        }
                   }
                }
                h->rpcr = pcr;
                h->rpcr_pos = h->process_pos;
            }
            h->process_pos += h->task.packet_size;
        }
        else if (h->eof >= 1)
        {
            h->process_pos = h->data_size;
            break;
        }
    }
}


static void process_send(tccore_t *h)
{
    int end_pos;
    uint8_t *pdata;
    int datasize;
    int sendsize;
    int i;

    if (h->send_pos >= h->pcr_pos || h->byterate == 0)
        return;

    //ATTENTION this may cause some data not send!!!!!!!!!!!
    while (h->send_pos < h->pcr_pos - h->task.packet_size)
    {
        if (h->send_buf[h->send_pos] == 0x47 && h->send_buf[h->send_pos + h->task.packet_size] == 0x47)
            break;
        h->send_pos++;
    }

    if (h->byterate == 0)
    {
        printf("EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE\n");
        return;
    }

    int sleepcount = 0;

    //send packet until next h->pcr
    for (;;)
    {
        end_pos = h->send_pos + h->task.min_pkt_size;

        while (h->send_buf[end_pos] != 0x47 
                && end_pos - h->send_pos >= h->task.max_pkt_size
                && end_pos < h->pcr_pos)
            end_pos++;

        if (h->eof == 1)
        {
            if (h->send_pos >= h->data_size)
            {
                printf("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF %d, %d\n", h->send_pos, h->data_size);
                h->eof = 2;
                break;
            }
            if (end_pos >= h->pcr_pos)
            {
                if (end_pos >= h->pcr_pos && h->process_pos + h->task.packet_size < h->data_size)
                    break;
                if (end_pos > h->data_size)
                {
                    print_log("SEND", LOG_INFO, "send the last!!!!!!!!!!!!!!!!\n");
                    end_pos = h->data_size;
                }
            }
        }
        else if (end_pos >= h->pcr_pos)
            break;

        sendsize = 0;
        for (i = 0; i < MAX_DEST_NUM; i++)
        {
            if (h->task.rtp_header[i] && h->rtp[i] != NULL)
            {
                uint64_t pts = h->pcr - (h->pcr_pos - h->send_pos) * 90000 / h->byterate;

                pdata = h->pkt;
                rtp_pack_header(h->rtp[i], 0, pts, h->pkt);
                memcpy(h->pkt + 12, &h->send_buf[h->send_pos], end_pos - h->send_pos);
                datasize = 12 + (end_pos - h->send_pos);
            }
            else
            {
                pdata = &h->send_buf[h->send_pos];
                datasize = end_pos - h->send_pos;
            }

            if (h->task.lost_rate[i] > 0 && h->send_count[i] >= h->drop_distance[i] - h->task.drop_num[i])
            {
                h->drop_count[i]++;
                if (h->drop_count[i] >= h->task.drop_num[i])
                {
                    h->send_count[i] = 0;
                    h->drop_count[i] = 0;
                }
            }
            else
            {
                if (h->sockfd[i] != -1)
                {
                    if (h->task.tcp)
                        sendsize = send(h->sockfd[i], pdata, datasize, 0);
                    else
                        sendsize = sendto(h->sockfd[i], pdata, datasize, 0, (struct sockaddr *)&h->peeraddr[i], 
                                sizeof(struct sockaddr_in));
                    if (sendsize < 0)
                    {
                        print_log("SEND", LOG_ERROR, "send packet error!\n");
                    }
                }
            }

            h->send_count[i]++;
        }

        h->send_pos = end_pos;

        if (sendsize * 1000 / h->byterate > 100)
           print_log("SEND", LOG_WARN, "delay time is too long, byte rate: %d, delay: %d\n",
                   h->byterate, sendsize * 1000 / h->byterate);

        sleepcount += (sendsize * 1000 * 1000 / h->byterate);
        if (sleepcount > 5000)
        {
            usleep(sleepcount);
            //struct timeval tv;
            //tv.tv_sec = 0;
            //tv.tv_usec = sleepcount;
            //select(0, NULL, NULL, NULL, &tv);
            sleepcount = 0;
        }
        //int sleeptime = (h->time_expect - (h->pcr_pos - end_pos) * 1000 * 90 / h->byterate) * 100 / 9 - get_tick() * 1000;
        //if (sleeptime > 1000)
        //    usleep(sleeptime);
    }

    if (sleepcount > 0)
    {
        usleep(sleepcount);
    }
    //int sleeptime = (h->time_expect - (h->pcr_pos - end_pos) * 1000 * 90 / h->byterate) * 100 / 9 - get_tick() * 1000;
    //if (sleeptime > 15000)
    //    usleep(sleeptime);
}


static int seek2time(tccore_t *h, uint32_t totime)
{
    uint64_t time = 0; 
    uint64_t last_pcr = 0;
    int read_size = 0;

    fseeko(h->fp, 0, SEEK_SET);
    h->data_size = 0;
    h->process_pos = 0;

    for (; ;)
    {
        read_size = fread(h->send_buf, 1, SEND_BUF_SIZE - h->data_size, h->fp);
        if (read_size <= 0)
            break;
        h->data_size += read_size;

        while (h->data_size >= h->process_pos + h->task.packet_size * 2)
        {
            while (h->data_size >= h->process_pos + h->task.packet_size * 2 && (h->send_buf[h->process_pos] != 0x47
                    || h->send_buf[h->process_pos + h->task.packet_size] != 0x47))
                h->process_pos++;

            if (h->data_size >= h->process_pos + h->task.packet_size * 2 && h->send_buf[h->process_pos] == 0x47
                    && h->send_buf[h->process_pos + h->task.packet_size] == 0x47)
            {
                uint64_t pcr;
                if (parser_pcr(&h->pcr_pid, &h->send_buf[h->process_pos], &pcr) == 0)
                {
                    if (h->task.speed_scale > 0 && h->task.speed_scale < 1000000)
                        pcr = pcr * 100 / h->task.speed_scale;

                    if (last_pcr == 0)
                        last_pcr = pcr;
                    if (pcr > last_pcr && pcr < last_pcr + 2700000)
                        time += (pcr - last_pcr);
                    if (time / 90000 > totime)
                    {
                        fseeko(h->fp, ftello(h->fp) - h->process_pos, SEEK_SET);
                        print_log("SEEK", LOG_INFO, "seek to time: %d, file pos: %"PRId64"\n", time / 90000, ftello(h->fp));
                        return 0;
                    }
                    last_pcr = pcr;
                }
            }
            h->process_pos += h->task.packet_size;
        }

        if (h->data_size > h->process_pos)
            memmove(h->send_buf, h->send_buf + h->process_pos, h->data_size - h->process_pos);
        h->data_size -= h->process_pos;
        h->process_pos = 0;
    }

    return -1;
}


static int reset(tccore_t *h, int timediff, int first_time)
{
    if (first_time == 0)
    {
        if (h->task.start_pos > 0 && h->task.start_pos < h->file_size)
            fseeko(h->fp, h->task.start_pos, SEEK_SET);
        else if (h->task.start_time > 0)
        {
            if (seek2time(h, h->task.start_time) == 0)
                timediff = h->task.start_time;
            else
                fseeko(h->fp, 0, SEEK_SET);
        }
        else
            fseeko(h->fp, 0, SEEK_SET);
    }
    else
    {
        print_log("RESET", LOG_INFO, "first time, check seekpos and seek time\n");
        if (h->task.seek_pos > 0 && h->task.seek_pos < h->file_size)
            fseeko(h->fp, h->task.seek_pos, SEEK_SET);
        else if (h->task.seek_time > 0)
        {
            if (seek2time(h, h->task.seek_time) == 0)
                timediff = h->task.seek_time;
            else
                fseeko(h->fp, 0, SEEK_SET);
        }
        else
            fseeko(h->fp, 0, SEEK_SET);
    }

    h->data_size = 0;
    h->send_pos = 0;
    h->process_pos = 0;
    h->pcr_pos = 0;
    h->eof = 0;
    h->pcr = 0;
    h->last_pcr = 0;
    h->byterate = 0;
    h->time_expect = (uint64_t)get_tick() * 90 + timediff;
    h->time_begin = h->time_expect;
    return 0;
}


static int net_setup(tccore_t *h)
{
    //struct in_addr if_req;
    struct ip_mreq mreq;
    uint32_t dest_ip;
    uint16_t dest_port;
    char ipstr[16];
    int mc_loop;
    char *p;
    int i;

    for (i = 0; i < MAX_DEST_NUM; i++)
    {
        if (h->task.dest[i][0] == 0)
            break;

        p = strstr(h->task.dest[i], ":");
        if (p == NULL)
        {
            print_log("NET", LOG_ERROR, "invalid destination!\n");
            return -1;
        }
        memset(ipstr, 0, 16);
        memcpy(ipstr, h->task.dest[i], p - h->task.dest[i]);
        dest_ip = inet_addr(ipstr);
        dest_port = htons(atoi(p + 1));
        print_log("NET", LOG_INFO, "dest_ip:%s, dest_port:%s\n", ipstr, p + 1);

        h->sockfd[i] = socket(AF_INET, SOCK_DGRAM, 0);
        if (h->sockfd[i] < 0)
        {
            print_log("NET", LOG_ERROR, "socket create error!\n");
            return -1;
        }

        memset(&h->peeraddr[i], 0, sizeof(struct sockaddr_in));
        h->peeraddr[i].sin_family = AF_INET;
        h->peeraddr[i].sin_addr.s_addr = dest_ip;
        h->peeraddr[i].sin_port = dest_port;

        if (h->task.tcp)
        {
            h->sockfd[i] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (connect(h->sockfd[i], (struct sockaddr *)&h->peeraddr[i], sizeof(struct sockaddr)) < 0)
            {
                print_log("NET", LOG_ERROR, "failed to connect [%s:%d]. %d\n", ipstr, dest_port, strerror(errno));
                return -1;
            }
            continue;
        }

        if (IS_MULTICAST_IP(ntohl(dest_ip)))
        {
            //设置要加入组播的地址
            memset(&mreq, 0, sizeof(struct ip_mreq));
            //设置发送组播消息的源主机的地址信息
            mreq.imr_multiaddr.s_addr = dest_ip;
            mreq.imr_interface.s_addr = htonl(INADDR_ANY);

            //把本机加入组播地址，即本机网卡作为组播成员，只有加入组才能收到组播消息
            if (setsockopt(h->sockfd[i], IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(struct ip_mreq)) == -1)
            {
                print_log("NET", LOG_ERROR, "setsockopt ERROR!\n");
                //return -1;
            }

            //TODO select a interface
            //inet_aton(INADDR_ANY, &if_req);
            //if (setsockopt(h->sockfd, IPPROTO_IP, IP_MULTICAST_IF, (char *)&if_req, sizeof(struct in_addr)) < 0)
            //{
            //    print_log("NET", LOG_ERROR, "set interface FAILED!\n");
            //    return -1;
            //}

            mc_loop = 1;
            setsockopt(h->sockfd[i], IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&mc_loop, sizeof(mc_loop));

            if (h->task.rtp_header[i])
            {
                print_log("RTP", LOG_INFO, "index:%d, rtp header:%d, rtp ssrc:%d, rtp seq start:%d\n",
                       i, h->task.rtp_header[i], h->task.rtp_ssrc[i], h->task.rtp_seq_start[i]);
                h->rtp[i] = rtp_create(h->task.rtp_ssrc[i], h->task.rtp_seq_start[i], 33, 90000);
                if (h->rtp[i] == NULL)
                {
                    print_log("TCCORE", LOG_ERROR, "rtp create failed!\n");
                    return -1;
                }
            }
        }
        
        if (h->task.ttl > 0)
        {
            print_log("NET", LOG_INFO, "ttl:%d\n", h->task.ttl);
            if (setsockopt(h->sockfd[i], IPPROTO_IP, IP_MULTICAST_TTL, (char *)&h->task.ttl, sizeof(h->task.ttl)) < 0)
                print_log("NET", LOG_ERROR, "set TTL FAILED! ttl value:%d\n", h->task.ttl);
        }
    }

    return 0;
}


tccore_t* tccore_create(tctask_t *task)
{
    tccore_t *h;
    int i;

    h = (tccore_t *)malloc(sizeof(tccore_t));
    if (h == NULL)
    {
        //log error
        return NULL;
    }
    memset(h, 0, sizeof(tccore_t));
    for (i = 0; i < MAX_DEST_NUM; i++)
        h->sockfd[i] = -1;
    h->task = *task;

    print_log("TCCORE", LOG_INFO, "filename:%s\n", h->task.filename);
    h->fp = fopen(h->task.filename, "rb");
    if (h->fp == NULL)
    {
        print_log("TCCORE", LOG_ERROR, "fopen failed! file:%s\n", h->task.filename);
        goto FAIL;
    }

    //get file size, unnecessary, just for log print
    fseeko(h->fp, 0, SEEK_END);
    h->file_size = ftello(h->fp);
    if (h->file_size < 1024 * 1024)
        print_log("FILE", h->task.log_file_pos, "file size: %.2fK\n", (double)h->file_size / 1024);
    else
        print_log("FILE", h->task.log_file_pos, "file size: %.2fM\n", (double)h->file_size / 1024 / 1024);
    fseeko(h->fp, 0, SEEK_SET);

    if (net_setup(h) != 0)
        goto FAIL;

    for (i = 0; i < MAX_DEST_NUM; i++)
    {
        if (h->task.lost_rate[i] > 0)
        {
            if (h->task.drop_num[i] <= 0 || h->task.drop_num[i] >= 10000)
                h->task.drop_num[i] = 1;
            h->drop_distance[i] = 1000 * h->task.drop_num[i] / h->task.lost_rate[i];
            print_log("TCCORE", LOG_INFO, "(%d) lost rate:%d%%/1000, drop num:%d, drop distance:%d\n",
                    i, h->task.lost_rate[i], h->task.drop_num[i], h->drop_distance[i]);
        }
    }

    if (h->task.bitrate > 0)
        print_log("TCCORE", LOG_INFO, "force bitrate:%d\n", h->task.bitrate);
    print_log("TCCORE", LOG_INFO, "loopfile:%d\n", h->task.loopfile);
    print_log("TCCORE", LOG_INFO, "startpos:%"PRId64", endpos:%"PRId64", starttime:%"PRId64", endtime:%"PRId64"\n",
            h->task.start_pos, h->task.end_pos, h->task.start_time, h->task.end_time);

    reset(h, 0, 1);

    return h;

FAIL:
    for (i = 0; i < MAX_DEST_NUM; i++)
        if (h->sockfd[i] >= 0) close(h->sockfd[i]);
    if (h->rtp != NULL) free(h->rtp);
    if (h->fp != NULL) fclose(h->fp);
    free(h);
    return NULL;
}


void tccore_destory(tccore_t *h)
{
    int i;
    for (i = 0; i < MAX_DEST_NUM; i++)
        if (h->sockfd[i] >= 0) close(h->sockfd[i]);
    if (h->rtp != NULL)
        free(h->rtp);
    if (h->fp != NULL)
        fclose(h->fp);
    free(h);
}


void tccore_start(tccore_t *h)
{
    h->state = 1;

    for (;;)
    {
        //process command
        if (h->cmd > 0)
        {
            if (h->cmd == 1)
            {
                h->state = 2;
                usleep(100000);
                continue;
            }
            else if (h->cmd == 2 && h->state == 2)
            {
                h->state = 1;
                h->time_expect = (uint64_t)get_tick() * 90;
                h->time_expect += ((uint64_t)(h->process_pos - h->send_pos)) * 90000 / h->byterate;
            }
            else if (h->cmd == 3)
            {
                reset(h, 0, 0);
                h->state = 0;
                return;
            }
        }

        process_send(h);

        if (h->eof == 2)
        {
            if (h->task.loopfile == -1 || (h->task.loopfile > 0 && h->loop_count < h->task.loopfile))
            {
                uint64_t timediff;  
                uint64_t cur = get_tick();
                //compute the expect time for the last section
                h->time_expect += (h->process_pos - h->pcr_pos) * 90000 / h->byterate;
                //add time difference to next loop
                timediff = h->time_expect - cur * 90;
                print_log("TCCORE", LOG_INFO, "End reset! time expect:%d, current:%d, pcr diff:%"PRId64"\n",
                        h->time_expect / 90, cur, timediff);
                reset(h, timediff, 0);

                h->loop_count++;
                print_log("TCCORE", LOG_INFO, "loop go to file begin, count: %d\n", h->loop_count);
                
                usleep(100000);
            }
            else
            {
                break;
            }
        }

        process_rate(h);
    }
}


int tccore_stop(tccore_t *h)
{
    int count = 0;
    h->cmd = 3;
    while (h->state != 0 && count < 100)
    {
        usleep(50000);
        count++;
    }
    return (h->state == 0) ? 0 : -1;
}


int tccore_pause(tccore_t *h)
{
    int count = 0;
    if (h->state != 1)
        return -1;
    h->cmd = 1;
    while (h->state != 2 && count < 100)
    {
        usleep(50000);
        count++;
    }
    return (h->state == 2) ? 0 : -1;
}


int tccore_resume(tccore_t *h)
{
    int count = 0;
    if (h->state != 2)
        return -1;
    h->cmd = 2;
    while (h->state != 1 && count < 100)
    {
        usleep(50000);
        count++;
    }
    return (h->state == 1) ? 0 : -1;
}


int tccore_get_state(tccore_t *h)
{
    return h->state;
}


tctask_t* tccore_get_task(tccore_t *h)
{
    return &h->task;
}


int tccore_get_loop(tccore_t *h)
{
    return h->loop_count;
}


int tccore_get_percent(tccore_t *h)
{
    uint64_t fpos;
    fpos = ftello(h->fp);
    return (int)(fpos * 100 / h->file_size);
}

