#ifndef __UTILS_H__
#define __UTILS_H__

#ifdef __cplusplus
extern "C" {
#endif


#ifdef WIN32
#define PRId64 "I64d"
#endif

#define IS_MULTICAST_IP(ip) (0xE0000000 <= (ip & 0xFF000000) && (ip & 0xFF000000) < 0xF0000000)

typedef enum
{
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR,
} log_level;

uint32_t get_tick();
void print_log(const char *name, int control, const char *format, ...);
int parser_pcr(int *pcr_pid, uint8_t *pkt, uint64_t *pcr/* out */);


#ifdef __cplusplus
}
#endif

#endif /* __UTILS_H__ */

