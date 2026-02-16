#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <furi_ble/profile_interface.h>
#include <furi_hal_bt.h>
#include <core/common_defines.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RECORD_BT "bt"

typedef struct Bt Bt;

typedef enum {
    BtStatusUnavailable,
    BtStatusOff,
    BtStatusAdvertising,
    BtStatusConnected,
} BtStatus;

typedef void (*BtStatusChangedCallback)(BtStatus status, void* context);

/** Change BLE Profile
 * @note Call of this function leads to 2nd core restart
 *
 * @param bt                 Bt instance
 * @param profile_template   Profile template to change to
 * @param params             Profile parameters. Can be NULL
 *
 * @return          true on success
 */
FURI_WARN_UNUSED FuriHalBleProfileBase* bt_profile_start(
    Bt* bt,
    const FuriHalBleProfileTemplate* profile_template,
    FuriHalBleProfileParams params);

/** Stop current BLE Profile and restore default profile
 * @note Call of this function leads to 2nd core restart
 *
 * @param bt        Bt instance
 *
 * @return          true on success
 */
bool bt_profile_restore_default(Bt* bt);

/** Disconnect from Central
 *
 * @param bt        Bt instance
 */
void bt_disconnect(Bt* bt);

/** Set callback for Bluetooth status change notification
 *
 * @param bt        Bt instance
 * @param callback  BtStatusChangedCallback instance
 * @param context   pointer to context
 */
void bt_set_status_changed_callback(Bt* bt, BtStatusChangedCallback callback, void* context);

/** Forget bonded devices
 * @note Leads to wipe ble key storage and deleting bt.keys
 *
 * @param bt        Bt instance
 */
void bt_forget_bonded_devices(Bt* bt);

/** Set keys storage file path
 *
 * @param bt                    Bt instance
 * @param keys_storage_path     Path to file with saved keys
 */
void bt_keys_storage_set_storage_path(Bt* bt, const char* keys_storage_path);

/** Set default keys storage file path
 *
 * @param bt                    Bt instance
 */
void bt_keys_storage_set_default_path(Bt* bt);

/** Start BLE scanning
 *
 * @param bt        Bt instance
 * @param params    Scan parameters
 * @param cb        Callback for scan results
 * @param ctx       Callback context
 *
 * @return          true on success
 */
bool bt_start_scan(Bt* bt, FuriHalBtScanParams* params, FuriHalBtScanCallback cb, void* ctx);

/** Stop BLE scanning
 *
 * @param bt        Bt instance
 */
void bt_stop_scan(Bt* bt);

/** Connect to a BLE peripheral
 *
 * @param bt            Bt instance
 * @param address       6-byte BLE address of the peripheral
 * @param address_type  Address type (public or random)
 *
 * @return              true on success
 */
bool bt_connect(Bt* bt, const uint8_t* address, uint8_t address_type);

/** Disconnect from a BLE peripheral (central role)
 *
 * @param bt        Bt instance
 */
void bt_disconnect_central(Bt* bt);

/** Get the central connection handle
 *
 * @param bt        Bt instance
 * @return          connection handle, or 0xFFFF if not connected
 */
uint16_t bt_get_central_conn_handle(Bt* bt);

#ifdef __cplusplus
}
#endif
