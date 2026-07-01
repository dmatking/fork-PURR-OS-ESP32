// purr_wm.cpp — PURR OS Window Manager
// MiniWin build: mw_init() is called from main.cpp; purr_wm_start() is a no-op.
// mw_user_* callbacks live in lib_miniwin/MiniWin/hal/PURR_CYD/purr_app.cpp.

#ifdef PURR_CYD
extern "C" void purr_wm_start() {}
void purr_wm_init() {}
#endif  // PURR_CYD
