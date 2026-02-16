#include "../ble_scanner_app.h"
#include "ble_scanner_scene.h"

#include <string.h>

#define TAG "BleScannerService"

static void ble_scanner_service_chars_cb(
    BleGattClientStatus status,
    BleGattClientCharacteristic* chars,
    uint8_t count,
    void* context) {
    BleScannerApp* app = context;

    if(status == BleGattClientStatusSuccess) {
        uint8_t n = count;
        if(n > BLE_SCANNER_MAX_CHARS) n = BLE_SCANNER_MAX_CHARS;
        memcpy(app->gatt_chars, chars, n * sizeof(BleGattClientCharacteristic));
        app->gatt_char_count = n;
        FURI_LOG_I(TAG, "Discovered %d characteristics", n);
    } else {
        FURI_LOG_E(TAG, "Characteristic discovery failed: %d", status);
        app->gatt_char_count = 0;
    }
    view_dispatcher_send_custom_event(
        app->view_dispatcher, BleScannerCustomEventCharsReady);
}

static void ble_scanner_service_read_cb(
    BleGattClientStatus status,
    const uint8_t* data,
    uint16_t data_len,
    void* context) {
    BleScannerApp* app = context;

    if(status == BleGattClientStatusSuccess) {
        app->read_len = data_len;
        if(app->read_len > sizeof(app->read_buf)) {
            app->read_len = sizeof(app->read_buf);
        }
        memcpy(app->read_buf, data, app->read_len);
        FURI_LOG_I(TAG, "Read %d bytes from handle", app->read_len);
    } else {
        FURI_LOG_E(TAG, "Read failed: %d", status);
        app->read_len = 0;
    }
    app->reading = false;
    view_dispatcher_send_custom_event(
        app->view_dispatcher, BleScannerCustomEventReadDone);
}

static void ble_scanner_service_draw_callback(Canvas* canvas, void* model) {
    BleScannerApp* app = *(BleScannerApp**)model;

    canvas_clear(canvas);

    // Header: service UUID
    BleGattClientService* svc = &app->gatt_services[app->selected_service];
    canvas_set_font(canvas, FontPrimary);
    char hdr[32];
    if(svc->uuid_type == 1) {
        uint16_t uuid16 = (uint16_t)(svc->uuid[0]) | ((uint16_t)(svc->uuid[1]) << 8);
        snprintf(hdr, sizeof(hdr), "Service 0x%04X", uuid16);
    } else {
        snprintf(hdr, sizeof(hdr), "Svc %02X%02X%02X%02X...",
            svc->uuid[15], svc->uuid[14], svc->uuid[13], svc->uuid[12]);
    }
    canvas_draw_str(canvas, 0, 11, hdr);
    canvas_draw_line(canvas, 0, 13, 127, 13);

    if(app->gatt_char_count == 0 && app->discovering) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, "Discovering...");
    } else if(app->gatt_char_count == 0) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 38, AlignCenter, AlignCenter, "No characteristics");
    } else {
        canvas_set_font(canvas, FontSecondary);
        int max_visible = 3;
        int16_t start = app->service_scroll;

        for(int i = start; i < app->gatt_char_count && (i - start) < max_visible; i++) {
            BleGattClientCharacteristic* ch = &app->gatt_chars[i];
            char line[48];

            // Build properties string
            char props[8] = "";
            int p = 0;
            if(ch->properties & 0x02) props[p++] = 'R';
            if(ch->properties & 0x08) props[p++] = 'W';
            if(ch->properties & 0x04) props[p++] = 'w'; // write no resp
            if(ch->properties & 0x10) props[p++] = 'N';
            if(ch->properties & 0x20) props[p++] = 'I';
            props[p] = '\0';

            if(ch->uuid_type == 1) {
                uint16_t uuid16 = (uint16_t)(ch->uuid[0]) | ((uint16_t)(ch->uuid[1]) << 8);
                snprintf(line, sizeof(line), "0x%04X [%s]", uuid16, props);
            } else {
                snprintf(line, sizeof(line), "%02X%02X.. [%s]",
                    ch->uuid[15], ch->uuid[14], props);
            }

            int y = 15 + (i - start) * 10;

            if(i == app->service_scroll) {
                canvas_draw_box(canvas, 0, y - 1, 127, 10);
                canvas_set_color(canvas, ColorWhite);
            }
            canvas_draw_str(canvas, 2, y + 7, line);
            if(i == app->service_scroll) {
                canvas_set_color(canvas, ColorBlack);
            }
        }

        // Show read result below the list
        int result_y = 15 + max_visible * 10 + 2;
        if(app->reading) {
            canvas_draw_str(canvas, 2, result_y + 7, "Reading...");
        } else if(app->read_len > 0) {
            char hex[64];
            int off = 0;
            int max_bytes = 10; // fit on screen
            for(int i = 0; i < app->read_len && i < max_bytes && off < (int)sizeof(hex) - 3;
                i++) {
                off += snprintf(hex + off, sizeof(hex) - off, "%02X ", app->read_buf[i]);
            }
            if(app->read_len > max_bytes) {
                snprintf(hex + off, sizeof(hex) - off, "...");
            }
            canvas_draw_str(canvas, 2, result_y + 7, hex);
        }

        // Scrollbar
        if(app->gatt_char_count > max_visible) {
            elements_scrollbar_pos(
                canvas, 126, 15, 30, app->service_scroll, app->gatt_char_count);
        }
    }

    // Bottom hint
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 0, 63, AlignLeft, AlignBottom, "OK:Read");
    canvas_draw_str_aligned(canvas, 127, 63, AlignRight, AlignBottom, "Back");
}

static bool ble_scanner_service_input_callback(InputEvent* event, void* context) {
    View* view = context;
    BleScannerApp* app = *(BleScannerApp**)view_get_model(view);

    int16_t sel = app->service_scroll;
    uint8_t count = app->gatt_char_count;
    ViewDispatcher* vd = app->view_dispatcher;

    bool consumed = false;
    bool need_redraw = false;

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        switch(event->key) {
        case InputKeyUp:
            if(sel > 0) {
                app->service_scroll--;
                consumed = true;
                need_redraw = true;
            }
            break;
        case InputKeyDown:
            if(sel < (int16_t)count - 1) {
                app->service_scroll++;
                consumed = true;
                need_redraw = true;
            }
            break;
        case InputKeyOk:
            if(count > 0 && sel < count) {
                consumed = true;
            }
            break;
        default:
            break;
        }
    }

    view_commit_model(view, need_redraw);

    // Dispatch read request after releasing model lock
    if(event->type == InputTypeShort && event->key == InputKeyOk &&
       count > 0 && sel < count) {
        BleGattClientCharacteristic* ch = &app->gatt_chars[sel];
        if(ch->properties & 0x02) { // readable
            app->reading = true;
            app->read_len = 0;
            ble_gatt_client_read(
                app->gatt_client, ch->value_handle,
                ble_scanner_service_read_cb, app);
            // Trigger redraw to show "Reading..."
            view_dispatcher_send_custom_event(vd, BleScannerCustomEventReadDone);
        }
    }

    return consumed;
}

void ble_scanner_scene_service_on_enter(void* context) {
    BleScannerApp* app = context;

    app->service_scroll = 0;
    app->gatt_char_count = 0;
    app->read_len = 0;
    app->reading = false;
    app->discovering = true;

    // Create service view on first entry, reuse on subsequent entries
    if(!app->service_view) {
        app->service_view = view_alloc();
        view_allocate_model(app->service_view, ViewModelTypeLocking, sizeof(BleScannerApp*));
        with_view_model(
            app->service_view, BleScannerApp** model, { *model = app; }, false);
        view_set_context(app->service_view, app->service_view);
        view_set_draw_callback(app->service_view, ble_scanner_service_draw_callback);
        view_set_input_callback(app->service_view, ble_scanner_service_input_callback);
        view_dispatcher_add_view(app->view_dispatcher, BleScannerViewService, app->service_view);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, BleScannerViewService);

    // Start characteristic discovery for the selected service
    BleGattClientService* svc = &app->gatt_services[app->selected_service];
    FURI_LOG_I(
        TAG,
        "Discovering chars for service [%04X-%04X]",
        svc->start_handle,
        svc->end_handle);

    ble_gatt_client_discover_characteristics(
        app->gatt_client,
        svc->start_handle,
        svc->end_handle,
        ble_scanner_service_chars_cb,
        app);
}

bool ble_scanner_scene_service_on_event(void* context, SceneManagerEvent event) {
    BleScannerApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case BleScannerCustomEventCharsReady:
            app->discovering = false;
            with_view_model(
                app->service_view, BleScannerApp** model, { UNUSED(model); }, true);
            consumed = true;
            break;
        case BleScannerCustomEventReadDone:
            with_view_model(
                app->service_view, BleScannerApp** model, { UNUSED(model); }, true);
            consumed = true;
            break;
        default:
            break;
        }
    }

    return consumed;
}

void ble_scanner_scene_service_on_exit(void* context) {
    UNUSED(context);
    // View stays registered — freed in app_free
}
