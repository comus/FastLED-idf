/* Hello World Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"

#include "FastLED.h"
#include "FX.h"

#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_provisioning_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"

#include "board.h"
#include "ble_mesh_example_init.h"
#include "ble_mesh_example_nvs.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"

#define TAG "EXAMPLE"

#define CID_ESP 0x02E5

static uint8_t dev_uuid[16] = {0xdd, 0xdd};

static struct example_info_store
{
  uint16_t net_idx; /* NetKey Index */
  uint16_t app_idx; /* AppKey Index */
  uint8_t onoff;    /* Remote OnOff */
  uint8_t tid;      /* Message TID */
} __attribute__((packed)) store = {
    .net_idx = ESP_BLE_MESH_KEY_UNUSED,
    .app_idx = ESP_BLE_MESH_KEY_UNUSED,
    .onoff = LED_OFF,
    .tid = 0x0,
};

static nvs_handle_t NVS_HANDLE;
static const char *NVS_KEY = "onoff_client";

static esp_ble_mesh_client_t onoff_client;

static esp_ble_mesh_cfg_srv_t config_server = {
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay = ESP_BLE_MESH_RELAY_ENABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .beacon = ESP_BLE_MESH_BEACON_DISABLED,
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
    .default_ttl = 7,
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(onoff_cli_pub, 2 + 1, ROLE_NODE);

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(&onoff_cli_pub, &onoff_client),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, ESP_BLE_MESH_MODEL_NONE),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .element_count = ARRAY_SIZE(elements),
    .elements = elements,
};

/* Disable OOB security for SILabs Android app */
static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
    .output_size = 0,
    .output_actions = 0,
};

static void mesh_example_info_store(void)
{
  ble_mesh_nvs_store(NVS_HANDLE, NVS_KEY, &store, sizeof(store));
}

static void mesh_example_info_restore(void)
{
  esp_err_t err = ESP_OK;
  bool exist = false;

  err = ble_mesh_nvs_restore(NVS_HANDLE, NVS_KEY, &store, sizeof(store), &exist);
  if (err != ESP_OK)
  {
    return;
  }

  if (exist)
  {
    ESP_LOGI(TAG, "Restore, net_idx 0x%04x, app_idx 0x%04x, onoff %u, tid 0x%02x",
             store.net_idx, store.app_idx, store.onoff, store.tid);
  }
}

static void prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags, uint32_t iv_index)
{
  ESP_LOGI(TAG, "net_idx: 0x%04x, addr: 0x%04x", net_idx, addr);
  ESP_LOGI(TAG, "flags: 0x%02x, iv_index: 0x%08x", flags, iv_index);
  board_led_operation(LED_G, LED_OFF);
  store.net_idx = net_idx;
  /* mesh_example_info_store() shall not be invoked here, because if the device
     * is restarted and goes into a provisioned state, then the following events
     * will come:
     * 1st: ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT
     * 2nd: ESP_BLE_MESH_PROV_REGISTER_COMP_EVT
     * So the store.net_idx will be updated here, and if we store the mesh example
     * info here, the wrong app_idx (initialized with 0xFFFF) will be stored in nvs
     * just before restoring it.
     */
}

static void example_ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                             esp_ble_mesh_prov_cb_param_t *param)
{
  switch (event)
  {
  case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_PROV_REGISTER_COMP_EVT, err_code %d", param->prov_register_comp.err_code);
    mesh_example_info_restore(); /* Restore proper mesh example info */
    break;
  case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT, err_code %d", param->node_prov_enable_comp.err_code);
    break;
  case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT, bearer %s",
             param->node_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
    break;
  case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT, bearer %s",
             param->node_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV ? "PB-ADV" : "PB-GATT");
    break;
  case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT");
    prov_complete(param->node_prov_complete.net_idx, param->node_prov_complete.addr,
                  param->node_prov_complete.flags, param->node_prov_complete.iv_index);
    break;
  case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
    break;
  case ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_NODE_SET_UNPROV_DEV_NAME_COMP_EVT, err_code %d", param->node_set_unprov_dev_name_comp.err_code);
    break;
  default:
    break;
  }
}

bool is_clicked = false;

void example_ble_mesh_send_gen_onoff_set(void)
{
  esp_ble_mesh_generic_client_set_state_t set = {0};
  esp_ble_mesh_client_common_param_t common = {0};
  esp_err_t err = ESP_OK;

  common.opcode = ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK;
  common.model = onoff_client.model;
  common.ctx.net_idx = store.net_idx;
  common.ctx.app_idx = store.app_idx;
  common.ctx.addr = 0xFFFF; /* to all nodes */
  common.ctx.send_ttl = 3;
  common.ctx.send_rel = false;
  common.msg_timeout = 0; /* 0 indicates that timeout value from menuconfig will be used */
  common.msg_role = ROLE_NODE;

  set.onoff_set.op_en = false;
  set.onoff_set.onoff = store.onoff;
  set.onoff_set.tid = store.tid++;

  err = esp_ble_mesh_generic_client_set_state(&common, &set);
  if (err)
  {
    ESP_LOGE(TAG, "Send Generic OnOff Set Unack failed");
    return;
  }

  store.onoff = !store.onoff;
  mesh_example_info_store(); /* Store proper mesh example info */

  printf("click_button\n");
  is_clicked = true;
}

static void example_ble_mesh_generic_client_cb(esp_ble_mesh_generic_client_cb_event_t event,
                                               esp_ble_mesh_generic_client_cb_param_t *param)
{
  ESP_LOGI(TAG, "Generic client, event %u, error code %d, opcode is 0x%04x",
           event, param->error_code, param->params->opcode);

  switch (event)
  {
  case ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT");
    if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET)
    {
      ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET, onoff %d", param->status_cb.onoff_status.present_onoff);
    }
    break;
  case ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT");
    if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET)
    {
      ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET, onoff %d", param->status_cb.onoff_status.present_onoff);
    }
    break;
  case ESP_BLE_MESH_GENERIC_CLIENT_PUBLISH_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_PUBLISH_EVT");
    break;
  case ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT");
    if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET)
    {
      /* If failed to get the response of Generic OnOff Set, resend Generic OnOff Set  */
      example_ble_mesh_send_gen_onoff_set();
    }
    break;
  default:
    break;
  }
}

static void example_ble_mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                                              esp_ble_mesh_cfg_server_cb_param_t *param)
{
  if (event == ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT)
  {
    switch (param->ctx.recv_op)
    {
    case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
      ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD");
      ESP_LOGI(TAG, "net_idx 0x%04x, app_idx 0x%04x",
               param->value.state_change.appkey_add.net_idx,
               param->value.state_change.appkey_add.app_idx);
      ESP_LOG_BUFFER_HEX("AppKey", param->value.state_change.appkey_add.app_key, 16);
      break;
    case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
      ESP_LOGI(TAG, "ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND");
      ESP_LOGI(TAG, "elem_addr 0x%04x, app_idx 0x%04x, cid 0x%04x, mod_id 0x%04x",
               param->value.state_change.mod_app_bind.element_addr,
               param->value.state_change.mod_app_bind.app_idx,
               param->value.state_change.mod_app_bind.company_id,
               param->value.state_change.mod_app_bind.model_id);
      if (param->value.state_change.mod_app_bind.company_id == 0xFFFF &&
          param->value.state_change.mod_app_bind.model_id == ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI)
      {
        store.app_idx = param->value.state_change.mod_app_bind.app_idx;
        mesh_example_info_store(); /* Store proper mesh example info */
      }
      break;
    default:
      break;
    }
  }
}

static esp_err_t ble_mesh_init(void)
{
  esp_err_t err = ESP_OK;

  esp_ble_mesh_register_prov_callback(example_ble_mesh_provisioning_cb);
  esp_ble_mesh_register_generic_client_callback(example_ble_mesh_generic_client_cb);
  esp_ble_mesh_register_config_server_callback(example_ble_mesh_config_server_cb);

  err = esp_ble_mesh_init(&provision, &composition);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to initialize mesh stack (err %d)", err);
    return err;
  }

  err = esp_ble_mesh_node_prov_enable((esp_ble_mesh_prov_bearer_t)(ESP_BLE_MESH_PROV_ADV | ESP_BLE_MESH_PROV_GATT));
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Failed to enable mesh node (err %d)", err);
    return err;
  }

  ESP_LOGI(TAG, "BLE Mesh Node initialized");

  board_led_operation(LED_G, LED_ON);

  return err;
}

//#define NUM_LEDS 512
#define NUM_LEDS 256
#define DATA_PIN_1 14
#define BRIGHTNESS 15
#define LED_TYPE NEOPIXEL

CRGB leds1[NUM_LEDS];

extern "C"
{
  void app_main();
}

int mode = 1;

void blinkLeds_simple(void *pvParameters)
{

  while (1)
  {
    printf("fill_rainbow\n");

    if (is_clicked)
    {
      for (int i = 0; i < NUM_LEDS; i++)
      {
        leds1[i] = CRGB::Red;
      }
      is_clicked = false;
    }
    else
    {
      // fill_rainbow(leds1, NUM_LEDS, esp_timer_get_time() / 100);
      // uint16_t beatA = beatsin16(30, 0, 255);
      // uint16_t beatB = beatsin16(20, 0, 255);
      // fill_rainbow(leds1, NUM_LEDS, (beatA + beatB) / 2, 8);
      if (mode == 1)
      {
        fill_rainbow(leds1, NUM_LEDS, 99);
        mode = 2;
      }
      else if (mode == 2)
      {
        fill_rainbow(leds1, NUM_LEDS, 199);
        mode = 1;
      }
    }

    FastLED.show();
    delay(1000);
  }
};

void ble_mesh_get_dev_uuid(uint8_t *dev_uuid)
{
  if (dev_uuid == NULL)
  {
    ESP_LOGE(TAG, "%s, Invalid device uuid", __func__);
    return;
  }

  /* Copy device address to the device uuid with offset equals to 2 here.
     * The first two bytes is used for matching device uuid by Provisioner.
     * And using device address here is to avoid using the same device uuid
     * by different unprovisioned devices.
     */
  memcpy(dev_uuid + 2, esp_bt_dev_get_address(), BD_ADDR_LEN);
}

esp_err_t bluetooth_init(void)
{
  esp_err_t ret;

  ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  ret = esp_bt_controller_init(&bt_cfg);
  if (ret)
  {
    ESP_LOGE(TAG, "%s initialize controller failed", __func__);
    return ret;
  }

  ret = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (ret)
  {
    ESP_LOGE(TAG, "%s enable controller failed", __func__);
    return ret;
  }
  ret = esp_bluedroid_init();
  if (ret)
  {
    ESP_LOGE(TAG, "%s init bluetooth failed", __func__);
    return ret;
  }
  ret = esp_bluedroid_enable();
  if (ret)
  {
    ESP_LOGE(TAG, "%s enable bluetooth failed", __func__);
    return ret;
  }

  return ret;
}

#define NVS_NAME "mesh_example"

esp_err_t ble_mesh_nvs_open(nvs_handle_t *handle)
{
  esp_err_t err = ESP_OK;

  if (handle == NULL)
  {
    ESP_LOGE(TAG, "Open, invalid nvs handle");
    return ESP_ERR_INVALID_ARG;
  }

  err = nvs_open(NVS_NAME, NVS_READWRITE, handle);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Open, nvs_open failed, err %d", err);
    return err;
  }

  ESP_LOGI(TAG, "Open namespace done, name \"%s\"", NVS_NAME);
  return err;
}

esp_err_t ble_mesh_nvs_store(nvs_handle_t handle, const char *key, const void *data, size_t length)
{
  esp_err_t err = ESP_OK;

  if (key == NULL || data == NULL || length == 0)
  {
    ESP_LOGE(TAG, "Store, invalid parameter");
    return ESP_ERR_INVALID_ARG;
  }

  err = nvs_set_blob(handle, key, data, length);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Store, nvs_set_blob failed, err %d", err);
    return err;
  }

  err = nvs_commit(handle);
  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Store, nvs_commit failed, err %d", err);
    return err;
  }

  ESP_LOGI(TAG, "Store, key \"%s\", length %u", key, length);
  ESP_LOG_BUFFER_HEX("EXAMPLE_NVS: Store, data", data, length);
  return err;
}

esp_err_t ble_mesh_nvs_restore(nvs_handle_t handle, const char *key, void *data, size_t length, bool *exist)
{
  esp_err_t err = ESP_OK;

  if (key == NULL || data == NULL || length == 0)
  {
    ESP_LOGE(TAG, "Restore, invalid parameter");
    return ESP_ERR_INVALID_ARG;
  }

  err = nvs_get_blob(handle, key, data, &length);
  if (err == ESP_ERR_NVS_NOT_FOUND)
  {
    ESP_LOGI(TAG, "Restore, key \"%s\" not exists", key);
    if (exist)
    {
      *exist = false;
    }
    return ESP_OK;
  }

  if (exist)
  {
    *exist = true;
  }

  if (err != ESP_OK)
  {
    ESP_LOGE(TAG, "Restore, nvs_get_blob failed, err %d", err);
  }
  else
  {
    ESP_LOGI(TAG, "Restore, key \"%s\", length %u", key, length);
    ESP_LOG_BUFFER_HEX("EXAMPLE_NVS: Restore, data", data, length);
  }

  return err;
}

void app_main()
{
  printf(" entering app main, call add leds\n");
  FastLED.addLeds<LED_TYPE, DATA_PIN_1>(leds1, NUM_LEDS);

  // printf(" set max power\n");
  // FastLED.setMaxPowerInVoltsAndMilliamps(12, 2000);

  FastLED.setBrightness(BRIGHTNESS);

  printf("create task for led blinking\n");
  xTaskCreatePinnedToCore(&blinkLeds_simple, "blinkLeds", 4000, NULL, 5, NULL, 0);

  esp_err_t err;

  ESP_LOGI(TAG, "Initializing...");

  board_init();

  err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES)
  {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  err = bluetooth_init();
  if (err)
  {
    ESP_LOGE(TAG, "esp32_bluetooth_init failed (err %d)", err);
    return;
  }

  /* Open nvs namespace for storing/restoring mesh example info */
  err = ble_mesh_nvs_open(&NVS_HANDLE);
  if (err)
  {
    return;
  }

  ble_mesh_get_dev_uuid(dev_uuid);

  /* Initialize the Bluetooth Mesh Subsystem */
  err = ble_mesh_init();
  if (err)
  {
    ESP_LOGE(TAG, "Bluetooth mesh init failed (err %d)", err);
  }
}
