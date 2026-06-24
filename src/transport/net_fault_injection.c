#define _POSIX_C_SOURCE 200809L

#include "net/fault_injection.h"

#include "ui/log.h"

#include <stdio.h>
#include <time.h>
#include <unistd.h>

struct fault_injection_config {
    int configured;
    unsigned int percent;
    uint32_t state;
};

static struct fault_injection_config fault_config;

void net_fault_configure_bitflip(unsigned int percent, int has_seed,
                                 unsigned int seed)
{
    fault_config.configured = 1;
    fault_config.percent = percent;
    fault_config.state = has_seed ?
                         (uint32_t)seed :
                         ((uint32_t)time(NULL) ^ (uint32_t)getpid());
    if (fault_config.state == 0U) {
        fault_config.state = 1U;
    }

    if (fault_config.percent > 0U) {
        ui_log(stdout, UI_LOG_WARN,
               "fault-injection bitflip=%u%% seed=%u\n",
               fault_config.percent, (unsigned int)fault_config.state);
    }
}

static uint32_t fault_random(void)
{
    fault_config.state = (fault_config.state * 1103515245U) + 12345U;
    return fault_config.state;
}

int net_fault_maybe_flip_bit(uint8_t *frame, size_t frame_len,
                             const char *label)
{
    size_t byte_index;
    unsigned int bit_index;
    uint8_t mask;
    uint8_t before;

    if (!fault_config.configured || fault_config.percent == 0U ||
        frame == NULL || frame_len == 0U) {
        return 0;
    }
    if ((fault_random() % 100U) >= fault_config.percent) {
        return 0;
    }

    byte_index = (size_t)(fault_random() % (uint32_t)frame_len);
    bit_index = (unsigned int)(fault_random() % 8U);
    mask = (uint8_t)(1U << bit_index);
    before = frame[byte_index];
    frame[byte_index] = (uint8_t)(before ^ mask);

    ui_log(stdout, UI_LOG_WARN,
           "fault-injection bitflip label=%s byte=%zu bit=%u antes=0x%02x depois=0x%02x\n",
           label == NULL ? "-" : label,
           byte_index, bit_index, before, frame[byte_index]);
    return 1;
}
