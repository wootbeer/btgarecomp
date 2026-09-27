#ifndef BTGA_PATCHES_UI_FUNCS_H
#define BTGA_PATCHES_UI_FUNCS_H

// Fills recompui's "// TODO: Forced game includes" hook
// (lib/RecompFrontend/recompui/src/api/ui_api_events.cpp), which needs
// RecompuiEventData (and the enums it's built from) to compile regardless
// of whether this game has any real UI-driving patches yet -- see
// recompui_event_structs.h for the actual definitions and why they live in
// a separate header (shared with the MIPS-side patches/*.c build).
//
// No patches.toml or patches/*.c source exists yet (see PROGRESS.md item
// 8), so there are no UI callback function declarations to add here beyond
// the event data shape itself.
#include "recompui_event_structs.h"

#endif // BTGA_PATCHES_UI_FUNCS_H
