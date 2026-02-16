#include "../desktop_i.h"
#include <furi_hal_version.h>
#include <toolbox/version.h>

#define BOOT_SPLASH_TIMEOUT 2000
#define DesktopBootSplashEventTimeout 0x00AA00AA

void desktop_scene_boot_splash_callback(void* context) {
    Desktop* desktop = (Desktop*)context;
    view_dispatcher_send_custom_event(desktop->view_dispatcher, DesktopBootSplashEventTimeout);
}

void desktop_scene_boot_splash_on_enter(void* context) {
    Desktop* desktop = (Desktop*)context;

    Popup* popup = desktop->popup;
    popup_set_context(popup, desktop);
    popup_set_header(popup, "LazyCat", 64, 20 + STATUS_BAR_Y_SHIFT, AlignCenter, AlignCenter);

    const Version* ver = furi_hal_version_get_firmware_version();
    const char* version_str = ver ? version_get_version(ver) : "unknown";
    popup_set_text(popup, version_str, 64, 36 + STATUS_BAR_Y_SHIFT, AlignCenter, AlignCenter);

    popup_set_callback(popup, desktop_scene_boot_splash_callback);
    popup_set_timeout(popup, BOOT_SPLASH_TIMEOUT);
    popup_enable_timeout(popup);

    view_dispatcher_switch_to_view(desktop->view_dispatcher, DesktopViewIdPopup);
}

bool desktop_scene_boot_splash_on_event(void* context, SceneManagerEvent event) {
    Desktop* desktop = (Desktop*)context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == DesktopBootSplashEventTimeout) {
            scene_manager_previous_scene(desktop->scene_manager);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        scene_manager_previous_scene(desktop->scene_manager);
        consumed = true;
    }

    return consumed;
}

void desktop_scene_boot_splash_on_exit(void* context) {
    Desktop* desktop = (Desktop*)context;
    Popup* popup = desktop->popup;
    popup_reset(popup);
}
