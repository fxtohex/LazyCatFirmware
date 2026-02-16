#include "ble_scanner_app.h"
#include "scenes/ble_scanner_scene.h"

#define TAG "BleScanner"

static void ble_scanner_tick_callback(void* context) {
    BleScannerApp* app = context;
    if(app->needs_redraw) {
        app->needs_redraw = false;
        if(app->scan_view) {
            with_view_model(
                app->scan_view, BleScannerApp** model, { UNUSED(model); }, true);
        }
    }
}

static bool ble_scanner_custom_event_callback(void* context, uint32_t event) {
    BleScannerApp* app = context;
    return scene_manager_handle_custom_event(app->scene_manager, event);
}

static bool ble_scanner_back_event_callback(void* context) {
    BleScannerApp* app = context;
    return scene_manager_handle_back_event(app->scene_manager);
}

static BleScannerApp* ble_scanner_app_alloc(void) {
    BleScannerApp* app = malloc(sizeof(BleScannerApp));
    memset(app, 0, sizeof(BleScannerApp));

    // Open system records
    app->gui = furi_record_open(RECORD_GUI);
    app->bt = furi_record_open(RECORD_BT);

    // Allocate view dispatcher (queue support is always enabled)
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, ble_scanner_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, ble_scanner_back_event_callback);
    view_dispatcher_set_tick_event_callback(
        app->view_dispatcher, ble_scanner_tick_callback, 250);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    // Allocate scene manager
    app->scene_manager = scene_manager_alloc(&ble_scanner_scene_handlers, app);

    // Allocate data mutex for thread-safe device list access
    app->data_mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    app->device_count = 0;
    app->selected_index = 0;
    app->scanning = false;
    app->scan_view = NULL;

    return app;
}

static void ble_scanner_app_free(BleScannerApp* app) {
    furi_assert(app);

    // Free scene manager and view dispatcher
    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);

    // Free mutex
    furi_mutex_free(app->data_mutex);

    // Close system records
    furi_record_close(RECORD_BT);
    furi_record_close(RECORD_GUI);

    free(app);
}

int32_t ble_scanner_app(void* args) {
    UNUSED(args);

    BleScannerApp* app = ble_scanner_app_alloc();

    FURI_LOG_I(TAG, "BLE Scanner started");

    scene_manager_next_scene(app->scene_manager, BleScannerSceneScan);
    view_dispatcher_run(app->view_dispatcher);

    FURI_LOG_I(TAG, "BLE Scanner exiting");

    ble_scanner_app_free(app);
    return 0;
}
