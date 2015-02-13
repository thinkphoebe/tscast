#ifdef WIN32
#include <windows.h>
#include <winbase.h>
#else
#include <sys/times.h>
#include <unistd.h> //for usleep
#endif

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h> //for va_list, va_start, va_end

#include "utils.h"

#ifdef WIN32
uint32_t get_tick()
{
    //return GetTickCount();
    return timeGetTime();
}
#else
uint32_t get_tick()
{
   struct tms tm;
   static uint32_t timeorigin;
   static int firsttimehere = 0;
   uint32_t now = times(&tm);
   if(firsttimehere == 0)
   {
       timeorigin = now;
       firsttimehere = 1;
   }

   //unsigned long HZ = sysconf(_SC_CLK_TCK); 
   //now的单位是tick,每秒的tick数, 上面代码可获得每秒的tick数, 应该是100
   //因此下面乘10转换为ms
   return (now - timeorigin)*10;  
}
#endif

int g_log_level = 1;
FILE *g_fp_log;

void print_log(const char *name, int control, const char *format, ...)
{
    va_list args;
    
    if (g_fp_log == NULL)
        g_fp_log = stdout;

    if (control < g_log_level)
        return;
    if (name == NULL)
        name = "NONAME";
    //printf("[%d](%s)", get_tick(), name);
    fprintf(g_fp_log, "[%d](%s)", get_tick(), name);

    va_start(args, format);
    //vprintf(format, args);
    vfprintf(g_fp_log, format, args);
    va_end(args);
}


//ATTENTION: 使用用第一个解析到的pcr_pid, 解析到后保存到传入参数pcr_pid中下次使用
int parser_pcr(int *pcr_pid, uint8_t *pkt, uint64_t *pcr/* out */)
{
    //uint8_t transport_error_indicator; 
    //uint8_t playload_unit_start_indicator; 
    //uint8_t transport_priority; 
    //uint8_t transport_scrambling_control; 
    uint8_t adaptation_field_control; 
    uint8_t adaptation_field_length; 
    //uint8_t continuity_counter; 
    uint16_t PID; 

    if (*pkt != 0x47)
    {
        print_log("pcr", LOG_ERROR, "tspacket error!\n");
        return -1; 
    }

    //transport_error_indicator = ((*(pkt + 1)) & 0x80) >> 7; 
    //playload_unit_start_indicator = ((*(pkt + 1)) & 0x40) >> 6; 
    //transport_priority = ((*(pkt + 1)) & 0x20) >> 5; 
    PID = ((*(pkt + 1)) & 0x1F); 
    PID = (PID << 8) + (*(pkt + 2)); 
    //transport_scrambling_control = ((*(pkt + 3)) & 0xC0) >> 6; 
    adaptation_field_control = ((*(pkt + 3)) & 0x30) >> 4; 
    //continuity_counter =  ((*(pkt + 3)) & 0x0F);

    //检查 adaptation_field_control, 并计算adaptation_field_length
    adaptation_field_length = 0; 
    if ((adaptation_field_control & 0x01) == 0)    
    {
        if (/*PID == tpobj->ppid && !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!1*/ (pkt[5] & 0x10)>0)
        {
            *pcr = pkt[6]; 
            *pcr = (*pcr << 8) + pkt[7]; 
            *pcr = (*pcr << 8) + pkt[8]; 
            *pcr = (*pcr << 8) + pkt[9]; 
            *pcr = (*pcr << 1); 
            //printf("--PID: %d, m_pcr: %"PRId64"\n", PID, m_pcr);
            goto GOT_PCR;
        }
        return -1; 
    }
    else if (adaptation_field_control == 0x03)
    {
        adaptation_field_length = pkt[4] + 1; 
    }
    else
    {
    }

    //if (PID == tpobj->ppid)
    {
        if ((adaptation_field_control == 0x2 && 0 < adaptation_field_length && adaptation_field_length == 184) 
                ||(adaptation_field_control == 0x3 && 0 < adaptation_field_length && adaptation_field_length < 184))
        {
            if ((pkt[5] & 0x10)>0)
            {
                *pcr = pkt[6]; 
                *pcr = (*pcr << 8) + pkt[7]; 
                *pcr = (*pcr << 8) + pkt[8]; 
                *pcr = (*pcr << 8) + pkt[9]; 
                *pcr = (*pcr << 1); 
                //printf("**PID: %d, m_pcr: %"PRId64"\n", PID, m_pcr);
                goto GOT_PCR;
            }
        }
    }

    return -1;

GOT_PCR:
    if (*pcr_pid == 0)
    {
        *pcr_pid = PID;
        print_log("pcr", LOG_INFO, "use 0x%x for pcr pid!\n", PID);
    }
    else if (PID != *pcr_pid)
    {
        return -1;
    }
    return 0;
}

