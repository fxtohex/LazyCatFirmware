#include "ble_scanner_scene.h"

// Generate scene on_enter handlers array
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const ble_scanner_scene_on_enter_handlers[])(void*) = {
#include "ble_scanner_scene_config.h"
};
#undef ADD_SCENE

// Generate scene on_event handlers array
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const ble_scanner_scene_on_event_handlers[])(void*, SceneManagerEvent) = {
#include "ble_scanner_scene_config.h"
};
#undef ADD_SCENE

// Generate scene on_exit handlers array
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const ble_scanner_scene_on_exit_handlers[])(void*) = {
#include "ble_scanner_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers ble_scanner_scene_handlers = {
    .on_enter_handlers = ble_scanner_scene_on_enter_handlers,
    .on_event_handlers = ble_scanner_scene_on_event_handlers,
    .on_exit_handlers = ble_scanner_scene_on_exit_handlers,
    .scene_num = BleScannerSceneNum,
};
