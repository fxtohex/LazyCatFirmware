#include "gatt_client.h"
#include "event_dispatcher.h"
#include "../app_common.h"
#include <ble/ble.h>
#include <furi.h>

#define TAG "GattClient"

typedef enum {
    BleGattClientStateIdle,
    BleGattClientStateDiscoveringServices,
    BleGattClientStateDiscoveringCharacteristics,
    BleGattClientStateReading,
    BleGattClientStateWriting,
    BleGattClientStateSubscribing,
} BleGattClientState;

struct BleGattClient {
    uint16_t connection_handle;
    BleGattClientState state;

    // Service discovery
    BleGattClientService services[BLE_GATT_CLIENT_MAX_SERVICES];
    uint8_t service_count;
    BleGattClientDiscoverServicesCallback discover_svc_cb;
    void* discover_svc_context;

    // Characteristic discovery
    BleGattClientCharacteristic characteristics[BLE_GATT_CLIENT_MAX_CHARACTERISTICS];
    uint8_t char_count;
    BleGattClientDiscoverCharsCallback discover_char_cb;
    void* discover_char_context;

    // Read
    BleGattClientReadCallback read_cb;
    void* read_context;
    uint8_t read_buf[BLE_GATT_CLIENT_MAX_VALUE_LEN];
    uint16_t read_buf_len;

    // Write
    BleGattClientWriteCallback write_cb;
    void* write_context;

    // Notification
    BleGattClientNotifyCallback notify_cb;
    void* notify_context;

    // Event handler registration
    GapSvcEventHandler* event_handler;
};

static BleEventAckStatus
    ble_gatt_client_event_handler(void* raw_event, void* context);

BleGattClient* ble_gatt_client_alloc(void) {
    BleGattClient* client = malloc(sizeof(BleGattClient));
    memset(client, 0, sizeof(BleGattClient));
    client->connection_handle = 0xFFFF;
    client->state = BleGattClientStateIdle;

    // Register as a BLE event handler
    client->event_handler =
        ble_event_dispatcher_register_svc_handler(ble_gatt_client_event_handler, client);

    return client;
}

void ble_gatt_client_free(BleGattClient* client) {
    furi_check(client);

    if(client->event_handler) {
        ble_event_dispatcher_unregister_svc_handler(client->event_handler);
    }

    free(client);
}

void ble_gatt_client_set_connection(BleGattClient* client, uint16_t connection_handle) {
    furi_check(client);
    client->connection_handle = connection_handle;
    client->state = BleGattClientStateIdle;
    client->service_count = 0;
    client->char_count = 0;
    client->read_buf_len = 0;
    client->discover_svc_cb = NULL;
    client->discover_char_cb = NULL;
    client->read_cb = NULL;
    client->write_cb = NULL;
}

bool ble_gatt_client_discover_services(
    BleGattClient* client,
    BleGattClientDiscoverServicesCallback callback,
    void* context) {
    furi_check(client);
    furi_check(callback);

    if(client->state != BleGattClientStateIdle) {
        FURI_LOG_W(TAG, "Client busy, state: %d", client->state);
        return false;
    }
    if(client->connection_handle == 0xFFFF) {
        FURI_LOG_W(TAG, "No connection");
        return false;
    }

    client->service_count = 0;
    client->discover_svc_cb = callback;
    client->discover_svc_context = context;
    client->state = BleGattClientStateDiscoveringServices;

    tBleStatus status = aci_gatt_disc_all_primary_services(client->connection_handle);
    if(status != BLE_STATUS_SUCCESS) {
        FURI_LOG_E(TAG, "Discover services failed: 0x%02X", status);
        client->state = BleGattClientStateIdle;
        return false;
    }

    FURI_LOG_D(TAG, "Service discovery started");
    return true;
}

bool ble_gatt_client_discover_characteristics(
    BleGattClient* client,
    uint16_t start_handle,
    uint16_t end_handle,
    BleGattClientDiscoverCharsCallback callback,
    void* context) {
    furi_check(client);
    furi_check(callback);

    if(client->state != BleGattClientStateIdle) {
        FURI_LOG_W(TAG, "Client busy, state: %d", client->state);
        return false;
    }
    if(client->connection_handle == 0xFFFF) {
        FURI_LOG_W(TAG, "No connection");
        return false;
    }

    client->char_count = 0;
    client->discover_char_cb = callback;
    client->discover_char_context = context;
    client->state = BleGattClientStateDiscoveringCharacteristics;

    tBleStatus status =
        aci_gatt_disc_all_char_of_service(client->connection_handle, start_handle, end_handle);
    if(status != BLE_STATUS_SUCCESS) {
        FURI_LOG_E(TAG, "Discover chars failed: 0x%02X", status);
        client->state = BleGattClientStateIdle;
        return false;
    }

    FURI_LOG_D(TAG, "Characteristic discovery started");
    return true;
}

bool ble_gatt_client_read(
    BleGattClient* client,
    uint16_t attr_handle,
    BleGattClientReadCallback callback,
    void* context) {
    furi_check(client);
    furi_check(callback);

    if(client->state != BleGattClientStateIdle) {
        FURI_LOG_W(TAG, "Client busy, state: %d", client->state);
        return false;
    }
    if(client->connection_handle == 0xFFFF) {
        FURI_LOG_W(TAG, "No connection");
        return false;
    }

    client->read_cb = callback;
    client->read_context = context;
    client->state = BleGattClientStateReading;

    tBleStatus status = aci_gatt_read_char_value(client->connection_handle, attr_handle);
    if(status != BLE_STATUS_SUCCESS) {
        FURI_LOG_E(TAG, "Read failed: 0x%02X", status);
        client->state = BleGattClientStateIdle;
        return false;
    }

    return true;
}

bool ble_gatt_client_write(
    BleGattClient* client,
    uint16_t attr_handle,
    const uint8_t* data,
    uint16_t data_len,
    BleGattClientWriteCallback callback,
    void* context) {
    furi_check(client);
    furi_check(data);
    furi_check(callback);

    if(client->state != BleGattClientStateIdle) {
        FURI_LOG_W(TAG, "Client busy, state: %d", client->state);
        return false;
    }
    if(client->connection_handle == 0xFFFF) {
        FURI_LOG_W(TAG, "No connection");
        return false;
    }

    client->write_cb = callback;
    client->write_context = context;
    client->state = BleGattClientStateWriting;

    tBleStatus status =
        aci_gatt_write_char_value(client->connection_handle, attr_handle, data_len, data);
    if(status != BLE_STATUS_SUCCESS) {
        FURI_LOG_E(TAG, "Write failed: 0x%02X", status);
        client->state = BleGattClientStateIdle;
        return false;
    }

    return true;
}

bool ble_gatt_client_write_no_resp(
    BleGattClient* client,
    uint16_t attr_handle,
    const uint8_t* data,
    uint16_t data_len) {
    furi_check(client);
    furi_check(data);

    if(client->connection_handle == 0xFFFF) {
        FURI_LOG_W(TAG, "No connection");
        return false;
    }

    tBleStatus status =
        aci_gatt_write_without_resp(client->connection_handle, attr_handle, data_len, data);
    if(status != BLE_STATUS_SUCCESS) {
        FURI_LOG_E(TAG, "Write no resp failed: 0x%02X", status);
        return false;
    }

    return true;
}

bool ble_gatt_client_subscribe(
    BleGattClient* client,
    uint16_t cccd_handle,
    bool notifications,
    bool indications,
    BleGattClientNotifyCallback callback,
    void* context) {
    furi_check(client);

    if(client->state != BleGattClientStateIdle) {
        FURI_LOG_W(TAG, "Client busy, state: %d", client->state);
        return false;
    }
    if(client->connection_handle == 0xFFFF) {
        FURI_LOG_W(TAG, "No connection");
        return false;
    }

    client->notify_cb = callback;
    client->notify_context = context;
    client->state = BleGattClientStateSubscribing;

    uint8_t cccd_val[2] = {0, 0};
    if(notifications) cccd_val[0] |= 0x01;
    if(indications) cccd_val[0] |= 0x02;

    tBleStatus status =
        aci_gatt_write_char_desc(client->connection_handle, cccd_handle, 2, cccd_val);
    if(status != BLE_STATUS_SUCCESS) {
        FURI_LOG_E(TAG, "Subscribe failed: 0x%02X", status);
        client->state = BleGattClientStateIdle;
        return false;
    }

    return true;
}

bool ble_gatt_client_unsubscribe(BleGattClient* client, uint16_t cccd_handle) {
    furi_check(client);

    if(client->connection_handle == 0xFFFF) {
        return false;
    }

    uint8_t cccd_val[2] = {0, 0};
    tBleStatus status =
        aci_gatt_write_char_desc(client->connection_handle, cccd_handle, 2, cccd_val);
    if(status != BLE_STATUS_SUCCESS) {
        FURI_LOG_E(TAG, "Unsubscribe failed: 0x%02X", status);
        return false;
    }

    client->notify_cb = NULL;
    client->notify_context = NULL;
    return true;
}

// Parse service data from ATT_READ_BY_GROUP_TYPE_RESP event
static void ble_gatt_client_handle_service_discovery(
    BleGattClient* client,
    aci_att_read_by_group_type_resp_event_rp0* evt) {
    if(client->state != BleGattClientStateDiscoveringServices) return;
    if(evt->Connection_Handle != client->connection_handle) return;

    uint8_t attr_len = evt->Attribute_Data_Length;
    uint8_t num_entries = evt->Data_Length / attr_len;

    for(uint8_t i = 0; i < num_entries && client->service_count < BLE_GATT_CLIENT_MAX_SERVICES;
        i++) {
        uint8_t* data = &evt->Attribute_Data_List[i * attr_len];
        BleGattClientService* svc = &client->services[client->service_count];

        svc->start_handle = (uint16_t)(data[0]) | ((uint16_t)(data[1]) << 8);
        svc->end_handle = (uint16_t)(data[2]) | ((uint16_t)(data[3]) << 8);

        memset(svc->uuid, 0, sizeof(svc->uuid));
        if(attr_len == 6) {
            // 16-bit UUID
            svc->uuid_type = 1;
            memcpy(svc->uuid, &data[4], 2);
        } else if(attr_len == 20) {
            // 128-bit UUID
            svc->uuid_type = 2;
            memcpy(svc->uuid, &data[4], 16);
        }

        FURI_LOG_D(
            TAG,
            "Service: handles %04X-%04X uuid_type=%d",
            svc->start_handle,
            svc->end_handle,
            svc->uuid_type);

        client->service_count++;
    }
}

// Parse characteristic data from ATT_READ_BY_TYPE_RESP event
static void ble_gatt_client_handle_char_discovery(
    BleGattClient* client,
    aci_att_read_by_type_resp_event_rp0* evt) {
    if(client->state != BleGattClientStateDiscoveringCharacteristics) return;
    if(evt->Connection_Handle != client->connection_handle) return;

    uint8_t handle_value_pair_len = evt->Handle_Value_Pair_Length;
    uint8_t data_len = evt->Data_Length;
    uint8_t num_entries = data_len / handle_value_pair_len;

    for(uint8_t i = 0;
        i < num_entries && client->char_count < BLE_GATT_CLIENT_MAX_CHARACTERISTICS;
        i++) {
        uint8_t* data = &evt->Handle_Value_Pair_Data[i * handle_value_pair_len];
        BleGattClientCharacteristic* chr = &client->characteristics[client->char_count];

        // handle_value_pair: [handle(2)] [properties(1)] [value_handle(2)] [uuid(2 or 16)]
        chr->handle = (uint16_t)(data[0]) | ((uint16_t)(data[1]) << 8);
        chr->properties = data[2];
        chr->value_handle = (uint16_t)(data[3]) | ((uint16_t)(data[4]) << 8);

        memset(chr->uuid, 0, sizeof(chr->uuid));
        uint8_t uuid_len = handle_value_pair_len - 5;
        if(uuid_len == 2) {
            chr->uuid_type = 1;
            memcpy(chr->uuid, &data[5], 2);
        } else if(uuid_len == 16) {
            chr->uuid_type = 2;
            memcpy(chr->uuid, &data[5], 16);
        }

        FURI_LOG_D(
            TAG,
            "Char: handle=%04X value=%04X props=%02X",
            chr->handle,
            chr->value_handle,
            chr->properties);

        client->char_count++;
    }
}

// Handle read response - buffer data for delivery on proc_complete
static void ble_gatt_client_handle_read_resp(
    BleGattClient* client,
    aci_att_read_resp_event_rp0* evt) {
    if(client->state != BleGattClientStateReading) return;
    if(evt->Connection_Handle != client->connection_handle) return;

    // Store data in buffer; deliver on GATT proc complete to avoid double callback
    uint16_t len = evt->Event_Data_Length;
    if(len > BLE_GATT_CLIENT_MAX_VALUE_LEN) len = BLE_GATT_CLIENT_MAX_VALUE_LEN;
    memcpy(client->read_buf, evt->Attribute_Value, len);
    client->read_buf_len = len;
}

// Handle notification event
static void ble_gatt_client_handle_notification(
    BleGattClient* client,
    aci_gatt_notification_event_rp0* evt) {
    if(evt->Connection_Handle != client->connection_handle) return;

    if(client->notify_cb) {
        client->notify_cb(
            evt->Attribute_Handle,
            evt->Attribute_Value,
            evt->Attribute_Value_Length,
            client->notify_context);
    }
}

// Handle indication event
static void ble_gatt_client_handle_indication(
    BleGattClient* client,
    aci_gatt_indication_event_rp0* evt) {
    if(evt->Connection_Handle != client->connection_handle) return;

    if(client->notify_cb) {
        client->notify_cb(
            evt->Attribute_Handle,
            evt->Attribute_Value,
            evt->Attribute_Value_Length,
            client->notify_context);
    }

    // Confirm indication
    aci_gatt_confirm_indication(client->connection_handle);
}

// Handle GATT procedure complete
static void ble_gatt_client_handle_proc_complete(
    BleGattClient* client,
    aci_gatt_proc_complete_event_rp0* evt) {
    if(evt->Connection_Handle != client->connection_handle) return;

    BleGattClientStatus status = (evt->Error_Code == 0) ? BleGattClientStatusSuccess :
                                                           BleGattClientStatusError;

    switch(client->state) {
    case BleGattClientStateDiscoveringServices:
        client->state = BleGattClientStateIdle;
        if(client->discover_svc_cb) {
            client->discover_svc_cb(
                status, client->services, client->service_count, client->discover_svc_context);
        }
        break;

    case BleGattClientStateDiscoveringCharacteristics:
        client->state = BleGattClientStateIdle;
        if(client->discover_char_cb) {
            client->discover_char_cb(
                status,
                client->characteristics,
                client->char_count,
                client->discover_char_context);
        }
        break;

    case BleGattClientStateReading:
        client->state = BleGattClientStateIdle;
        if(client->read_cb) {
            if(status == BleGattClientStatusSuccess && client->read_buf_len > 0) {
                client->read_cb(
                    status, client->read_buf, client->read_buf_len, client->read_context);
            } else {
                client->read_cb(status, NULL, 0, client->read_context);
            }
        }
        client->read_buf_len = 0;
        break;

    case BleGattClientStateWriting:
        client->state = BleGattClientStateIdle;
        if(client->write_cb) {
            client->write_cb(status, client->write_context);
        }
        break;

    case BleGattClientStateSubscribing:
        client->state = BleGattClientStateIdle;
        // Subscribe complete - notify callback already set
        break;

    default:
        break;
    }
}

static BleEventAckStatus
    ble_gatt_client_event_handler(void* raw_event, void* context) {
    BleGattClient* client = (BleGattClient*)context;
    hci_event_pckt* event_pckt = (hci_event_pckt*)(((hci_uart_pckt*)raw_event)->data);

    if(event_pckt->evt != HCI_VENDOR_SPECIFIC_DEBUG_EVT_CODE) return BleEventNotAck;

    evt_blecore_aci* vendor_evt = (evt_blecore_aci*)event_pckt->data;

    switch(vendor_evt->ecode) {
    case ACI_ATT_READ_BY_GROUP_TYPE_RESP_VSEVT_CODE:
        ble_gatt_client_handle_service_discovery(
            client, (aci_att_read_by_group_type_resp_event_rp0*)vendor_evt->data);
        return BleEventAckFlowEnable;

    case ACI_ATT_READ_BY_TYPE_RESP_VSEVT_CODE:
        if(client->state == BleGattClientStateDiscoveringCharacteristics) {
            ble_gatt_client_handle_char_discovery(
                client, (aci_att_read_by_type_resp_event_rp0*)vendor_evt->data);
            return BleEventAckFlowEnable;
        }
        return BleEventNotAck;

    case ACI_ATT_READ_RESP_VSEVT_CODE:
        if(client->state == BleGattClientStateReading) {
            ble_gatt_client_handle_read_resp(
                client, (aci_att_read_resp_event_rp0*)vendor_evt->data);
            return BleEventAckFlowEnable;
        }
        return BleEventNotAck;

    case ACI_GATT_NOTIFICATION_VSEVT_CODE:
        ble_gatt_client_handle_notification(
            client, (aci_gatt_notification_event_rp0*)vendor_evt->data);
        return BleEventAckFlowEnable;

    case ACI_GATT_INDICATION_VSEVT_CODE:
        ble_gatt_client_handle_indication(
            client, (aci_gatt_indication_event_rp0*)vendor_evt->data);
        return BleEventAckFlowEnable;

    case ACI_GATT_PROC_COMPLETE_VSEVT_CODE:
        if(client->state != BleGattClientStateIdle) {
            ble_gatt_client_handle_proc_complete(
                client, (aci_gatt_proc_complete_event_rp0*)vendor_evt->data);
            return BleEventAckFlowEnable;
        }
        return BleEventNotAck;

    default:
        return BleEventNotAck;
    }
}

bool ble_gatt_client_process_event(BleGattClient* client, void* event) {
    BleEventAckStatus status = ble_gatt_client_event_handler(event, client);
    return status != BleEventNotAck;
}
