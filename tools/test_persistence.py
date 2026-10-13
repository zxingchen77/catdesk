#!/usr/bin/env python3
"""Exercise the firmware's actual NVS load/save functions on the host.

The extracted functions are compiled with small NVS API stubs and the real
CatDesk model. This validates serialization, reload sanitization, defaults and
failure handling; it does not emulate flash wear, real power loss, or claim a
physical NVS transaction is atomic. No board or serial port is opened.
"""

from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
UI = ROOT / "firmware/components/catdesk_ui/catdesk_ui.c"


def extract_function(source: str, name: str) -> str:
    match = re.search(r"static\s+(?:int64_t|void)\s+" + re.escape(name)
                      + r"\s*\([^)]*\)\s*\{", source)
    if not match:
        raise RuntimeError(f"Actual firmware function {name} not found")
    depth, state, escaped = 1, "code", False
    i = match.end()
    while i < len(source):
        char = source[i]
        next_char = source[i + 1:i + 2]
        if state == "line":
            if char == "\n":
                state = "code"
        elif state == "comment":
            if char == "*" and next_char == "/":
                state = "code"
                i += 1
        elif state in ("string", "char"):
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == ('"' if state == "string" else "'"):
                state = "code"
        elif char == "/" and next_char in ("/", "*"):
            state = "line" if next_char == "/" else "comment"
            i += 1
        elif char == '"':
            state = "string"
        elif char == "'":
            state = "char"
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():i + 1]
        i += 1
    raise RuntimeError(f"Unterminated firmware function {name}")


STUBS = r'''
#include "catdesk_model.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int esp_err_t;
typedef int nvs_handle_t;
typedef int esp_lcd_panel_handle_t;
enum { ESP_OK=0, NVS_READWRITE=1, ERR_MISSING=2, ERR_INIT=3,
       ERR_OPEN=4, ERR_LENGTH=5, ERR_WRITE=6, ERR_COMMIT=7 };
static cd_model_t s_model;
static nvs_handle_t s_nvs;
static esp_lcd_panel_handle_t s_panel = 1;
static bool s_nvs_ready, s_settings_dirty;
static int64_t s_last_save_us, s_last_change_us;
static const char *TAG="CATDESK";
static unsigned warnings, erase_calls, write_calls, commit_calls, resync_calls;
#define ESP_LOGW(tag,format,...) do { (void)(tag); (void)(format); ++warnings; } while(0)
const char *esp_err_to_name(esp_err_t error) { (void)error; return "stub error"; }
esp_err_t esp_lcd_rgb_panel_restart(esp_lcd_panel_handle_t panel)
{ assert(panel == 1); ++resync_calls; return ESP_OK; }

typedef struct {
    bool has_brightness, has_count, has_checks, has_blob, has_epoch;
    uint8_t brightness, count, checks, ids[16];
    size_t ids_size;
    int64_t epoch;
} saved_t;
static saved_t persisted, pending;
static int init_error, open_error, commit_error;
static const char *write_error_key, *read_error_key;
static const int64_t OCT_6_MIDNIGHT=INT64_C(1791216000);

esp_err_t nvs_flash_init(void) { return init_error; }
/* Available stubs make any accidental erase observable. */
esp_err_t nvs_flash_erase(void) { ++erase_calls; return ESP_OK; }
esp_err_t nvs_flash_erase_partition(const char *name)
{ (void)name; ++erase_calls; return ESP_OK; }
esp_err_t nvs_erase_all(nvs_handle_t handle)
{ (void)handle; ++erase_calls; return ESP_OK; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    assert(strcmp(name,"catdesk")==0 && mode==NVS_READWRITE);
    if(open_error) return open_error;
    *handle=1;
    pending=persisted;
    return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t handle,const char *key,uint8_t *value)
{
    assert(handle==1);
    if(read_error_key && strcmp(key,read_error_key)==0)return ERR_MISSING;
    if(strcmp(key,"brightness")==0 && persisted.has_brightness){*value=persisted.brightness;return ESP_OK;}
    if(strcmp(key,"todo_count")==0 && persisted.has_count){*value=persisted.count;return ESP_OK;}
    if(strcmp(key,"todo_checks")==0 && persisted.has_checks){*value=persisted.checks;return ESP_OK;}
    return ERR_MISSING;
}
esp_err_t nvs_get_blob(nvs_handle_t handle,const char *key,void *value,size_t *size)
{
    assert(handle==1 && strcmp(key,"todo_ids")==0);
    if(!persisted.has_blob || (read_error_key && strcmp(key,read_error_key)==0))return ERR_MISSING;
    size_t capacity=*size;*size=persisted.ids_size;
    if(capacity<persisted.ids_size)return ERR_LENGTH;
    memcpy(value,persisted.ids,persisted.ids_size);
    return ESP_OK;
}
esp_err_t nvs_get_i64(nvs_handle_t handle,const char *key,int64_t *value)
{
    assert(handle==1 && strcmp(key,"epoch")==0);
    if(!persisted.has_epoch || (read_error_key && strcmp(key,read_error_key)==0))return ERR_MISSING;
    *value=persisted.epoch;return ESP_OK;
}
static bool fail_write(const char *key)
{ ++write_calls;return write_error_key && strcmp(key,write_error_key)==0; }
esp_err_t nvs_set_u8(nvs_handle_t handle,const char *key,uint8_t value)
{
    assert(handle==1);
    if(fail_write(key))return ERR_WRITE;
    if(strcmp(key,"brightness")==0){pending.brightness=value;pending.has_brightness=true;}
    else if(strcmp(key,"todo_count")==0){pending.count=value;pending.has_count=true;}
    else if(strcmp(key,"todo_checks")==0){pending.checks=value;pending.has_checks=true;}
    else assert(false);
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle,const char *key,const void *value,size_t size)
{
    assert(handle==1 && strcmp(key,"todo_ids")==0 && size==CD_TODO_CAPACITY);
    if(fail_write(key))return ERR_WRITE;
    memcpy(pending.ids,value,size);pending.ids_size=size;pending.has_blob=true;
    return ESP_OK;
}
esp_err_t nvs_set_i64(nvs_handle_t handle,const char *key,int64_t value)
{
    assert(handle==1 && strcmp(key,"epoch")==0);
    if(fail_write(key))return ERR_WRITE;
    pending.epoch=value;pending.has_epoch=true;return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle==1);++commit_calls;
    if(commit_error)return commit_error;
    persisted=pending;return ESP_OK;
}
static void reboot_runtime(void)
{
    cd_model_init(&s_model,OCT_6_MIDNIGHT);
    s_nvs=0;s_nvs_ready=false;s_settings_dirty=false;s_last_save_us=0;s_last_change_us=0;
}
static void clear_fixture(void)
{
    memset(&persisted,0,sizeof(persisted));memset(&pending,0,sizeof(pending));
    init_error=open_error=commit_error=0;write_error_key=read_error_key=NULL;
    warnings=erase_calls=write_calls=commit_calls=resync_calls=0;
    reboot_runtime();
}
'''


CASES = r'''
static void assert_default_ids(void)
{ for(unsigned i=0;i<CD_TODO_CAPACITY;i++)assert(s_model.todo_ids[i]==i); }

static void test_save_reload(void)
{
    static const uint8_t ids[]={4,2,0,3,1};
    for(unsigned brightness=20;brightness<=100;brightness++) {
        for(unsigned count=3;count<=5;count++) {
            clear_fixture();assert(load_settings()==0 && s_nvs_ready);
            s_model.brightness=(uint8_t)brightness;s_model.todo_count=(uint8_t)count;
            s_model.todo_checked=(uint8_t)(brightness&((1u<<count)-1u));
            memcpy(s_model.todo_ids,ids,sizeof(ids));
            s_model.epoch_utc=OCT_6_MIDNIGHT+86400+brightness;s_model.clock_synced=true;
            s_settings_dirty=true;save_settings(1234567);
            assert(!s_settings_dirty && s_last_save_us==1234567 && commit_calls==1);
            assert(write_calls==5 && warnings==0 && erase_calls==0);
            assert(resync_calls==1);
            const uint8_t checks=s_model.todo_checked;
            const int64_t epoch=s_model.epoch_utc;
            reboot_runtime();assert(load_settings()==epoch);
            assert(s_model.brightness==brightness && s_model.todo_count==count);
            assert(s_model.todo_checked==checks && memcmp(s_model.todo_ids,ids,sizeof(ids))==0);
            /* Last epoch survives; reliable synchronization is not fabricated. */
            assert(!s_model.clock_synced);
        }
    }
    for(unsigned checks=0;checks<32;checks++) {
        clear_fixture();assert(load_settings()==0);s_model.todo_count=5;
        s_model.todo_checked=(uint8_t)checks;s_settings_dirty=true;save_settings(99);
        reboot_runtime();assert(load_settings()==OCT_6_MIDNIGHT);
        assert(s_model.todo_count==5 && s_model.todo_checked==checks);
    }
}

static void test_missing_bad_data(void)
{
    clear_fixture();assert(load_settings()==0);
    assert(s_model.brightness==85 && s_model.todo_count==3 && s_model.todo_checked==0);
    assert_default_ids();
    const unsigned invalid_brightness[]={0,19,101,255};
    for(unsigned i=0;i<sizeof(invalid_brightness)/sizeof(invalid_brightness[0]);i++) {
        clear_fixture();persisted.has_brightness=true;persisted.brightness=(uint8_t)invalid_brightness[i];
        assert(load_settings()==0 && s_model.brightness==85);
    }
    const unsigned invalid_count[]={0,1,2,6,255};
    for(unsigned i=0;i<sizeof(invalid_count)/sizeof(invalid_count[0]);i++) {
        clear_fixture();persisted.has_count=true;persisted.count=(uint8_t)invalid_count[i];
        persisted.has_checks=true;persisted.checks=255;assert(load_settings()==0);
        assert(s_model.todo_count==3 && s_model.todo_checked==7);assert_default_ids();
    }
    for(unsigned count=3;count<=5;count++) {
        clear_fixture();persisted.has_count=true;persisted.count=(uint8_t)count;
        persisted.has_checks=true;persisted.checks=255;assert(load_settings()==0);
        assert(s_model.todo_checked==(1u<<count)-1u);
    }
    const unsigned invalid_sizes[]={0,1,4,6,16};
    for(unsigned i=0;i<sizeof(invalid_sizes)/sizeof(invalid_sizes[0]);i++) {
        clear_fixture();persisted.has_blob=true;persisted.ids_size=invalid_sizes[i];
        memset(persisted.ids,4,sizeof(persisted.ids));assert(load_settings()==0);assert_default_ids();
    }
    for(unsigned bad_slot=0;bad_slot<5;bad_slot++) {
        clear_fixture();persisted.has_blob=true;persisted.ids_size=5;
        for(unsigned i=0;i<5;i++)persisted.ids[i]=(uint8_t)i;
        persisted.ids[bad_slot]=255;assert(load_settings()==0);assert_default_ids();
    }
    clear_fixture();persisted.has_epoch=true;persisted.epoch=OCT_6_MIDNIGHT+86400;
    read_error_key="epoch";assert(load_settings()==0 && !s_model.clock_synced);
    assert(erase_calls==0);
}

static void test_storage_errors(void)
{
    clear_fixture();init_error=ERR_INIT;
    assert(load_settings()==0 && !s_nvs_ready && warnings==1 && erase_calls==0);
    s_settings_dirty=true;save_settings(88);
    assert(write_calls==0 && commit_calls==0 && s_settings_dirty);
    assert(resync_calls==0);
    clear_fixture();open_error=ERR_OPEN;
    assert(load_settings()==0 && !s_nvs_ready && warnings==1 && erase_calls==0);
    assert(s_model.brightness==85 && s_model.todo_count==3);assert_default_ids();

    const char *keys[]={"brightness","todo_count","todo_checks","todo_ids","epoch"};
    for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);i++) {
        clear_fixture();assert(load_settings()==0);write_error_key=keys[i];
        s_model.brightness=72;s_model.todo_count=5;s_model.todo_checked=31;
        s_settings_dirty=true;save_settings(800);
        assert(s_settings_dirty && warnings==1 && commit_calls==0 && erase_calls==0);
        assert(resync_calls==1); /* Even a partial/failed flash write can interrupt RGB. */
        assert(s_last_save_us==800);
        assert(s_last_change_us==800);
        write_error_key=NULL;save_settings(900);
        assert(!s_settings_dirty && commit_calls==1 && erase_calls==0);
        assert(resync_calls==2);
        reboot_runtime();assert(load_settings()==OCT_6_MIDNIGHT);
        assert(s_model.brightness==72 && s_model.todo_count==5 && s_model.todo_checked==31);
    }
    clear_fixture();assert(load_settings()==0);commit_error=ERR_COMMIT;
    s_settings_dirty=true;save_settings(1000);
    assert(s_settings_dirty && commit_calls==1 && warnings==1 && erase_calls==0);
    assert(resync_calls==1);
    assert(s_last_change_us==1000);
    commit_error=0;save_settings(1100);
    assert(!s_settings_dirty && commit_calls==2 && erase_calls==0);
    assert(resync_calls==2);
}

int main(void)
{
    test_save_reload();test_missing_bad_data();test_storage_errors();
    puts("CatDesk persistence: actual load/save functions PASS; 243 brightness/count roundtrips, all32 five-item checkbox sets, epoch metadata, defaults, corrupt blobs, write/commit recovery and no erase.");
    return 0;
}
'''


def main():
    source = UI.read_text()
    functions = "\n\n".join(extract_function(source, name)
                             for name in ("resync_rgb", "load_settings", "save_settings"))
    with tempfile.TemporaryDirectory(prefix="catdesk-persistence-") as directory:
        temporary = Path(directory)
        harness = temporary / "test_persistence.c"
        binary = temporary / "test_persistence"
        harness.write_text(STUBS + "\n" + functions + "\n" + CASES)
        subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
            "-I", str(ROOT / "firmware/components/catdesk_ui/include"),
            str(harness), str(ROOT / "firmware/components/catdesk_ui/catdesk_model.c"),
            "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
