#ifndef BTGA_PATCHES_RECOMPUI_EVENT_STRUCTS_H
#define BTGA_PATCHES_RECOMPUI_EVENT_STRUCTS_H

#include <stdint.h>

// Shared ABI between the host-side UI code (lib/RecompFrontend/recompui) and
// guest-side (MIPS) patch code for UI callback events. recomp_run_ui_callbacks
// (lib/RecompFrontend/recompui/src/api/ui_api_events.cpp) builds one of these
// on the recompiled game's stack and calls into a game-registered callback
// with a pointer to it in $a1.
//
// RecompuiEventType/RecompuiDragPhase/RecompuiMenuAction must be kept in sync
// with recompui::EventType/DragPhase/MenuAction
// (lib/RecompFrontend/recompui/src/elements/ui_types.h) -- same values, same
// order. Plain C (not enum class / C++ structs) so this header works
// unchanged from both the x86 host build and the MIPS-targeted patches/*.c
// build (see patches/Makefile).

typedef enum {
    RECOMPUI_EVENT_NONE,
    RECOMPUI_EVENT_CLICK,
    RECOMPUI_EVENT_FOCUS,
    RECOMPUI_EVENT_HOVER,
    RECOMPUI_EVENT_ENABLE,
    RECOMPUI_EVENT_DRAG,
    RECOMPUI_EVENT_TEXT,
    RECOMPUI_EVENT_UPDATE,
    RECOMPUI_EVENT_NAVIGATE,
    RECOMPUI_EVENT_MOUSEBUTTON,
    RECOMPUI_EVENT_MENUACTION,
    RECOMPUI_EVENT_COUNT
} RecompuiEventType;

typedef enum {
    RECOMPUI_DRAG_NONE,
    RECOMPUI_DRAG_START,
    RECOMPUI_DRAG_MOVE,
    RECOMPUI_DRAG_END
} RecompuiDragPhase;

typedef enum {
    RECOMPUI_MENUACTION_NONE,
    RECOMPUI_MENUACTION_ACCEPT,
    RECOMPUI_MENUACTION_APPLY,
    RECOMPUI_MENUACTION_BACK,
    RECOMPUI_MENUACTION_TOGGLE,
    RECOMPUI_MENUACTION_TABLEFT,
    RECOMPUI_MENUACTION_TABRIGHT
} RecompuiMenuAction;

typedef struct {
    float x;
    float y;
} RecompuiEventClickData;

typedef struct {
    int32_t active;
} RecompuiEventFocusData;

typedef struct {
    int32_t active;
} RecompuiEventHoverData;

typedef struct {
    int32_t active;
} RecompuiEventEnableData;

typedef struct {
    float x;
    float y;
    RecompuiDragPhase phase;
} RecompuiEventDragData;

typedef struct {
    RecompuiMenuAction action;
} RecompuiEventMenuActionData;

typedef union {
    RecompuiEventClickData click;
    RecompuiEventFocusData focus;
    RecompuiEventHoverData hover;
    RecompuiEventEnableData enable;
    RecompuiEventDragData drag;
    RecompuiEventMenuActionData menu_action;
} RecompuiEventDataUnion;

typedef struct {
    RecompuiEventType type;
    RecompuiEventDataUnion data;
} RecompuiEventData;

#endif // BTGA_PATCHES_RECOMPUI_EVENT_STRUCTS_H
