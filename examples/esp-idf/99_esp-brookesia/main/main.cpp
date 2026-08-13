/*
 * SPDX-FileCopyrightText: 2023-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include "bsp/esp-bsp.h"
#include "esp_brookesia.hpp"
#include "boost/thread.hpp"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "wifi_config.h"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Main"
#include "esp_lib_utils.h"

using namespace esp_brookesia;
using namespace esp_brookesia::gui;
using namespace esp_brookesia::systems::phone;

constexpr bool EXAMPLE_SHOW_MEM_INFO = false;
constexpr uint32_t LVGL_TASK_STACK_SIZE = 40 * 1024;

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t *)data;
        ESP_UTILS_LOGW("WiFi disconnected, reason: %d", disconn->reason);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *got_ip = (ip_event_got_ip_t *)data;
        ESP_UTILS_LOGI("WiFi got IP: " IPSTR, IP2STR(&got_ip->ip_info.ip));
    }
}

static void wifi_connect_sta(void)
{
    esp_netif_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password) - 1);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* Auto-connect does not reliably kick in when the config is set at
     * runtime (nothing else logged a connect attempt) — connect explicitly. */
    ESP_ERROR_CHECK(esp_wifi_connect());

    ESP_UTILS_LOGI("Connecting to WiFi: %s", WIFI_SSID);

    // Blocking wait for IPv4 (MVP; provisioning comes in a later ticket)
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info = {};
    for (int retry = 0; retry < 20; retry++) {
        esp_netif_get_ip_info(sta_netif, &ip_info);
        if (ip_info.ip.addr != 0) {
            ESP_UTILS_LOGI("WiFi connected, IP: " IPSTR, IP2STR(&ip_info.ip));
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    ESP_UTILS_LOGW("WiFi connect timeout (SSID/password not configured?); pet app will stay disconnected");
}

extern "C" void app_main(void)
{
    ESP_UTILS_LOGI("Display ESP-Brookesia phone demo");

    /* WiFi STA for the PC Bridge link (ticket 02). Connect first so the
     * pet app can open its WebSocket as soon as it runs. */
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    wifi_connect_sta();

    /* Brookesia screen creation exceeds the adapter's 8 KB default on this board. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
    esp_lv_adapter_config_t lv_adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
#pragma GCC diagnostic pop
    lv_adapter_config.task_stack_size = LVGL_TASK_STACK_SIZE;
    lv_adapter_config.stack_in_psram = true;

    bsp_display_cfg_t display_config = {};
    display_config.lv_adapter_cfg = lv_adapter_config;
    display_config.rotation = ESP_LV_ADAPTER_ROTATE_0;
    display_config.tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE;
    display_config.touch_flags.mirror_x = 1;
    display_config.touch_flags.mirror_y = 1;

    ESP_UTILS_CHECK_NULL_EXIT(
        bsp_display_start_with_config(&display_config), "Start display failed"
    );
    ESP_UTILS_CHECK_ERROR_EXIT(bsp_display_backlight_on(), "Turn on display backlight failed");

    /* Configure GUI lock */
    LvLock::registerCallbacks([](int timeout_ms) {
        esp_err_t ret = bsp_display_lock(timeout_ms);
        ESP_UTILS_CHECK_FALSE_RETURN(ret == ESP_OK, false, "Lock failed (timeout_ms: %d)", timeout_ms);

        return true;
    }, []() {
        bsp_display_unlock();
        return true;
    });

    /* Create a phone object */
    Phone *phone = new (std::nothrow) Phone();
    ESP_UTILS_CHECK_NULL_EXIT(phone, "Create phone failed");

    {
        // When operating on non-GUI tasks, should acquire a lock before operating on LVGL
        LvLockGuard gui_guard;

        /* Begin the phone */
        ESP_UTILS_CHECK_FALSE_EXIT(phone->begin(), "Begin failed");
        // assert(phone->getDisplay().showContainerBorder() && "Show container border failed");

        /* Init and install apps from registry */
        std::vector<systems::base::Manager::RegistryAppInfo> inited_apps;
        ESP_UTILS_CHECK_FALSE_EXIT(phone->initAppFromRegistry(inited_apps), "Init app registry failed");
        ESP_UTILS_CHECK_FALSE_EXIT(phone->installAppFromRegistry(inited_apps), "Install app registry failed");

        /* Create a timer to update the clock */
        lv_timer_create([](lv_timer_t *t) {
            time_t now;
            struct tm timeinfo;
            Phone *phone = (Phone *)t->user_data;

            ESP_UTILS_CHECK_NULL_EXIT(phone, "Invalid phone");

            time(&now);
            localtime_r(&now, &timeinfo);

            ESP_UTILS_CHECK_FALSE_EXIT(
                phone->getDisplay().getStatusBar()->setClock(timeinfo.tm_hour, timeinfo.tm_min),
                "Refresh status bar failed"
            );
        }, 1000, phone);
    }

    if constexpr (EXAMPLE_SHOW_MEM_INFO) {
        esp_utils::thread_config_guard thread_config({
            .name = "mem_info",
            .stack_size = 4096,
        });
        boost::thread([ = ]() {
            char buffer[128];    /* Make sure buffer is enough for `sprintf` */
            size_t internal_free = 0;
            size_t internal_total = 0;
            size_t external_free = 0;
            size_t external_total = 0;

            while (1) {
                internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
                internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
                external_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
                external_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
                sprintf(buffer,
                        "\t           Biggest /     Free /    Total\n"
                        "\t  SRAM : [%8d / %8d / %8d]\n"
                        "\t PSRAM : [%8d / %8d / %8d]",
                        heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), internal_free, internal_total,
                        heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM), external_free, external_total);
                ESP_UTILS_LOGI("\n%s", buffer);

                {
                    LvLockGuard gui_guard;
                    ESP_UTILS_CHECK_FALSE_EXIT(
                        phone->getDisplay().getRecentsScreen()->setMemoryLabel(
                            internal_free / 1024, internal_total / 1024, external_free / 1024, external_total / 1024
                        ), "Set memory label failed"
                    );
                }

                boost::this_thread::sleep_for(boost::chrono::seconds(5));
            }
        }).detach();
    }
}