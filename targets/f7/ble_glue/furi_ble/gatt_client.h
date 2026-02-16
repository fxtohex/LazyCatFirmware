#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <furi.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_GATT_CLIENT_MAX_SERVICES        20
#define BLE_GATT_CLIENT_MAX_CHARACTERISTICS 32
#define BLE_GATT_CLIENT_MAX_VALUE_LEN       247

typedef struct {
    uint16_t start_handle;
    uint16_t end_handle;
    uint8_t uuid[16];
    uint8_t uuid_type; // 1 = 16-bit, 2 = 128-bit
} BleGattClientService;

typedef struct {
    uint16_t handle;
    uint16_t value_handle;
    uint8_t properties;
    uint8_t uuid[16];
    uint8_t uuid_type; // 1 = 16-bit, 2 = 128-bit
} BleGattClientCharacteristic;

typedef enum {
    BleGattClientStatusSuccess,
    BleGattClientStatusError,
    BleGattClientStatusTimeout,
    BleGattClientStatusDisconnected,
} BleGattClientStatus;

typedef void (*BleGattClientDiscoverServicesCallback)(
    BleGattClientStatus status,
    BleGattClientService* services,
    uint8_t count,
    void* context);

typedef void (*BleGattClientDiscoverCharsCallback)(
    BleGattClientStatus status,
    BleGattClientCharacteristic* chars,
    uint8_t count,
    void* context);

typedef void (*BleGattClientReadCallback)(
    BleGattClientStatus status,
    const uint8_t* data,
    uint16_t data_len,
    void* context);

typedef void (*BleGattClientWriteCallback)(
    BleGattClientStatus status,
    void* context);

typedef void (*BleGattClientNotifyCallback)(
    uint16_t attr_handle,
    const uint8_t* data,
    uint16_t data_len,
    void* context);

typedef struct BleGattClient BleGattClient;

/** Allocate GATT client instance */
BleGattClient* ble_gatt_client_alloc(void);

/** Free GATT client instance */
void ble_gatt_client_free(BleGattClient* client);

/** Set connection handle for this client */
void ble_gatt_client_set_connection(BleGattClient* client, uint16_t connection_handle);

/** Discover all primary services on the remote device
 *
 * @param client    GATT client instance
 * @param callback  callback with discovered services
 * @param context   user context
 * @return true if discovery started
 */
bool ble_gatt_client_discover_services(
    BleGattClient* client,
    BleGattClientDiscoverServicesCallback callback,
    void* context);

/** Discover all characteristics of a service
 *
 * @param client        GATT client instance
 * @param start_handle  service start handle
 * @param end_handle    service end handle
 * @param callback      callback with discovered characteristics
 * @param context       user context
 * @return true if discovery started
 */
bool ble_gatt_client_discover_characteristics(
    BleGattClient* client,
    uint16_t start_handle,
    uint16_t end_handle,
    BleGattClientDiscoverCharsCallback callback,
    void* context);

/** Read a characteristic value
 *
 * @param client       GATT client instance
 * @param attr_handle  attribute handle to read
 * @param callback     callback with read data
 * @param context      user context
 * @return true if read request sent
 */
bool ble_gatt_client_read(
    BleGattClient* client,
    uint16_t attr_handle,
    BleGattClientReadCallback callback,
    void* context);

/** Write a characteristic value (with response)
 *
 * @param client       GATT client instance
 * @param attr_handle  attribute handle to write
 * @param data         data to write
 * @param data_len     data length
 * @param callback     callback on completion
 * @param context      user context
 * @return true if write request sent
 */
bool ble_gatt_client_write(
    BleGattClient* client,
    uint16_t attr_handle,
    const uint8_t* data,
    uint16_t data_len,
    BleGattClientWriteCallback callback,
    void* context);

/** Write a characteristic value without response
 *
 * @param client       GATT client instance
 * @param attr_handle  attribute handle to write
 * @param data         data to write
 * @param data_len     data length
 * @return true if write sent
 */
bool ble_gatt_client_write_no_resp(
    BleGattClient* client,
    uint16_t attr_handle,
    const uint8_t* data,
    uint16_t data_len);

/** Subscribe to notifications/indications
 *
 * @param client          GATT client instance
 * @param cccd_handle     CCCD (Client Characteristic Configuration Descriptor) handle
 * @param notifications   enable notifications
 * @param indications     enable indications
 * @param callback        callback for received notifications
 * @param context         user context
 * @return true if subscription request sent
 */
bool ble_gatt_client_subscribe(
    BleGattClient* client,
    uint16_t cccd_handle,
    bool notifications,
    bool indications,
    BleGattClientNotifyCallback callback,
    void* context);

/** Unsubscribe from notifications/indications
 *
 * @param client       GATT client instance
 * @param cccd_handle  CCCD handle
 * @return true if unsubscribe request sent
 */
bool ble_gatt_client_unsubscribe(
    BleGattClient* client,
    uint16_t cccd_handle);

/** Process a BLE vendor-specific event for this client.
 *  Should be called from the BLE event dispatcher.
 *
 * @param client   GATT client instance
 * @param event    raw HCI event payload
 * @return true if event was consumed
 */
bool ble_gatt_client_process_event(BleGattClient* client, void* event);

#ifdef __cplusplus
}
#endif
