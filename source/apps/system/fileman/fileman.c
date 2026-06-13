// fileman.c — PURR OS file manager (.claw)
// Uses purr_win.h only — works on KittenUI (LVGL) and MiniWin identically.
//
// Two panels: left = directory listing, right = text file preview.
// Roots:  /spiffs  (PURR internal flash)
//         /sd      (SD card, if mounted)

#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "purr_win.h"
#include "purr_kernel.h"
#include "purr_module.h"

#define MAX_ENTRIES  64
#define PATH_MAX_LEN 256
#define PREVIEW_MAX  2048

// ── State ─────────────────────────────────────────────────────────────────────

static purr_win_t s_win        = 0;
static purr_wid_t s_path_lbl   = 0;   // current path header
static purr_wid_t s_list_area  = 0;   // left: scrollable entry list (textarea)
static purr_wid_t s_prev_area  = 0;   // right: file preview (textarea)
static purr_wid_t s_status_lbl = 0;

static char s_cwd[PATH_MAX_LEN] = "/spiffs";
static char s_entries[MAX_ENTRIES][64];
static int  s_entry_types[MAX_ENTRIES]; // 0=file, 1=dir
static int  s_entry_count = 0;
static int  s_selected    = -1;

// ── Helpers ───────────────────────────────────────────────────────────────────

static void set_status(const char *msg) {
    purr_win_label_set(s_status_lbl, msg);
}

static void refresh_list(void) {
    s_entry_count = 0;
    s_selected    = -1;

    DIR *d = opendir(s_cwd);
    if (!d) {
        set_status("Cannot open directory.");
        purr_win_textarea_set(s_list_area, "(empty)");
        return;
    }

    struct dirent *ent;
    char buf[PREVIEW_MAX];
    size_t pos = 0;

    // Parent entry if not at a root
    bool at_root = (strcmp(s_cwd, "/spiffs") == 0 ||
                    strcmp(s_cwd, "/sd")     == 0);
    if (!at_root && pos < PREVIEW_MAX - 4) {
        pos += snprintf(buf + pos, PREVIEW_MAX - pos, "[..]\n");
        strncpy(s_entries[s_entry_count], "..", sizeof(s_entries[0]) - 1);
        s_entry_types[s_entry_count] = 1;
        s_entry_count++;
    }

    while ((ent = readdir(d)) != NULL && s_entry_count < MAX_ENTRIES) {
        if (ent->d_name[0] == '.') continue;

        bool is_dir = (ent->d_type == DT_DIR);
        if (ent->d_type == DT_UNKNOWN) {
            // Determine via stat for filesystems that don't set d_type
            char full[PATH_MAX_LEN];
            snprintf(full, sizeof(full), "%s/%s", s_cwd, ent->d_name);
            struct stat st;
            if (stat(full, &st) == 0) is_dir = S_ISDIR(st.st_mode);
        }

        if (pos < PREVIEW_MAX - 80) {
            pos += snprintf(buf + pos, PREVIEW_MAX - pos,
                            is_dir ? "[%s]\n" : " %s\n", ent->d_name);
        }

        strncpy(s_entries[s_entry_count], ent->d_name, sizeof(s_entries[0]) - 1);
        s_entry_types[s_entry_count] = is_dir ? 1 : 0;
        s_entry_count++;
    }
    closedir(d);

    buf[pos] = '\0';
    purr_win_textarea_set(s_list_area, buf);
    purr_win_label_set(s_path_lbl, s_cwd);
    purr_win_textarea_set(s_prev_area, "");
    set_status("Select a file to preview.");
}

static void preview_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        purr_win_textarea_set(s_prev_area, "(cannot open file)");
        return;
    }

    char buf[PREVIEW_MAX];
    size_t n = fread(buf, 1, PREVIEW_MAX - 1, f);
    fclose(f);
    buf[n] = '\0';

    // Replace non-printable bytes with '.' to stay UI-safe
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)buf[i];
        if (c != '\n' && c != '\r' && c != '\t' && (c < 0x20 || c > 0x7E))
            buf[i] = '.';
    }

    purr_win_textarea_set(s_prev_area, buf);

    char status[PATH_MAX_LEN + 24];
    snprintf(status, sizeof(status), "%lu bytes", (unsigned long)n);
    set_status(status);
}

// ── Navigation buttons ────────────────────────────────────────────────────────

static void navigate_to(const char *path) {
    strncpy(s_cwd, path, PATH_MAX_LEN - 1);
    refresh_list();
}

// "Up" — go to parent
static void on_up(purr_wid_t w, purr_event_t e, void *u) {
    (void)w;(void)e;(void)u;
    if (strcmp(s_cwd, "/spiffs") == 0 || strcmp(s_cwd, "/sd") == 0) {
        // At a root — show root chooser
        navigate_to("/spiffs");
        return;
    }
    char parent[PATH_MAX_LEN];
    strncpy(parent, s_cwd, sizeof(parent) - 1);
    char *slash = strrchr(parent, '/');
    if (slash && slash != parent) {
        *slash = '\0';
    } else {
        strcpy(parent, "/spiffs");
    }
    navigate_to(parent);
}

// Navigate to SPIFFS root
static void on_goto_spiffs(purr_wid_t w, purr_event_t e, void *u) {
    (void)w;(void)e;(void)u;
    navigate_to("/spiffs");
}

// Navigate to SD root
static void on_goto_sd(purr_wid_t w, purr_event_t e, void *u) {
    (void)w;(void)e;(void)u;
    if (!purr_kernel_sd_available()) {
        set_status("SD card not mounted.");
        return;
    }
    navigate_to("/sd");
}

// "Open" — open selected entry (enter dir or preview file)
// Because we can't bind per-entry buttons without a list widget, we expose
// Prev/Next buttons to select entries and Open to act on them.

static void update_selection_label(void) {
    if (s_selected < 0 || s_selected >= s_entry_count) {
        set_status("No selection.");
        return;
    }
    char msg[80];
    snprintf(msg, sizeof(msg), "[%d/%d] %s%s",
             s_selected + 1, s_entry_count,
             s_entries[s_selected],
             s_entry_types[s_selected] ? "/" : "");
    set_status(msg);
}

static void on_prev(purr_wid_t w, purr_event_t e, void *u) {
    (void)w;(void)e;(void)u;
    if (s_entry_count == 0) return;
    s_selected = (s_selected <= 0) ? s_entry_count - 1 : s_selected - 1;
    update_selection_label();
}

static void on_next(purr_wid_t w, purr_event_t e, void *u) {
    (void)w;(void)e;(void)u;
    if (s_entry_count == 0) return;
    s_selected = (s_selected + 1) % s_entry_count;
    update_selection_label();
}

static void on_open(purr_wid_t w, purr_event_t e, void *u) {
    (void)w;(void)e;(void)u;
    if (s_selected < 0 || s_selected >= s_entry_count) {
        set_status("Nothing selected.");
        return;
    }

    const char *name = s_entries[s_selected];

    if (strcmp(name, "..") == 0) {
        on_up(w, e, u);
        return;
    }

    char full[PATH_MAX_LEN];
    snprintf(full, sizeof(full), "%s/%s", s_cwd, name);

    if (s_entry_types[s_selected] == 1) {
        navigate_to(full);
    } else {
        preview_file(full);
        purr_win_label_set(s_path_lbl, full);
    }
}

// ── App lifecycle ─────────────────────────────────────────────────────────────

static int fileman_init(void) {
    s_win = purr_win_create("File Manager");

    // Toolbar row: SPIFFS | SD | Up | [prev] [next] [open]
    purr_wid_t toolbar = purr_win_row(s_win, 4);
    purr_win_button(s_win, "SPIFFS", on_goto_spiffs, NULL);
    purr_win_button(s_win, "SD",     on_goto_sd,     NULL);
    purr_win_button(s_win, "Up",     on_up,          NULL);
    purr_win_layout_end(toolbar);

    // Path header
    s_path_lbl = purr_win_label(s_win, s_cwd);

    // Content row: file list (60%) | preview (40%)
    purr_wid_t content = purr_win_row(s_win, 0);
    s_list_area = purr_win_textarea(s_win, 55, 70);
    s_prev_area = purr_win_textarea(s_win, 40, 70);
    purr_win_layout_end(content);

    // Selection row
    purr_wid_t sel_row = purr_win_row(s_win, 4);
    purr_win_button(s_win, "< Prev", on_prev, NULL);
    purr_win_button(s_win, "Open",   on_open, NULL);
    purr_win_button(s_win, "Next >", on_next, NULL);
    purr_win_layout_end(sel_row);

    // Status bar
    s_status_lbl = purr_win_label(s_win, "Ready.");

    purr_win_show(s_win);
    refresh_list();
    return 0;
}

static void fileman_deinit(void) {
    purr_win_destroy(s_win);
    s_win = 0;
    s_entry_count = 0;
    s_selected = -1;
    strncpy(s_cwd, "/spiffs", PATH_MAX_LEN - 1);
}

// ── Module header ─────────────────────────────────────────────────────────────

PURR_MODULE_REGISTER(fileman) = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_APP,
    .load_priority     = PURR_PRIORITY_OPTIONAL,
    .name              = "fileman",
    .version           = "1.0.0",
    .kernel_min        = "0.9.0",
    .provided_catcalls = 0,
    .required_catcalls = 0,
    .init              = fileman_init,
    .deinit            = fileman_deinit,
};
