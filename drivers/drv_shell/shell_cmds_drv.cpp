// shell_cmds_drv.cpp — 'drvmgr' shell command implementation
// Covers both PDL (.drv scripts) and native sys_drv registered drivers.

#include "purr_drv.h"
#include "purr_sys_drv.h"
#include <stdio.h>
#include <string.h>

#define DRV_DIR "/sdcard/drvdebug/"

extern "C" void cmd_drvmgr(int argc, char **argv) {
    if (argc < 2) {
        printf("drvmgr <load|unload|list|cmd|tick|reload|sys> [args]\n"
               "  load <file>         load /sdcard/drvdebug/<file>.drv\n"
               "  unload <name>       unload PDL driver by name\n"
               "  list                list all drivers (sys + PDL)\n"
               "  cmd <name> [args]   send command to PDL or sys driver\n"
               "  tick                manually trigger tick on all PDL drivers\n"
               "  reload <name>       unload + reload PDL driver from drvdebug\n"
               "  sys                 list sys drivers only (with subsystem info)\n");
        return;
    }

    const char *sub = argv[1];

    if (!strcmp(sub, "list")) {
        // sys drivers first
        sys_drv_t *sdrv[SYS_DRV_MAX];
        int sn = sys_drv_list(sdrv, SYS_DRV_MAX);
        for (int i = 0; i < sn; i++)
            printf("  [sys] %-20s  %-8s  %s\n", sdrv[i]->name, sdrv[i]->subsystem,
                   sdrv[i]->enabled ? "enabled" : "disabled");
        // PDL drivers
        char names[PURR_DRV_MAX][32];
        int pn = purr_drv_list(names, PURR_DRV_MAX);
        for (int i = 0; i < pn; i++) printf("  [pdl] %s\n", names[i]);
        if (!sn && !pn) printf("no drivers registered\n");
        return;
    }

    if (!strcmp(sub, "sys")) {
        sys_drv_t *sdrv[SYS_DRV_MAX];
        int sn = sys_drv_list(sdrv, SYS_DRV_MAX);
        if (!sn) { printf("no sys drivers registered\n"); return; }
        for (int i = 0; i < sn; i++)
            printf("  [%d] %-20s  %-8s  %s  cmd=%s\n",
                   i, sdrv[i]->name, sdrv[i]->subsystem,
                   sdrv[i]->enabled ? "enabled" : "disabled",
                   sdrv[i]->cmd ? "yes" : "no");
        return;
    }

    if (!strcmp(sub, "tick")) {
        purr_drv_tick();
        printf("pdl tick dispatched\n");
        return;
    }

    if (!strcmp(sub, "load")) {
        if (argc < 3) { printf("usage: drvmgr load <filename>\n"); return; }
        char path[256];
        const char *arg = argv[2];
        if (arg[0] == '/') {
            strncpy(path, arg, sizeof(path)-1);
        } else {
            char name[64]; strncpy(name, arg, 63);
            char *dot = strrchr(name, '.'); if (dot) *dot = '\0';
            snprintf(path, sizeof(path), "%s%s.drv", DRV_DIR, name);
        }
        char err[128] = {};
        if (purr_drv_load(path, err, sizeof(err)))
            printf("loaded '%s'\n", path);
        else
            printf("error: %s\n", err);
        return;
    }

    if (!strcmp(sub, "unload")) {
        if (argc < 3) { printf("usage: drvmgr unload <name>\n"); return; }
        printf(purr_drv_unload(argv[2]) ? "unloaded '%s'\n" : "not found: '%s'\n", argv[2]);
        return;
    }

    if (!strcmp(sub, "cmd")) {
        if (argc < 3) { printf("usage: drvmgr cmd <name> [args]\n"); return; }
        char args[256] = {};
        for (int i = 3; i < argc; i++) {
            if (i > 3) strncat(args, " ", sizeof(args)-strlen(args)-1);
            strncat(args, argv[i], sizeof(args)-strlen(args)-1);
        }
        char out[256] = {};
        // try PDL first, then sys
        if (purr_drv_cmd(argv[2], args, out, sizeof(out))) {
            printf("%s\n", out); return;
        }
        int r = sys_drv_cmd(argv[2], args, out, sizeof(out));
        printf("%s\n", out);
        (void)r;
        return;
    }

    if (!strcmp(sub, "reload")) {
        if (argc < 3) { printf("usage: drvmgr reload <name>\n"); return; }
        char path[256];
        snprintf(path, sizeof(path), "%s%s.drv", DRV_DIR, argv[2]);
        purr_drv_unload(argv[2]);
        char err[128] = {};
        if (purr_drv_load(path, err, sizeof(err)))
            printf("reloaded '%s'\n", argv[2]);
        else
            printf("error: %s\n", err);
        return;
    }

    printf("unknown subcommand '%s'\n", sub);
}
