#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/scene_manager.h>
#include <gui/elements.h>
#include <bt/bt_service/bt.h>
#include <furi_hal_bt.h>
#include <furi_ble/gatt_client.h>

#define BLE_SCANNER_MAX_DEVICES 64
#define BLE_SCANNER_MAX_SERVICES 20
#define BLE_SCANNER_MAX_CHARS    32
#define BLE_SCANNER_NAME_MAX   32
#define BLE_SCANNER_VISIBLE    4

typedef struct {
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi;
    char name[BLE_SCANNER_NAME_MAX];
    bool has_name;
    uint32_t last_seen;
} BleScannerDevice;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    SceneManager* scene_manager;
    View* scan_view;
    Bt* bt;

    BleScannerDevice devices[BLE_SCANNER_MAX_DEVICES];
    uint8_t device_count;
    bool scanning;
    volatile bool needs_redraw;
    int16_t selected_index;
    FuriMutex* data_mutex;

    // Device detail / GATT state
    View* device_view;
    BleGattClient* gatt_client;
    bool connected;
    bool discovering;
    BleGattClientService gatt_services[BLE_SCANNER_MAX_SERVICES];
    uint8_t gatt_service_count;
    BleGattClientCharacteristic gatt_chars[BLE_SCANNER_MAX_CHARS];
    uint8_t gatt_char_count;
    int16_t device_scroll;
    uint8_t connection_retries;

    // Service detail / characteristic state
    View* service_view;
    uint8_t selected_service;
    int16_t service_scroll;
    uint8_t read_buf[BLE_GATT_CLIENT_MAX_VALUE_LEN];
    uint16_t read_len;
    bool reading;
} BleScannerApp;

typedef enum {
    BleScannerViewScan,
    BleScannerViewDevice,
    BleScannerViewService,
} BleScannerView;

typedef enum {
    BleScannerCustomEventToggleScan,
    BleScannerCustomEventRefresh,
    BleScannerCustomEventConnected,
    BleScannerCustomEventConnectionTick,
    BleScannerCustomEventDisconnected,
    BleScannerCustomEventServiceSelected,
    BleScannerCustomEventServicesDiscovered,
    BleScannerCustomEventCharsReady,
    BleScannerCustomEventReadDone,
} BleScannerCustomEvent;
