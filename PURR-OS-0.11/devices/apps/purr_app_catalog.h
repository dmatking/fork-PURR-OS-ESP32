#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *name;
    void (*launch)(void);
} purr_catalog_entry_t;

extern const purr_catalog_entry_t purr_catalog[];
extern const int purr_catalog_count;

#ifdef __cplusplus
}
#endif
