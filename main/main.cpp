#include <stdio.h>

#include <atomic>
#include <chrono>

#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_attr.h>
#include <esp_log.h>
#include <esp_pm.h>
#include <esp_sleep.h>
#include <esp_timer.h>

#include <freertos/FreeRTOS.h>

#include <neopixel.h>

using namespace std::chrono;

bool previousState = false;
std::atomic<uint32_t> counter { 0 };
std::atomic<int64_t> sleepDuration { 0 };

esp_pm_lock_handle_t noSleep;

bool timerRunning = false;
esp_timer_handle_t timer = nullptr;

static const gpio_num_t ledGpio = GPIO_NUM_8;
static const gpio_num_t buttonGpio = GPIO_NUM_9;

void timerCallback(void* arg) {
    ESP_LOGI("main", "Timer expired, allowing light sleep");
    ESP_ERROR_CHECK(esp_pm_lock_release(noSleep));
    previousState = false;
    timerRunning = false;
}

void interruptHandler(void* arg) {
    bool currentState = rtc_gpio_get_level(buttonGpio);
    if (currentState) {
        if (!previousState) {
            ESP_ERROR_CHECK(esp_pm_lock_acquire(noSleep));
            if (timerRunning) {
                ESP_ERROR_CHECK(esp_timer_stop(timer));
            }
            esp_timer_start_once(timer, duration_cast<microseconds>(200ms).count());
            timerRunning = true;
        }
    } else {
        if (previousState) {
            counter++;
            ESP_ERROR_CHECK(esp_pm_lock_release(noSleep));
            esp_timer_stop(timer);
            timerRunning = false;
        }
    }
    previousState = currentState;
}

extern "C" void app_main() {
    tNeopixelContext neopixel = neopixel_Init(1, ledGpio);
    tNeopixel pixel = { 0, NP_RGB(50, 0, 0) };
    neopixel_SetPixel(neopixel, &pixel, 1);

    // esp_pm_sleep_cbs_register_config_t cbs_conf = {
    //     .enter_cb = [](int64_t timeToSleepInUs, void* arg) {
    //         return ESP_OK; },
    //     .exit_cb = [](int64_t timeSleptInUs, void* arg) {
    //         sleepDuration += timeSleptInUs;
    //         return ESP_OK; },
    // };
    // ESP_ERROR_CHECK(esp_pm_light_sleep_register_cbs(&cbs_conf));

    // esp_pm_config_t pm_config = {
    //     .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
    //     .min_freq_mhz = CONFIG_XTAL_FREQ,
    //     .light_sleep_enable = true,
    // };
    // ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    // esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "no-sleep", &noSleep);

    // esp_timer_create_args_t timer_args = {
    //     .callback = &timerCallback,
    //     .name = "end-sleep"
    // };
    // ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));

    // gpio_install_isr_service(0);

    // gpio_config_t config = {
    //     .pin_bit_mask = 1ULL << buttonGpio,
    //     .mode = GPIO_MODE_INPUT,
    //     .pull_up_en = GPIO_PULLUP_DISABLE,
    //     .pull_down_en = GPIO_PULLDOWN_ENABLE,
    //     .intr_type = GPIO_INTR_ANYEDGE,
    // };
    // gpio_config(&config);

    // gpio_sleep_sel_dis(buttonGpio);

    // rtc_gpio_wakeup_enable(buttonGpio, GPIO_INTR_HIGH_LEVEL);
    // esp_sleep_enable_gpio_wakeup();

    // gpio_isr_handler_add(buttonGpio, interruptHandler, nullptr);

    while (true) {
        printf("Counter: %lu; slept %lld us\n",
            counter.exchange(0),
            sleepDuration.exchange(0));
        fflush(stdout);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
