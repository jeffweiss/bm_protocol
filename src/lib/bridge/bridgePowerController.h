#pragma once

#include "FreeRTOS.h"
#include "event_groups.h"
#include "task.h"
#include "io.h"

#include <stdint.h>

class BridgePowerController {
public:
    BridgePowerController(IOPinHandle_t &BusPowerPin, uint32_t sampleIntervalMs = DEFAULT_SAMPLE_INTERVAL_MS, uint32_t sampleDurationMs = DEFAULT_SAMPLE_DURATION_MS,
        uint32_t subsampleIntervalMs = DEFAULT_SUBSAMPLE_INTERVAL_MS, uint32_t subsampleDurationMs = DEFAULT_SUBSAMPLE_DURATION_MS,
        bool subSamplingEnabled = false, bool powerControllerEnabled = false);
    void powerControlEnable(bool enable);
    bool isPowerControlEnabled();
    void subSampleEnable(bool enable);
    bool isSubsampleEnabled();
    bool waitForSignal(bool on, TickType_t ticks_to_wait);
    bool isBridgePowerOn(void);
    bool initPeriodElapsed(void);

    // Shim function for FreeRTOS compatibility, should not be called as part of the public API.
    void _update(void); // PRIVATE
private:
    void powerBusAndSetSignal(bool on);
    static void powerControllerRun(void* arg);
    bool nowTimestampMs(uint32_t &timestamp);

public:
    static constexpr uint32_t OFF = (1 << 0); 
    static constexpr uint32_t ON = (1 << 1); 
    static constexpr uint32_t DEFAULT_SAMPLE_INTERVAL_MS = (5 * 60 * 1000); 
    static constexpr uint32_t DEFAULT_SAMPLE_DURATION_MS = (5 * 60 * 1000); 
    static constexpr uint32_t DEFAULT_SUBSAMPLE_INTERVAL_MS = (60 * 1000); 
    static constexpr uint32_t DEFAULT_SUBSAMPLE_DURATION_MS = (30 * 1000); 
    static constexpr uint32_t MIN_SAMPLE_INTERVAL_MS = (60 * 1000);
    static constexpr uint32_t MIN_SAMPLE_DURATION_MS = (60 * 1000);
    static constexpr uint32_t MAX_SAMPLE_INTERVAL_MS = (24 * 60 * 60 * 1000);
    static constexpr uint32_t MAX_SAMPLE_DURATION_MS = (24 * 60 * 60 * 1000);
    static constexpr uint32_t MIN_SUBSAMPLE_INTERVAL_MS = (1000);
    static constexpr uint32_t MIN_SUBSAMPLE_DURATION_MS = (1000);
    static constexpr uint32_t MAX_SUBSAMPLE_INTERVAL_MS = (60 * 60 * 1000);
    static constexpr uint32_t MAX_SUBSAMPLE_DURATION_MS = (60 * 60 * 1000);

private:
    static constexpr uint32_t MIN_TASK_SLEEP_MS = (1 * 1000);
    static constexpr uint32_t INIT_POWER_ON_TIMEOUT_MS = (2 * 60 * 1000);
    static constexpr char bm_printf_topic[] = "bm_printf";

private:
    IOPinHandle_t &_BusPowerPin;
    bool _powerControlEnabled;
    uint32_t _sampleIntervalMs;
    uint32_t _sampleDurationMs;
    uint32_t _subsampleIntervalMs;
    uint32_t _subsampleDurationMs;
    uint32_t _nextSampleIntervalTimeTimestamp;
    uint32_t _nextSubSampleIntervalTimeTimestamp;
    bool _rtcSet;
    bool _initDone;
    bool _subSamplingEnabled;
    EventGroupHandle_t _busPowerEventGroup;
    TaskHandle_t _task_handle;
};

