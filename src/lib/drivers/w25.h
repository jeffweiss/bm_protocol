#pragma once

#include "abstract_spi.h"
#include "FreeRTOS.h"
#include "semphr.h"

namespace spiflash {

class W25 : public AbstractSPI {
public:
    W25(SPIInterface_t *interface, IOPinHandle_t *csPin);
    bool eraseChip(uint32_t timeoutMs=100);
    bool read(uint32_t addr, uint8_t *buffer, size_t len, uint32_t timeoutMs=100);
    bool write(uint32_t addr, uint8_t *buffer, size_t len, uint32_t timeoutMs=100);
    bool eraseSector(uint32_t addr, uint32_t timeoutMs=100);
    bool crc32Checksum(uint32_t addr, size_t len, uint32_t &crc32, uint32_t timeoutMs=100);
private:
    bool readyToWrite(uint32_t timeoutMs);
    bool readStatus(uint8_t &status);
    bool checkWEL(uint32_t timeoutMs, bool set, bool feedWDT=false);
    bool _read(uint32_t addr, uint8_t *buffer, size_t len);
    bool _write(uint32_t addr, uint8_t *buffer, size_t len);
    bool _eraseSector(uint32_t addr);
private:
    SemaphoreHandle_t _mutex;
};

} // namespace spiflash