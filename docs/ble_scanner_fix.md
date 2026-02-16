# Fix: BLE Scanner Connect Crash (v3 — view lifecycle)

## Context

App crashes/exits immediately when pressing Right to connect. The v2 fix (synchronous bt_connect) got bt_connect actually called (visible in logs as "call 13"), but the app exits ~5ms later with "BLE Scanner exiting".

## Root Cause

**`view_dispatcher_remove_view()` stops the event loop when the removed view is the current view.**

In `view_dispatcher.c:188-190`:
```c
if(view_dispatcher->current_view == view) {
    view_dispatcher_set_current_view(view_dispatcher, NULL);  // → calls view_dispatcher_stop()!
}
```

`view_dispatcher_set_current_view(NULL)` calls `furi_event_loop_stop()` at line 430.

**Crash sequence** (when pressing Right to connect):
1. InputTypeShort(Right) → scan scene sends `BleScannerCustomEventConnected`
2. Connected handler → `scene_manager_next_scene(BleScannerSceneDevice)`
3. `scene_manager_next_scene` calls scan `on_exit` first, THEN device `on_enter`
4. Scan `on_exit` calls `view_dispatcher_remove_view(BleScannerViewScan)` — **scan_view IS current_view** → `view_dispatcher_stop()` → event loop stop flag set
5. Device `on_enter` creates device_view, switches to it — but stop flag is already set
6. Event handler returns → event loop checks stop flag → **exits**
7. `view_dispatcher_run()` returns → app logs "BLE Scanner exiting"

This same bug affects ALL scene transitions (Scan→Device, Device→Service) because every scene's `on_exit` calls `view_dispatcher_remove_view` while its view is still current.

## What's Already Applied (keep)

- **bt.c Change A**: ScanStop handler guarded with `if(bt->scanning)`
- **bt.c Change B**: Connect handler cleans up scan state if needed
- **v2 changes**: synchronous bt_connect in device on_enter, `needs_connect` removed

## Fix: Keep views alive across scene transitions

Follow the standard Flipper app pattern: allocate views once, reuse across scene entries, free on app exit.

### File 1: `applications_user/ble_scanner/scenes/ble_scanner_scene_scan.c`

**on_enter** — guard view creation, always switch:
```c
void ble_scanner_scene_scan_on_enter(void* context) {
    BleScannerApp* app = context;

    furi_check(furi_mutex_acquire(app->data_mutex, FuriWaitForever) == FuriStatusOk);
    app->device_count = 0;
    app->selected_index = 0;
    app->scanning = false;
    furi_mutex_release(app->data_mutex);

    if(!app->scan_view) {
        app->scan_view = view_alloc();
        view_allocate_model(app->scan_view, ViewModelTypeLocking, sizeof(BleScannerApp*));
        with_view_model(
            app->scan_view, BleScannerApp** model, { *model = app; }, false);
        view_set_context(app->scan_view, app->scan_view);
        view_set_draw_callback(app->scan_view, ble_scanner_scan_draw_callback);
        view_set_input_callback(app->scan_view, ble_scanner_scan_input_callback);
        view_dispatcher_add_view(app->view_dispatcher, BleScannerViewScan, app->scan_view);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, BleScannerViewScan);
}
```

**on_exit** — remove view_dispatcher_remove_view and view_free:
```c
void ble_scanner_scene_scan_on_exit(void* context) {
    BleScannerApp* app = context;
    ble_scanner_stop_scan(app);
    // View stays registered — freed in app_free
}
```

### File 2: `applications_user/ble_scanner/scenes/ble_scanner_scene_device.c`

**on_enter** — guard view creation, always switch:
```c
void ble_scanner_scene_device_on_enter(void* context) {
    BleScannerApp* app = context;

    app->connected = false;
    app->discovering = false;
    app->gatt_service_count = 0;
    app->gatt_char_count = 0;
    app->device_scroll = 0;

    if(!app->device_view) {
        app->device_view = view_alloc();
        view_allocate_model(app->device_view, ViewModelTypeLocking, sizeof(BleScannerApp*));
        with_view_model(
            app->device_view, BleScannerApp** model, { *model = app; }, false);
        view_set_context(app->device_view, app->device_view);
        view_set_draw_callback(app->device_view, ble_scanner_device_draw_callback);
        view_set_input_callback(app->device_view, ble_scanner_device_input_callback);
        view_dispatcher_add_view(app->view_dispatcher, BleScannerViewDevice, app->device_view);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, BleScannerViewDevice);

    // Allocate GATT client (per-connection, freed in on_exit)
    app->gatt_client = ble_gatt_client_alloc();

    // Initiate connection
    uint16_t idx = scene_manager_get_scene_state(app->scene_manager, BleScannerSceneScan);
    BleScannerDevice* dev = &app->devices[idx];
    FURI_LOG_I(TAG, "Connecting to %02X:%02X:%02X:%02X:%02X:%02X",
        dev->address[0], dev->address[1], dev->address[2],
        dev->address[3], dev->address[4], dev->address[5]);
    bool result = bt_connect(app->bt, dev->address, dev->address_type);
    if(result) {
        app->connection_retries = 20;
    } else {
        FURI_LOG_E(TAG, "Connection initiation failed");
        app->connection_retries = 0;
    }
}
```

**on_exit** — remove view cleanup, keep connection/GATT cleanup:
```c
void ble_scanner_scene_device_on_exit(void* context) {
    BleScannerApp* app = context;

    app->connection_retries = 0;

    if(app->connected) {
        bt_disconnect_central(app->bt);
        app->connected = false;
    }

    if(app->gatt_client) {
        ble_gatt_client_free(app->gatt_client);
        app->gatt_client = NULL;
    }
    // View stays registered — freed in app_free
}
```

### File 3: `applications_user/ble_scanner/scenes/ble_scanner_scene_service.c`

**on_enter** — guard view creation:
```c
void ble_scanner_scene_service_on_enter(void* context) {
    BleScannerApp* app = context;

    app->service_scroll = 0;
    app->gatt_char_count = 0;
    app->read_len = 0;
    app->reading = false;
    app->discovering = true;

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

    // Start characteristic discovery
    BleGattClientService* svc = &app->gatt_services[app->selected_service];
    FURI_LOG_I(TAG, "Discovering chars for service [%04X-%04X]",
        svc->start_handle, svc->end_handle);
    ble_gatt_client_discover_characteristics(
        app->gatt_client, svc->start_handle, svc->end_handle,
        ble_scanner_service_chars_cb, app);
}
```

**on_exit** — remove view cleanup:
```c
void ble_scanner_scene_service_on_exit(void* context) {
    UNUSED(context);
    // View stays registered — freed in app_free
}
```

### File 4: `applications_user/ble_scanner/ble_scanner_app.c`

**app_free** — add view cleanup before scene_manager_free:
```c
static void ble_scanner_app_free(BleScannerApp* app) {
    furi_assert(app);

    // Remove and free views
    if(app->scan_view) {
        view_dispatcher_remove_view(app->view_dispatcher, BleScannerViewScan);
        view_free(app->scan_view);
    }
    if(app->device_view) {
        view_dispatcher_remove_view(app->view_dispatcher, BleScannerViewDevice);
        view_free(app->device_view);
    }
    if(app->service_view) {
        view_dispatcher_remove_view(app->view_dispatcher, BleScannerViewService);
        view_free(app->service_view);
    }

    scene_manager_free(app->scene_manager);
    view_dispatcher_free(app->view_dispatcher);
    furi_mutex_free(app->data_mutex);
    furi_record_close(RECORD_BT);
    furi_record_close(RECORD_GUI);
    free(app);
}
```

### File 5: `applications_user/ble_scanner/ble_scanner_app.h`

Remove `bool needs_connect;` field (already done in v2).

## Verification

1. `./fbt fap_ble_scanner` — app-only build (no firmware changes needed)
2. Scan → find devices → stop scan → press Right to connect → should show "Connecting..." and stay on device scene (NOT exit)
3. Back from device scene → should return to scan scene (not crash)
4. Re-connect (Right again) → should work on second entry too
5. Scan → Device → connect → view services → Back → Back → Back → clean exit
