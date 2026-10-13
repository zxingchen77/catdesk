/* Host check: cc -std=c11 -Wall -Wextra -Werror -pedantic \
 *   -Ifirmware/components/catdesk_ui/include tools/test_link.c \
 *   firmware/components/catdesk_ui/catdesk_link.c \
 *   firmware/components/catdesk_ui/catdesk_model.c -o /tmp/catdesk-test-link
 */
#include "catdesk_link.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_hello(void)
{
    cd_model_t model;
    cd_model_init(&model, 1791216000);
    char reply[64];
    const cd_model_t before = model;
    const char *inputs[] = {"CD1 HELLO", "CD1 HELLO\n", "CD1 HELLO\r\n"};
    for (unsigned i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        assert(cd_link_parse(&model, inputs[i], reply, sizeof(reply)));
        assert(strcmp(reply, "CD1 READY CATDESK 1") == 0);
        assert(memcmp(&before, &model, sizeof(model)) == 0);
    }
}

static void test_invalid(void)
{
    const char *invalid[] = {
        "", "CD1", "HELLO", "CD2 HELLO", "cd1 HELLO", " CD1 HELLO",
        "CD1 HELLO ", "CD1 HELLO extra", "CD1  HELLO", "CD1\tHELLO",
        "CD1 HELLO\r", "CD1 HELLO\n\n", "CD1 HELLO\nCD1 HELLO",
        "CD1 HELLO\x7f", "CD1 HELLO\x80", "CD1 HELLO\x01",
        "CD1 TIME", "CD1 TIME ", "CD1 TIME +1791302400", "CD1 TIME -1",
        "CD1 TIME 1791302400.0", "CD1 TIME 1e9", "CD1 TIME 0x6a000000",
        "CD1 TIME 1704067199", "CD1 TIME 4102444801", "CD1 TIME 1791302400 extra",
        "CD1 TIME 1791302400 ", "CD1 TIME  1791302400", "CD1 TIME 1791302400\t",
        "CD1 TIME 18446744073709551616", "CD1 TIME 999999999999999999999999999999",
        "CD1 MAC", "CD1 MAC 0 0 0", "CD1 MAC 0 0 0 0 0", "CD1 MAC 0 0 0 0 ",
        "CD1 MAC -1 0 0 0", "CD1 MAC 101 0 0 0", "CD1 MAC 0 2 0 0",
        "CD1 MAC 0 0 101 0", "CD1 MAC 0 0 0 101", "CD1 MAC 0 0 0 -1",
        "CD1 MAC 0  0 0 0", "CD1 MAC 0 0 +1 0", "CD1 MAC 0 0 0.1 0",
        "CD1 MAC 18446744073709551616 0 0 0", "CD1 MAC 0\t0 0 0"
    };
    cd_model_t model;
    cd_model_init(&model, 1791216000);
    const cd_model_t before = model;
    char reply[64];
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        strcpy(reply, "previous reply");
        assert(!cd_link_parse(&model, invalid[i], reply, sizeof(reply)));
        assert(reply[0] == '\0');
        assert(memcmp(&before, &model, sizeof(model)) == 0);
    }
    char long_line[CD_LINK_MAX_LINE + 1];
    memset(long_line, '0', sizeof(long_line));
    memcpy(long_line, "CD1 TIME ", 9);
    long_line[CD_LINK_MAX_LINE] = '\0';
    assert(!cd_link_parse(&model, long_line, reply, sizeof(reply)));
    assert(reply[0] == '\0');
    assert(memcmp(&before, &model, sizeof(model)) == 0);
}

static void test_buffers(void)
{
    cd_model_t model;
    cd_model_init(&model, 1791216000);
    const cd_model_t before = model;
    char reply[64];
    assert(!cd_link_parse(&model, "CD1 TIME 1791302400", reply, 11));
    assert(reply[0] == '\0' && memcmp(&before, &model, sizeof(model)) == 0);
    assert(!cd_link_parse(&model, "CD1 MAC 90 1 12 70", reply, 10));
    assert(reply[0] == '\0' && memcmp(&before, &model, sizeof(model)) == 0);
    assert(!cd_link_parse(&model, "CD1 HELLO", reply, 18));
    assert(reply[0] == '\0');
    assert(!cd_link_parse(&model, "CD1 HELLO", NULL, 64));
    assert(!cd_link_parse(&model, "CD1 HELLO", reply, 0));
    assert(!cd_link_parse(&model, NULL, reply, sizeof(reply)));
    assert(reply[0] == '\0');
    assert(!cd_link_parse(NULL, "CD1 HELLO", reply, sizeof(reply)));
    assert(reply[0] == '\0');
    assert(cd_link_parse(&model, "CD1 TIME 1791302400", reply, 12));
    assert(strcmp(reply, "CD1 OK TIME") == 0);
}

static void test_mac(void)
{
    cd_model_t model;
    cd_model_init(&model, 1791216000);
    char reply[64];
    assert(cd_link_parse(&model, "CD1 MAC 78 1 12 63\n", reply, sizeof(reply)));
    assert(strcmp(reply, "CD1 OK MAC") == 0);
    assert(model.mac_connected && model.mac_charging);
    assert(model.mac_battery == 78 && model.mac_cpu == 12 && model.mac_memory == 63);
    assert(cd_link_parse(&model, "CD1 MAC 0 0 0 0", reply, sizeof(reply)));
    assert(model.mac_connected && !model.mac_charging);
    assert(model.mac_battery == 0 && model.mac_cpu == 0 && model.mac_memory == 0);
    assert(cd_link_parse(&model, "CD1 MAC 100 1 100 100\r\n", reply, sizeof(reply)));
    assert(model.mac_battery == 100 && model.mac_cpu == 100 && model.mac_memory == 100);
    assert(cd_link_parse(&model, "CD1 MAC 078 0 012 063", reply, sizeof(reply)));
    assert(model.mac_battery == 78 && model.mac_cpu == 12 && model.mac_memory == 63);
    cd_link_disconnect(&model);
    assert(!model.mac_connected && !model.mac_charging);
    assert(model.mac_battery == 78 && model.mac_cpu == 12 && model.mac_memory == 63);
    cd_link_disconnect(NULL);
}

static void test_time(void)
{
    cd_model_t model;
    cd_model_init(&model, 1791216000);
    char reply[64];
    assert(!model.clock_synced);
    assert(cd_link_parse(&model, "CD1 TIME 1704067200", reply, sizeof(reply)));
    assert(strcmp(reply, "CD1 OK TIME") == 0 && model.clock_synced);
    assert(model.year == 2024 && model.month == 1 && model.day == 1 && model.hour == 8);
    assert(cd_link_parse(&model, "CD1 TIME 4102444800", reply, sizeof(reply)));
    assert(model.year == 2100 && model.month == 1 && model.day == 1 && model.hour == 8);
    assert(cd_link_parse(&model, "CD1 TIME 1791302399", reply, sizeof(reply)));
    assert(model.year == 2026 && model.day == 6 && model.hour == 23);
    assert(model.days_to_anniversary == 1);
    cd_model_tick(&model, 1000);
    assert(model.anniversary_today && model.elapsed_days == 730 && model.celebration_active);
    cd_model_tick(&model, 10000);
    assert(!model.celebration_active);
    assert(cd_link_parse(&model, "CD1 TIME 1791302500", reply, sizeof(reply)));
    assert(!model.celebration_active); /* Periodic sync doesn't replay it. */
    cd_link_disconnect(&model);
    assert(model.clock_synced); /* Clock keeps running after the cable leaves. */

    cd_model_init(&model, 1791216000);
    model.popup = CD_POPUP_TODO;
    assert(cd_link_parse(&model, "CD1 TIME 1791302400", reply, sizeof(reply)));
    assert(model.anniversary_today && !model.celebration_active);
    model.popup = CD_POPUP_NONE;
    cd_model_tick(&model, 80);
    assert(model.celebration_active);
}

int main(void)
{
    test_hello();
    test_invalid();
    test_buffers();
    test_mac();
    test_time();
    puts("CatDesk link: protocol, strict input, overflow, buffer, Mac and clock checks passed.");
    return 0;
}
