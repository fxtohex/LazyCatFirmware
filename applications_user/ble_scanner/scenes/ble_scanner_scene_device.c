#include "../ble_scanner_app.h"
#include "ble_scanner_scene.h"

#include <string.h>

#define TAG "BleScannerDevice"

static void ble_scanner_device_services_cb(
    BleGattClientStatus status,
    BleGattClientService* services,
    uint8_t count,
    void* context) {
    BleScannerApp* app = context;

    if(status == BleGattClientStatusSuccess) {
        uint8_t copy_count = count;
        if(copy_count > BLE_SCANNER_MAX_SERVICES) copy_count = BLE_SCANNER_MAX_SERVICES;
        memcpy(app->gatt_services, services, copy_count * sizeof(BleGattClientService));
        app->gatt_service_count = copy_count;
        FURI_LOG_I(TAG, "Discovered %d services", copy_count);
    } else {
        FURI_LOG_E(TAG, "Service discovery failed: %d", status);
        app->gatt_service_count = 0;
    }
    app->discovering = false;
    view_dispatcher_send_custom_event(
        app->view_dispatcher, BleScannerCustomEventServicesDiscovered);
}

static void ble_scanner_device_draw_callback(Canvas* canvas, void* model) {
    BleScannerApp* app = *(BleScannerApp**)model;

    canvas_clear(canvas);

    // Get selected device
    uint16_t idx = scene_manager_get_scene_state(app->scene_manager, BleScannerSceneScan);
    BleScannerDevice* dev = &app->devices[idx];

    // Header: device name
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 11, dev->name);

    // MAC address
    canvas_set_font(canvas, FontSecondary);
    char mac[20];
    snprintf(
        mac,
        sizeof(mac),
        "%02X:%02X:%02X:%02X:%02X:%02X",
        dev->address[0],
        dev->address[1],
        dev->address[2],
        dev->address[3],
        dev->address[4],
        dev->address[5]);
    canvas_draw_str(canvas, 0, 22, mac);

    // Connection / discovery status
    canvas_draw_line(canvas, 0, 24, 127, 24);

    if(!app->connected) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, "Connecting...");
    } else if(app->discovering) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, "Discovering...");
    } else if(app->gatt_service_count == 0) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, "No services found");
    } else {
        // Show discovered services in a scrollable list
        canvas_set_font(canvas, FontSecondary);
        int16_t y_offset = 26;
        int16_t start = app->device_scroll;
        int max_visible = 4;

        for(int i = start; i < app->gatt_service_count && (i - start) < max_visible; i++) {
            BleGattClientService* svc = &app->gatt_services[i];
            char svc_str[48];

            if(svc->uuid_type == 1) {
                uint16_t uuid16 = (uint16_t)(svc->uuid[0]) | ((uint16_t)(svc->uuid[1]) << 8);
                snprintf(
                    svc_str,
                    sizeof(svc_str),
                    "Svc 0x%04X [%04X-%04X]",
                    uuid16,
                    svc->start_handle,
                    svc->end_handle);
            } else {
                snprintf(
                    svc_str,
                    sizeof(svc_str),
                    "Svc %02X%02X.. [%04X-%04X]",
                    svc->uuid[15],
                    svc->uuid[14],
                    svc->start_handle,
                    svc->end_handle);
            }

            int y = y_offset + (i - start) * 10;

            if(i == app->device_scroll) {
                canvas_draw_box(canvas, 0, y - 1, 127, 10);
                canvas_set_color(canvas, ColorWhite);
            }
            canvas_draw_str(canvas, 2, y + 7, svc_str);
            if(i == app->device_scroll) {
                canvas_set_color(canvas, ColorBlack);
            }
        }

        // Scrollbar
        if(app->gatt_service_count > max_visible) {
            elements_scrollbar_pos(
                canvas, 126, 26, 38, app->device_scroll, app->gatt_service_count);
        }
    }

    // Bottom hint
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, "Back: Disconnect");
}

static bool ble_scanner_device_input_callback(InputEvent* event, void* context) {
    View* view = context;
    BleScannerApp* app = *(BleScannerApp**)view_get_model(view);
    bool consumed = false;

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        switch(event->key) {
        case InputKeyUp:
            if(app->device_scroll > 0) {
                app->device_scroll--;
                consumed = true;
            }
            break;
        case InputKeyDown:
            if(app->device_scroll < (int16_t)app->gatt_service_count - 1) {
                app->device_scroll++;
                consumed = true;
            }
            break;
        default:
            break;
        }
    }

    if(consumed) {
        view_commit_model(view, true);
    } else {
        view_commit_model(view, false);
    }

    return consumed;
}

void ble_scanner_scene_device_on_enter(void* context) {
    BleScannerApp* app = context;

    // Get selected device index from scan scene state
    uint16_t idx = scene_manager_get_scene_state(app->scene_manager, BleScannerSceneScan);

    app->connected = false;
    app->discovering = false;
    app->gatt_service_count = 0;
    app->gatt_char_count = 0;
    app->device_scroll = 0;

    // Create device view
    app->device_view = view_alloc();
    view_allocate_model(app->device_view, ViewModelTypeLocking, sizeof(BleScannerApp*));
    with_view_model(
        app->device_view, BleScannerApp** model, { *model = app; }, false);
    view_set_context(app->device_view, app->device_view);
    view_set_draw_callback(app->device_view, ble_scanner_device_draw_callback);
    view_set_input_callback(app->device_view, ble_scanner_device_input_callback);

    view_dispatcher_add_view(app->view_dispatcher, BleScannerViewDevice, app->device_view);
    view_dispatcher_switch_to_view(app->view_dispatcher, BleScannerViewDevice);

    // Allocate GATT client
    app->gatt_client = ble_gatt_client_alloc();

    // Initiate connection
    BleScannerDevice* dev = &app->devices[idx];
    FURI_LOG_I(
        TAG,
        "Connecting to %02X:%02X:%02X:%02X:%02X:%02X",
        dev->address[0],
        dev->address[1],
        dev->address[2],
        dev->address[3],
        dev->address[4],
        dev->address[5]);

    bool result = bt_connect(app->bt, dev->address, dev->address_type);
    if(result) {
        // Wait for connection to be established
        // Poll for connection handle with timeout
        app->connected = false;
        for(int retries = 0; retries < 50; retries++) {
            furi_delay_ms(100);
            uint16_t handle = bt_get_central_conn_handle(app->bt);
            if(handle != 0xFFFF) {
                app->connected = true;
                ble_gatt_client_set_connection(app->gatt_client, handle);
                FURI_LOG_I(TAG, "Connected, handle: %04X", handle);
                // Start service discovery
                app->discovering = true;
                ble_gatt_client_discover_services(
                    app->gatt_client, ble_scanner_device_services_cb, app);
                break;
            }
        }
        if(!app->connected) {
            FURI_LOG_E(TAG, "Connection timed out");
        }
    } else {
        FURI_LOG_E(TAG, "Connection initiation failed");
    }

    with_view_model(
        app->device_view, BleScannerApp** model, { UNUSED(model); }, true);
}

bool ble_scanner_scene_device_on_event(void* context, SceneManagerEvent event) {
    BleScannerApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case BleScannerCustomEventServicesDiscovered:
            with_view_model(
                app->device_view, BleScannerApp** model, { UNUSED(model); }, true);
            consumed = true;
            break;
        case BleScannerCustomEventDisconnected:
            with_view_model(
                app->device_view,
                BleScannerApp** model,
                { UNUSED(model); app->connected = false; },
                true);
            consumed = true;
            break;
        default:
            break;
        }
    }

    return consumed;
}

void ble_scanner_scene_device_on_exit(void* context) {
    BleScannerApp* app = context;

    // Disconnect if still connected
    if(app->connected) {
        bt_disconnect_central(app->bt);
        app->connected = false;
    }

    // Free GATT client
    if(app->gatt_client) {
        ble_gatt_client_free(app->gatt_client);
        app->gatt_client = NULL;
    }

    view_dispatcher_remove_view(app->view_dispatcher, BleScannerViewDevice);
    if(app->device_view) {
        view_free(app->device_view);
        app->device_view = NULL;
    }
}
