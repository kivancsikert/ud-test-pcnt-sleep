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
#include <freertos/queue.h>
#include <freertos/task.h>

using namespace std::chrono;

bool previousState = false;
std::atomic<uint32_t> counter { 0 };
std::atomic<uint64_t> sleepDurationInUs { 0 };
std::atomic<uint32_t> sleepCount { 0 };

esp_pm_lock_handle_t noSleep;

bool timerRunning = false;
esp_timer_handle_t timer = nullptr;

static const gpio_num_t ledGpio = GPIO_NUM_23;
static const gpio_num_t buttonGpio = GPIO_NUM_3;

static QueueHandle_t buttonQueue = xQueueCreate(10, sizeof(bool));

void timerCallback(void* arg) {
    ESP_LOGI("main", "Timer expired, allowing light sleep");
    ESP_ERROR_CHECK(esp_pm_lock_release(noSleep));
    previousState = false;
    timerRunning = false;
}

static void IRAM_ATTR interruptHandler(void* arg) {
    bool currentState = rtc_gpio_get_level(buttonGpio);
    xQueueSendFromISR(buttonQueue, &currentState, NULL);
}

static void buttonHandlerTask(void* arg) {
    bool buttonState;
    bool previousState = true;

    while (true) {
        // Wait for a button press event
        if (xQueueReceive(buttonQueue, &buttonState, portMAX_DELAY)) {
            // Print the received button state
            ESP_LOGI("main", "Button pressed: %s", buttonState ? "true" : "false");
            rtc_gpio_wakeup_enable(buttonGpio, buttonState ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
            if (previousState != buttonState) {
                previousState = buttonState;
                if (buttonState) {
                    counter++;
                }
            }
        }
    }
}

extern "C" void app_main() {
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
            switch (esp_sleep_get_wakeup_cause()) {
                case ESP_SLEEP_WAKEUP_GPIO: {
                    interruptHandler(nullptr);
                    break;
                }
                default: {
                    break;
                }
            }
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

    gpio_install_isr_service(0);

    rtc_gpio_init(buttonGpio);
    rtc_gpio_set_direction(buttonGpio, RTC_GPIO_MODE_INPUT_ONLY);
    // rtc_gpio_pulldown_en(buttonGpio);
    esp_sleep_enable_gpio_wakeup();

    gpio_isr_handler_add(buttonGpio, interruptHandler, nullptr);

    xTaskCreate(&buttonHandlerTask, "button-handler", 2048, nullptr, 1, NULL);

    auto startTime = high_resolution_clock::now();
    while (true) {
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        auto endTime = high_resolution_clock::now();
        auto delayDurationInUs = duration_cast<microseconds>(endTime - startTime).count();
        ESP_LOGI("main", "Counter: %lu; awake %.3f%% (%lu cycles) -- button pressed: %s",
            counter.exchange(0),
            (1.0 - ((double) sleepDurationInUs.exchange(0)) / ((double) delayDurationInUs)) * 100.0,
            sleepCount.exchange(0),
            rtc_gpio_get_level(buttonGpio) ? "true" : "false");
        fflush(stdout);
        startTime = endTime;
    }
}
