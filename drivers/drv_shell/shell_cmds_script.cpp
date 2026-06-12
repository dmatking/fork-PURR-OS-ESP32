// shell_cmds_script.cpp — 'run' shell command for purr_script_run dispatch

#include "modules/purr_script.h"
#include <stdio.h>

extern "C" {

void cmd_script_run(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: run <path>   (e.g. run /sdcard/hello.lua)\n");
        return;
    }
    const char *path = argv[1];
    bool restricted = (argc >= 3 && argv[2][0] == 'r');
    printf("running: %s%s\n", path, restricted ? " (restricted)" : "");
    purr_script_result_t r = purr_script_run(path, restricted);
    if (r != PURR_SCRIPT_OK)
        printf("error: %s\n", purr_script_result_str(r));
}

} // extern "C"
