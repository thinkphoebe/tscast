#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "utils.h"
#include "rtp.h"


int log_rtp_level = 1;


rtp_t* rtp_create(uint32_t ssrc, uint16_t base_seq, int payload_type, int clock_rate)
{
    rtp_t* h_rtp;

    h_rtp = (rtp_t *)malloc(sizeof(rtp_t));
    if (h_rtp == NULL)
    {
        print_log("RTP", LOG_ERROR, "malloc FAILED!\n");
        return NULL;
    }
    memset(h_rtp, 0, sizeof(rtp_t));

    h_rtp->ssrc = ssrc;
    h_rtp->sequence = base_seq;
    h_rtp->payload_type = payload_type;
    h_rtp->clock_rate = clock_rate;

    return h_rtp;
}


void rtp_pack_header(rtp_t *h_rtp, int marker, int64_t pts, uint8_t header[12])
{
    uint32_t timestamp = pts * (int64_t)h_rtp->clock_rate / INT64_C(1000000);
    memset(header, 0, 12);
    header[0] = 0x80;
    header[1] = (marker ? 0x80 : 0x00) | h_rtp->payload_type;
    header[2] = (h_rtp->sequence >> 8) & 0xFF;
    header[3] = (h_rtp->sequence     ) & 0xFF;
    header[4] = (timestamp >> 24) & 0xFF;
    header[5] = (timestamp >> 16) & 0xFF;
    header[6] = (timestamp >>  8) & 0xFF;
    header[7] = (timestamp      ) & 0xFF;
    memcpy(header + 8, &h_rtp->ssrc, 4);
    //h_rtp->timestamp = timestamp;
    h_rtp->sequence++;
}

