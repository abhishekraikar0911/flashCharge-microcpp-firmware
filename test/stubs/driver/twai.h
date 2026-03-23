#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mock of ESP-IDF TWAI message structure for native testing
typedef struct {
    uint32_t identifier;
    uint32_t extd:1;
    uint32_t rtr:1;
    uint32_t ss:1;
    uint32_t self:1;
    uint32_t dlc_non_comp:1;
    uint32_t reserved:27;
    uint8_t data_length_code;
    uint8_t data[8];
} twai_message_t;

#ifdef __cplusplus
}
#endif
