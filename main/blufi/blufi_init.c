#include <stdio.h>
#include "esp_err.h"
#include "esp_blufi_api.h"
#include "esp_log.h"
#include "esp_blufi.h"
#include "blufi.h"
#ifdef CONFIG_BT_BLUEDROID_ENABLED
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#endif

#ifdef CONFIG_BT_NIMBLE_ENABLED
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "console/console.h"
#endif


#ifdef CONFIG_BT_BLUEDROID_ENABLED



#endif /* CONFIG_BT_BLUEDROID_ENABLED */


#ifdef CONFIG_BT_NIMBLE_ENABLED
void ble_store_config_init(void);

static void
blufi_on_sync(void)
{
  esp_blufi_profile_init();
}





#endif /* CONFIG_BT_NIMBLE_ENABLED */
