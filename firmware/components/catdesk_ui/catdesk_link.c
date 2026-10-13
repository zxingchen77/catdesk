#include "catdesk_link.h"

#include <stdint.h>
#include <string.h>

#define MIN_EPOCH UINT64_C(1704067200) /* 2024-01-01 00:00 UTC. */
#define MAX_EPOCH UINT64_C(4102444800) /* 2100-01-01 00:00 UTC. */

static bool take_uint(const char **cursor, uint64_t *value)
{
    const char *p = *cursor;
    if (*p < '0' || *p > '9') return false;
    uint64_t result = 0;
    while (*p >= '0' && *p <= '9') {
        const unsigned digit = (unsigned)(*p - '0');
        if (result > (UINT64_MAX - digit) / 10) return false;
        result = result * 10 + digit;
        ++p;
    }
    *cursor = p;
    *value = result;
    return true;
}

static bool take_space(const char **cursor)
{
    if (**cursor != ' ') return false;
    ++*cursor;
    return true;
}

static bool write_reply(char *reply, size_t cap, const char *message)
{
    const size_t length = strlen(message);
    if (cap <= length) return false;
    memcpy(reply, message, length + 1);
    return true;
}

bool cd_link_parse(cd_model_t *model, const char *line, char *reply, size_t cap)
{
    if (reply != NULL && cap != 0) reply[0] = '\0';
    if (model == NULL || line == NULL || reply == NULL || cap == 0) return false;

    size_t length = 0;
    while (length < CD_LINK_MAX_LINE && line[length] != '\0') ++length;
    if (length == CD_LINK_MAX_LINE) return false;
    if (length != 0 && line[length - 1] == '\n') {
        --length;
        if (length != 0 && line[length - 1] == '\r') --length;
    }
    char command[CD_LINK_MAX_LINE];
    for (size_t i = 0; i < length; ++i) {
        const unsigned char c = (unsigned char)line[i];
        if (c < 32 || c > 126) return false;
        command[i] = (char)c;
    }
    command[length] = '\0';

    if (strcmp(command, "CD1 HELLO") == 0) {
        return write_reply(reply, cap, "CD1 READY CATDESK 1");
    }
    if (strncmp(command, "CD1 TIME ", 9) == 0) {
        const char *cursor = command + 9;
        uint64_t epoch;
        if (!take_uint(&cursor, &epoch) || *cursor != '\0'
            || epoch < MIN_EPOCH || epoch > MAX_EPOCH) return false;
        if (!write_reply(reply, cap, "CD1 OK TIME")) return false;
        cd_model_set_time(model, (int64_t)epoch);
        model->clock_synced = true;
        return true;
    }
    if (strncmp(command, "CD1 MAC ", 8) == 0) {
        const char *cursor = command + 8;
        uint64_t battery, charging, cpu, memory;
        if (!take_uint(&cursor, &battery) || !take_space(&cursor)
            || !take_uint(&cursor, &charging) || !take_space(&cursor)
            || !take_uint(&cursor, &cpu) || !take_space(&cursor)
            || !take_uint(&cursor, &memory) || *cursor != '\0'
            || battery > 100 || charging > 1 || cpu > 100 || memory > 100) return false;
        if (!write_reply(reply, cap, "CD1 OK MAC")) return false;
        model->mac_connected = true;
        model->mac_battery = (uint8_t)battery;
        model->mac_charging = charging != 0;
        model->mac_cpu = (uint8_t)cpu;
        model->mac_memory = (uint8_t)memory;
        return true;
    }
    return false;
}

void cd_link_disconnect(cd_model_t *model)
{
    if (model == NULL) return;
    model->mac_connected = false;
    model->mac_charging = false;
    model->mac_battery = 78;
    model->mac_cpu = 12;
    model->mac_memory = 63;
}
