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
std::atomic<uint64_t> sleepDurationInUs { 0 };
std::atomic<uint32_t> sleepCount { 0 };

esp_pm_lock_handle_t noSleep;

bool timerRunning = false;
esp_timer_handle_t timer = nullptr;

static const gpio_num_t rgbLedGpio = GPIO_NUM_8;
static const gpio_num_t ledGpio = GPIO_NUM_15;
static const gpio_num_t buttonGpio = GPIO_NUM_9;

// static tNeopixelContext neopixel = neopixel_Init(1, rgbLedGpio);
// static tNeopixel off = { 0, NP_RGB(0, 0, 0) };
// static tNeopixel red = { 0, NP_RGB(50, 0, 0) };
// static tNeopixel green = { 0, NP_RGB(0, 50, 0) };
// static tNeopixel blue = { 0, NP_RGB(0, 0, 50) };
// static tNeopixel white = { 0, NP_RGB(17, 17, 17) };

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
    // neopixel_SetPixel(neopixel, &red, 1);
    // neopixel_SetPixel(neopixel, &green, 1);
    // neopixel_SetPixel(neopixel, &blue, 1);
    // neopixel_SetPixel(neopixel, &white, 1);

    // Set up GPIO for the LED
    gpio_config_t ledConfig = {
        .pin_bit_mask = 1ULL << ledGpio,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&ledConfig);
    gpio_set_direction(ledGpio, GPIO_MODE_OUTPUT);
    gpio_sleep_set_direction(ledGpio, GPIO_MODE_OUTPUT);

    esp_pm_sleep_cbs_register_config_t cbs_conf = {
        .enter_cb = [](int64_t timeToSleepInUs, void* arg) {
            gpio_set_level(ledGpio, 1);
            return ESP_OK; },
        .exit_cb = [](int64_t timeSleptInUs, void* arg) {
            gpio_set_level(ledGpio, 0);
            sleepDurationInUs += timeSleptInUs;
            sleepCount++;
            return ESP_OK; },
    };
    ESP_ERROR_CHECK(esp_pm_light_sleep_register_cbs(&cbs_conf));

    esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_XTAL_FREQ,
        .light_sleep_enable = true,
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

    esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "no-sleep", &noSleep);

    esp_timer_create_args_t timer_args = {
        .callback = &timerCallback,
        .name = "end-sleep"
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));

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

    auto startTime = high_resolution_clock::now();
    while (true) {
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        auto endTime = high_resolution_clock::now();
        auto delayDurationInUs = duration_cast<microseconds>(endTime - startTime).count();
        ESP_LOGI("main", "Counter: %lu; awake %.3f%% (%lu cycles)",
            counter.exchange(0),
            (1.0 - ((double) sleepDurationInUs.exchange(0)) / ((double) delayDurationInUs)) * 100.0,
            sleepCount.exchange(0));
        startTime = endTime;
    }
}
