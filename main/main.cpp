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

CRGBPalette16 currentPalette;
TBlendType currentBlending;

extern CRGBPalette16 myRedWhiteBluePalette;
extern const TProgmemPalette16 IRAM_ATTR myRedWhiteBluePalette_p;

#include "palettes.h"

//#define NUM_LEDS 512
#define NUM_LEDS 60
#define DATA_PIN_1 14
#define DATA_PIN_2 18
#define BRIGHTNESS 80
#define LED_TYPE WS2811
#define COLOR_ORDER RGB

CRGB leds1[NUM_LEDS];
CRGB leds2[NUM_LEDS];

#define N_COLORS 17
static const CRGB colors[N_COLORS] = {
    CRGB::Red,
    CRGB::Green,
    CRGB::Blue,
    CRGB::White,
    CRGB::AliceBlue,
    CRGB::ForestGreen,
    CRGB::Lavender,
    CRGB::MistyRose,
    CRGB::DarkOrchid,
    CRGB::DarkOrange,
    CRGB::Black,
    CRGB::Teal,
    CRGB::Violet,
    CRGB::Lime,
    CRGB::Chartreuse,
    CRGB::BlueViolet,
    CRGB::Aqua};

static const char *colors_names[N_COLORS]{
    "Red",
    "Green",
    "Blue",
    "White",
    "aliceblue",
    "ForestGreen",
    "Lavender",
    "MistyRose",
    "DarkOrchid",
    "DarkOrange",
    "Black",
    "Teal",
    "Violet",
    "Lime",
    "Chartreuse",
    "BlueViolet",
    "Aqua"};

extern "C"
{
  void app_main();
}

/* test using the FX unit
**
*/

static void blinkWithFx_allpatterns(void *pvParameters)
{

  uint16_t mode = FX_MODE_STATIC;

  WS2812FX ws2812fx;

  ws2812fx.init(NUM_LEDS, leds1, false); // type was configured before
  ws2812fx.setBrightness(255);
  ws2812fx.setMode(0 /*segid*/, mode);

  // microseconds
  uint64_t mode_change_time = esp_timer_get_time();

  while (true)
  {

    if ((mode_change_time + 10000000L) < esp_timer_get_time())
    {
      mode += 1;
      mode %= MODE_COUNT;
      mode_change_time = esp_timer_get_time();
      ws2812fx.setMode(0 /*segid*/, mode);
      printf(" changed mode to %d\n", mode);
    }

    ws2812fx.service();
    vTaskDelay(10 / portTICK_PERIOD_MS); /*10ms*/
  }
};

/* test specific patterns so we know FX is working right
**
*/

typedef struct
{
  const char *name;
  int mode;
  int secs; // secs to test it
  uint32_t color;
  int speed;
} testModes_t;

static const testModes_t testModes[] = {
    {"color wipe: all leds after each other up. Then off. Repeat. RED", FX_MODE_COLOR_WIPE, 5, 0xFF0000, 1000},
    {"color wipe: all leds after each other up. Then off. Repeat. RGREE", FX_MODE_COLOR_WIPE, 5, 0x00FF00, 1000},
    {"color wipe: all leds after each other up. Then off. Repeat. Blu", FX_MODE_COLOR_WIPE, 5, 0x0000FF, 1000},
    {"chase rainbow: Color running on white.", FX_MODE_CHASE_RAINBOW, 10, 0xffffff, 200},
    {"breath, on white.", FX_MODE_BREATH, 5, 0xffffff, 100},
    {"breath, on red.", FX_MODE_BREATH, 5, 0xff0000, 100},
    {"what is twinkefox? on red?", FX_MODE_TWINKLEFOX, 20, 0xff0000, 2000},
};

#define TEST_MODES_N (sizeof(testModes) / sizeof(testModes_t))

static void blinkWithFx_test(void *pvParameters)
{

  WS2812FX ws2812fx;
  WS2812FX::Segment *segments = ws2812fx.getSegments();

  ws2812fx.init(NUM_LEDS, leds1, false); // type was configured before
  ws2812fx.setBrightness(255);

  int test_id = 0;
  printf(" start mode: %s\n", testModes[test_id].name);
  ws2812fx.setMode(0 /*segid*/, testModes[test_id].mode);
  segments[0].colors[0] = testModes[test_id].color;
  segments[0].speed = testModes[test_id].speed;
  uint64_t nextMode = esp_timer_get_time() + (testModes[test_id].secs * 1000000L);

  while (true)
  {

    uint64_t now = esp_timer_get_time();

    if (nextMode < now)
    {
      test_id = (test_id + 1) % TEST_MODES_N;
      nextMode = esp_timer_get_time() + (testModes[test_id].secs * 1000000L);
      ws2812fx.setMode(0 /*segid*/, testModes[test_id].mode);
      segments[0].colors[0] = testModes[test_id].color;
      segments[0].speed = testModes[test_id].speed;
      printf(" changed mode to: %s\n", testModes[test_id].name);
    }

    ws2812fx.service();
    vTaskDelay(10 / portTICK_PERIOD_MS); /*10ms*/
  }
};

  /*
** chase sequences are good for testing correctness, because you can see
** that the colors are correct, and you can see cases where the wrong pixel is lit.
*/

#define CHASE_DELAY 200

void blinkLeds_chase2(void *pvParameters)
{

  while (true)
  {

    for (int ci = 0; ci < N_COLORS; ci++)
    {
      CRGB color = colors[ci];
      printf(" chase: *** color %s ***\n", colors_names[ci]);

      // set strings to black first
      fill_solid(leds1, NUM_LEDS, CRGB::Black);
      fill_solid(leds2, NUM_LEDS, CRGB::Black);
      FastLED.show();

      int prev;

      // forward
      printf(" chase: forward\n");
      prev = -1;
      for (int i = 0; i < NUM_LEDS; i++)
      {
        if (prev >= 0)
        {
          leds2[prev] = leds1[prev] = CRGB::Black;
        }
        leds2[i] = leds1[i] = color;
        prev = i;

        FastLED.show();
        delay(CHASE_DELAY);
      }

      printf(" chase: backward\n");
      prev = -1;
      for (int i = NUM_LEDS - 1; i >= 0; i--)
      {
        if (prev >= 0)
        {
          leds2[prev] = leds1[prev] = CRGB::Black;
        }
        leds2[i] = leds1[i] = color;
        prev = i;

        FastLED.show();
        delay(CHASE_DELAY);
      }

      // two at a time
      printf(" chase: twofer\n");
      prev = -1;
      for (int i = 0; i < NUM_LEDS; i += 2)
      {
        if (prev >= 0)
        {
          leds2[prev] = leds1[prev] = CRGB::Black;
          leds2[prev + 1] = leds1[prev + 1] = CRGB::Black;
        }
        leds2[i] = leds1[i] = color;
        leds2[i + 1] = leds1[i + 1] = color;
        prev = i;

        FastLED.show();
        delay(CHASE_DELAY);
      }

    } // for all colors
  }   // while true
}

void ChangePalettePeriodically()
{

  uint8_t secondHand = (millis() / 1000) % 60;
  static uint8_t lastSecond = 99;

  if (lastSecond != secondHand)
  {
    lastSecond = secondHand;
    if (secondHand == 0)
    {
      currentPalette = RainbowColors_p;
      currentBlending = LINEARBLEND;
    }
    if (secondHand == 10)
    {
      currentPalette = RainbowStripeColors_p;
      currentBlending = NOBLEND;
    }
    if (secondHand == 15)
    {
      currentPalette = RainbowStripeColors_p;
      currentBlending = LINEARBLEND;
    }
    if (secondHand == 20)
    {
      SetupPurpleAndGreenPalette();
      currentBlending = LINEARBLEND;
    }
    if (secondHand == 25)
    {
      SetupTotallyRandomPalette();
      currentBlending = LINEARBLEND;
    }
    if (secondHand == 30)
    {
      SetupBlackAndWhiteStripedPalette();
      currentBlending = NOBLEND;
    }
    if (secondHand == 35)
    {
      SetupBlackAndWhiteStripedPalette();
      currentBlending = LINEARBLEND;
    }
    if (secondHand == 40)
    {
      currentPalette = CloudColors_p;
      currentBlending = LINEARBLEND;
    }
    if (secondHand == 45)
    {
      currentPalette = PartyColors_p;
      currentBlending = LINEARBLEND;
    }
    if (secondHand == 50)
    {
      currentPalette = myRedWhiteBluePalette_p;
      currentBlending = NOBLEND;
    }
    if (secondHand == 55)
    {
      currentPalette = myRedWhiteBluePalette_p;
      currentBlending = LINEARBLEND;
    }
  }
}

void blinkLeds_interesting(void *pvParameters)
{
  while (1)
  {
    printf("blink leds\n");
    ChangePalettePeriodically();

    static uint8_t startIndex = 0;
    startIndex = startIndex + 1; /* motion speed */

    for (int i = 0; i < NUM_LEDS; i++)
    {
      leds1[i] = ColorFromPalette(currentPalette, startIndex, 64, currentBlending);
      leds2[i] = ColorFromPalette(currentPalette, startIndex, 64, currentBlending);
      startIndex += 3;
    }
    printf("show leds\n");
    FastLED.show();
    delay(400);
  };
};

// Going to use the ESP timer system to attempt to get a frame rate.
// According to the documentation, this is a fairly high priority,
// and one should attempt to do minimal work - such as dispatching a message to a queue.
// at first, let's try just blasting pixels on it.

// Target frames per second
#define FASTFADE_FPS 30

typedef struct
{
  CHSV color;
} fastfade_t;

static void _fastfade_cb(void *param)
{

  fastfade_t *ff = (fastfade_t *)param;

  ff->color.hue++;

  if (ff->color.hue % 10 == 0)
  {
    printf("fast hsv fade h: %d s: %d v: %d\n", ff->color.hue, ff->color.s, ff->color.v);
  }

  fill_solid(leds1, NUM_LEDS, ff->color);
  fill_solid(leds2, NUM_LEDS, ff->color);

  FastLED.show();
};

static void fastfade(void *pvParameters)
{

  fastfade_t ff_t = {
      .color = CHSV(0 /*hue*/, 255 /*sat*/, 255 /*value*/)};

  esp_timer_create_args_t timer_create_args = {
      .callback = _fastfade_cb,
      .arg = (void *)&ff_t,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "fastfade_timer"};

  esp_timer_handle_t timer_h;

  esp_timer_create(&timer_create_args, &timer_h);

  esp_timer_start_periodic(timer_h, 1000000L / FASTFADE_FPS);

  // suck- just trying this
  while (1)
  {

    vTaskDelay(1000 / portTICK_PERIOD_MS);
  };
}

void blinkLeds_simple(void *pvParameters)
{

  while (1)
  {

    for (int j = 0; j < N_COLORS; j++)
    {
      printf("blink leds\n");

      for (int i = 0; i < NUM_LEDS; i++)
      {
        leds1[i] = colors[j];
        leds2[i] = colors[j];
      }
      FastLED.show();
      delay(1000);
    };
  }
};

#define N_COLORS_CHASE 7
CRGB colors_chase[N_COLORS_CHASE] = {
    CRGB::AliceBlue,
    CRGB::Lavender,
    CRGB::DarkOrange,
    CRGB::Red,
    CRGB::Green,
    CRGB::Blue,
    CRGB::White,
};

void blinkLeds_chase(void *pvParameters)
{
  int pos = 0;
  int led_color = 0;
  while (1)
  {
    printf("chase leds\n");

    // do it the dumb way - blank the leds
    for (int i = 0; i < NUM_LEDS; i++)
    {
      leds1[i] = CRGB::Black;
      leds2[i] = CRGB::Black;
    }

    // set the one LED to the right color
    leds1[pos] = leds2[pos] = colors_chase[led_color];
    pos = (pos + 1) % NUM_LEDS;

    // use a new color
    if (pos == 0)
    {
      led_color = (led_color + 1) % N_COLORS_CHASE;
    }

    uint64_t start = esp_timer_get_time();
    FastLED.show();
    uint64_t end = esp_timer_get_time();
    printf("Show Time: %" PRIu64 "\n", end - start);
    delay(200);
  };
}

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
  // the WS2811 family uses the RMT driver
  FastLED.addLeds<LED_TYPE, DATA_PIN_1>(leds1, NUM_LEDS);
  FastLED.addLeds<LED_TYPE, DATA_PIN_2>(leds2, NUM_LEDS);

  // this is a good test because it uses the GPIO ports, these are 4 wire not 3 wire
  //FastLED.addLeds<APA102, 13, 15>(leds, NUM_LEDS);

  printf(" set max power\n");
  // I have a 2A power supply, although it's 12v
  FastLED.setMaxPowerInVoltsAndMilliamps(12, 2000);

  // change the task below to one of the functions above to try different patterns
  printf("create task for led blinking\n");

  //xTaskCreatePinnedToCore(&blinkLeds_simple, "blinkLeds", 4000, NULL, 5, NULL, 0);
  //xTaskCreatePinnedToCore(&fastfade, "blinkLeds", 4000, NULL, 5, NULL, 0);
  //xTaskCreatePinnedToCore(&blinkWithFx_allpatterns, "blinkLeds", 4000, NULL, 5, NULL, 0);
  xTaskCreatePinnedToCore(&blinkWithFx_test, "blinkLeds", 4000, NULL, 5, NULL, 0);
  //xTaskCreatePinnedToCore(&blinkLeds_chase, "blinkLeds", 4000, NULL, 5, NULL, 0);
  //xTaskCreatePinnedToCore(&blinkLeds_chase2, "blinkLeds", 4000, NULL, 5, NULL, 0);

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
