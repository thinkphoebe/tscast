#ifndef __RTP_H__
#define __RTP_H__

#ifdef __cplusplus
extern "C" {
#endif


typedef struct _rtp
{
    uint32_t    timestamp;
    uint16_t    sequence;
    uint8_t     payload_type;
    uint32_t    ssrc;   
    int         clock_rate;
} rtp_t;


rtp_t* rtp_create(uint32_t ssrc, uint16_t base_seq, int payload_type, int clock_rate);
void rtp_pack_header(rtp_t *h_rtp, int marker, int64_t pts, uint8_t header[12]);


#ifdef __cplusplus
}
#endif

#endif /* __RTP_H__ */

