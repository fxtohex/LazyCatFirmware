#include "../ble_scanner_app.h"
#include "ble_scanner_scene.h"

#include <string.h>

#define TAG "BleScanner"

/**
 * Parse BLE advertising data for Complete or Shortened Local Name.
 * AD structure: [length][type][data...][length][type][data...]...
 * Type 0x09 = Complete Local Name, Type 0x08 = Shortened Local Name.
 */
static bool ble_scanner_parse_name(
    const uint8_t* data,
    uint8_t data_len,
    char* out_name,
    size_t out_size) {
    uint8_t pos = 0;
    while(pos < data_len) {
        uint8_t field_len = data[pos];
        if(field_len == 0 || (pos + field_len) >= data_len) break;
        uint8_t field_type = data[pos + 1];
        if(field_type == 0x09 || field_type == 0x08) {
            uint8_t name_len = field_len - 1;
            if(name_len >= out_size) name_len = out_size - 1;
            memcpy(out_name, &data[pos + 2], name_len);
            out_name[name_len] = '\0';
            return true;
        }
        pos += field_len + 1;
    }
    return false;
}

/**
 * BLE scan result callback. Invoked from the BT service GAP event context.
 * We must not touch View model or GUI objects from here.
 * Instead, we update our device list under mutex and signal the main thread.
 */
static void ble_scanner_scan_callback(GapScanResult* result, void* context) {
    BleScannerApp* app = context;

    if(furi_mutex_acquire(app->data_mutex, 50) != FuriStatusOk) {
        return; // Skip this result rather than block the BLE stack
    }

    // Search for existing device by MAC address
    int found = -1;
    for(uint8_t i = 0; i < app->device_count; i++) {
        if(memcmp(app->devices[i].address, result->address, 6) == 0) {
            found = i;
            break;
        }
    }

    if(found >= 0) {
        // Update existing device RSSI and timestamp
        app->devices[found].rssi = result->rssi;
        app->devices[found].last_seen = furi_get_tick();
        // Try to fill in name if we did not have one before
        if(!app->devices[found].has_name && result->data_len > 0) {
            char name[BLE_SCANNER_NAME_MAX];
            if(ble_scanner_parse_name(result->data, result->data_len, name, sizeof(name))) {
                strncpy(app->devices[found].name, name, BLE_SCANNER_NAME_MAX - 1);
                app->devices[found].name[BLE_SCANNER_NAME_MAX - 1] = '\0';
                app->devices[found].has_name = true;
            }
        }
    } else if(app->device_count < BLE_SCANNER_MAX_DEVICES) {
        // Add new device
        BleScannerDevice* dev = &app->devices[app->device_count];
        memcpy(dev->address, result->address, 6);
        dev->address_type = result->address_type;
        dev->rssi = result->rssi;
        dev->last_seen = furi_get_tick();
        dev->has_name = false;

        if(result->data_len > 0) {
            char name[BLE_SCANNER_NAME_MAX];
            if(ble_scanner_parse_name(result->data, result->data_len, name, sizeof(name))) {
                strncpy(dev->name, name, BLE_SCANNER_NAME_MAX - 1);
                dev->name[BLE_SCANNER_NAME_MAX - 1] = '\0';
                dev->has_name = true;
            }
        }

        if(!dev->has_name) {
            snprintf(
                dev->name,
                BLE_SCANNER_NAME_MAX,
                "%02X:%02X:%02X:%02X:%02X:%02X",
                dev->address[0],
                dev->address[1],
                dev->address[2],
                dev->address[3],
                dev->address[4],
                dev->address[5]);
        }

        app->device_count++;
    }

    furi_mutex_release(app->data_mutex);

    // Signal tick callback to trigger redraw (safe from any thread context)
    app->needs_redraw = true;
}

/**
 * Draw callback for the scan view. Runs on the GUI thread.
 * Model is BleScannerApp** (pointer to app pointer stored in view model).
 */
static void ble_scanner_scan_draw_callback(Canvas* canvas, void* model) {
    BleScannerApp* app = *(BleScannerApp**)model;

    if(furi_mutex_acquire(app->data_mutex, 10) != FuriStatusOk) {
        // If we cannot acquire the lock quickly, draw a minimal frame
        canvas_clear(canvas);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, 0, 11, "BLE Scanner");
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, "Updating...");
        return;
    }

    canvas_clear(canvas);

    // Header
    canvas_set_font(canvas, FontPrimary);
    if(app->scanning) {
        canvas_draw_str(canvas, 0, 11, "BLE Scan [ON]");
    } else {
        canvas_draw_str(canvas, 0, 11, "BLE Scan [OFF]");
    }

    // Device count in top-right corner
    char count_str[16];
    snprintf(count_str, sizeof(count_str), "%d found", app->device_count);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 124, 2, AlignRight, AlignTop, count_str);

    // Separator line
    canvas_draw_line(canvas, 0, 13, 127, 13);

    if(app->device_count == 0) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(
            canvas,
            64,
            38,
            AlignCenter,
            AlignCenter,
            app->scanning ? "Scanning..." : "Press OK to scan");
    } else {
        // Compute scroll window
        int16_t start_idx = 0;
        if(app->selected_index >= BLE_SCANNER_VISIBLE) {
            start_idx = app->selected_index - BLE_SCANNER_VISIBLE + 1;
        }

        canvas_set_font(canvas, FontSecondary);

        for(int i = 0; i < BLE_SCANNER_VISIBLE && (start_idx + i) < app->device_count; i++) {
            int idx = start_idx + i;
            BleScannerDevice* dev = &app->devices[idx];
            int y = 15 + i * 12;

            // Highlight selected item with inverted box
            if(idx == app->selected_index) {
                canvas_draw_box(canvas, 0, y, 125, 12);
                canvas_set_color(canvas, ColorWhite);
            }

            char line[48];
            snprintf(line, sizeof(line), "%.18s %ddBm", dev->name, dev->rssi);
            canvas_draw_str(canvas, 2, y + 9, line);

            if(idx == app->selected_index) {
                canvas_set_color(canvas, ColorBlack);
            }
        }

        // Scrollbar
        if(app->device_count > BLE_SCANNER_VISIBLE) {
            elements_scrollbar_pos(canvas, 126, 15, 48, app->selected_index, app->device_count);
        }
    }

    // Bottom bar hint
    canvas_set_font(canvas, FontSecondary);
    if(app->device_count > 0) {
        canvas_draw_str_aligned(
            canvas, 0, 63, AlignLeft, AlignBottom, app->scanning ? "OK:Stop" : "OK:Scan");
        canvas_draw_str_aligned(canvas, 127, 63, AlignRight, AlignBottom, ">:Connect");
    } else {
        canvas_draw_str_aligned(
            canvas, 64, 63, AlignCenter, AlignBottom, app->scanning ? "OK: Stop" : "OK: Scan");
    }

    furi_mutex_release(app->data_mutex);
}

/**
 * Input callback for the scan view. Runs on the GUI thread.
 * Context is the View* itself (following the ble_spam pattern).
 * We extract state under the model lock, release it, THEN dispatch events.
 */
static bool ble_scanner_scan_input_callback(InputEvent* event, void* context) {
    View* view = context;
    BleScannerApp* app = *(BleScannerApp**)view_get_model(view);

    // Extract what we need under the lock
    int16_t sel = app->selected_index;
    uint8_t count = app->device_count;
    ViewDispatcher* vd = app->view_dispatcher;
    SceneManager* sm = app->scene_manager;

    bool consumed = false;
    bool need_redraw = false;

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        switch(event->key) {
        case InputKeyUp:
            if(sel > 0) {
                app->selected_index--;
                consumed = true;
                need_redraw = true;
            }
            break;
        case InputKeyDown:
            if(sel < (int16_t)count - 1) {
                app->selected_index++;
                consumed = true;
                need_redraw = true;
            }
            break;
        case InputKeyOk:
            consumed = true;
            break;
        case InputKeyRight:
            if(count > 0 && sel < count) {
                consumed = true;
            }
            break;
        default:
            break;
        }
    }

    // Release the model lock before dispatching events
    view_commit_model(view, need_redraw);

    // Now dispatch events without holding the model lock
    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        if(event->key == InputKeyOk) {
            view_dispatcher_send_custom_event(vd, BleScannerCustomEventToggleScan);
        } else if(event->key == InputKeyRight && count > 0 && sel < count) {
            scene_manager_set_scene_state(sm, BleScannerSceneScan, sel);
            view_dispatcher_send_custom_event(vd, BleScannerCustomEventConnected);
        }
    }

    return consumed;
}

static void ble_scanner_start_scan(BleScannerApp* app) {
    FuriHalBtScanParams params = {
        .interval_ms = 100,
        .window_ms = 50,
        .active = true,
        .filter_duplicates = false,
        .timeout_ms = 30000,
    };
    app->scanning = bt_start_scan(app->bt, &params, ble_scanner_scan_callback, app);
    if(app->scanning) {
        FURI_LOG_I(TAG, "BLE scan started");
    } else {
        FURI_LOG_E(TAG, "Failed to start BLE scan");
    }
}

static void ble_scanner_stop_scan(BleScannerApp* app) {
    if(app->scanning) {
        bt_stop_scan(app->bt);
        app->scanning = false;
        FURI_LOG_I(TAG, "BLE scan stopped");
    }
}

void ble_scanner_scene_scan_on_enter(void* context) {
    BleScannerApp* app = context;

    // Reset device list
    furi_check(furi_mutex_acquire(app->data_mutex, FuriWaitForever) == FuriStatusOk);
    app->device_count = 0;
    app->selected_index = 0;
    app->scanning = false;
    furi_mutex_release(app->data_mutex);

    // Create and configure the scan view
    app->scan_view = view_alloc();
    view_allocate_model(app->scan_view, ViewModelTypeLocking, sizeof(BleScannerApp*));
    with_view_model(
        app->scan_view, BleScannerApp** model, { *model = app; }, false);
    view_set_context(app->scan_view, app->scan_view);
    view_set_draw_callback(app->scan_view, ble_scanner_scan_draw_callback);
    view_set_input_callback(app->scan_view, ble_scanner_scan_input_callback);

    view_dispatcher_add_view(app->view_dispatcher, BleScannerViewScan, app->scan_view);
    view_dispatcher_switch_to_view(app->view_dispatcher, BleScannerViewScan);
}

bool ble_scanner_scene_scan_on_event(void* context, SceneManagerEvent event) {
    BleScannerApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case BleScannerCustomEventToggleScan:
            if(app->scanning) {
                ble_scanner_stop_scan(app);
            } else {
                ble_scanner_start_scan(app);
            }
            // Trigger redraw to update status
            with_view_model(
                app->scan_view, BleScannerApp** model, { UNUSED(model); }, true);
            consumed = true;
            break;
        case BleScannerCustomEventConnected:
            // Stop scan and navigate to device detail
            ble_scanner_stop_scan(app);
            scene_manager_next_scene(app->scene_manager, BleScannerSceneDevice);
            consumed = true;
            break;
        default:
            break;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        ble_scanner_stop_scan(app);
        // Let the scene manager handle the back event (exit)
        consumed = false;
    }

    return consumed;
}

void ble_scanner_scene_scan_on_exit(void* context) {
    BleScannerApp* app = context;

    ble_scanner_stop_scan(app);

    view_dispatcher_remove_view(app->view_dispatcher, BleScannerViewScan);
    view_free(app->scan_view);
    app->scan_view = NULL;
}
