#include "ncp_dfu.h"
#include "external_flash_partitions.h"
#include <stdio.h>
#include "bm_dfu.h"
#include "bm_serial.h"
#include "device_info.h"
#include "bootutil/bootutil_public.h"
#include "bootutil/image.h"
#include "flash_map_backend/flash_map_backend.h"
#include "stm32_flash.h"
#include "sysflash/sysflash.h"
#include "util.h"
#include "reset_reason.h"
#include "FreeRTOS.h"

typedef struct  __attribute__((__packed__)) {
  uint32_t magic;
  uint32_t gitSHA;
} NcpReboootUpdateInfo_t;

#define IMG_CRC_TIMEOUT_MS          (30 * 1000)
#define NODE_UPDATE_TIMEOUT_MS      (60 * 1000)
#define FLASH_WRITE_READ_TIMEOUT_MS (3 * 1000)
#define REBOOT_DELAY_MS             (1 * 1000)
#define COPY_BUFFER_SIZE            (1024)
#define REBOOT_MAGIC                (0xbaadc0de)
#define POWER_CONTROLLER_TIMEOUT_MS (5 * 1000)
#define INTERNAL_FLASH_PAGE_SIZE    (0x2000)

typedef struct {
    NvmPartition* dfu_cli_partition;
    BridgePowerController * power_controller;
    bool power_controller_was_enabled;
    const struct flash_area * fa;
} ncp_dfu_ctx_t;

static ncp_dfu_ctx_t _ctx;
static NcpReboootUpdateInfo_t _reboot_info __attribute__((section(".noinit")));

static void _ncp_dfu_finish(bool success, bm_dfu_err_t err, uint64_t node_id) {
    memset(&_reboot_info, 0, sizeof(_reboot_info));
    bm_serial_dfu_send_finish(node_id, success, err);
    if(_ctx.power_controller_was_enabled){
        _ctx.power_controller->powerControlEnable(true);
    }
}

static void _update_success_cb(bool success, bm_dfu_err_t err, uint64_t node_id) {
    _ncp_dfu_finish(success, err, node_id);
}

static bool _do_self_update(bm_serial_dfu_start_t *dfu_start) {
    static uint8_t copy_buffer[COPY_BUFFER_SIZE];
    bool rval = false;
    do {
        if (flash_area_open(FLASH_AREA_IMAGE_SECONDARY(0), &_ctx.fa) != 0) {
            printf("Failed to open internal flash\n");
            break;
        }
        uint32_t image_flash_size = dfu_start->image_size;
        image_flash_size += (INTERNAL_FLASH_PAGE_SIZE - 1);
        image_flash_size &= ~(INTERNAL_FLASH_PAGE_SIZE - 1);
        if(flash_area_erase(_ctx.fa, 0, image_flash_size) != 0) {
            printf("Failed to erase flash.\n");
            break;
        }
        // COPY DFU partition to internal flash
        uint32_t offset = 0;
        uint32_t bytes_remaining = dfu_start->image_size;
        while(bytes_remaining) {
            size_t chunk_size = MIN(bytes_remaining, COPY_BUFFER_SIZE); 
            if(!_ctx.dfu_cli_partition->read(offset + DFU_IMG_START_OFFSET_BYTES, copy_buffer,chunk_size, FLASH_WRITE_READ_TIMEOUT_MS)){
                printf("Failed to read dfu partition\n");
                break;
            }
            if(flash_area_write(_ctx.fa, offset, copy_buffer, chunk_size) != 0){
                printf("Failed to flash area write\n");
                break;
            }
            offset += chunk_size;
            bytes_remaining -= chunk_size;
        }
        flash_area_close(_ctx.fa);
        if(!bytes_remaining) {
            _reboot_info.gitSHA = dfu_start->gitSHA;
            _reboot_info.magic = REBOOT_MAGIC;
            boot_set_pending(0);
            resetSystem(RESET_REASON_MCUBOOT);
        }
    } while(0);
    return rval;
}

static bool _do_bcmp_update(bm_serial_dfu_start_t *dfu_start) {
    bool rval = false; 
    do {
        bm_dfu_img_info_t info = {
            .image_size = dfu_start->image_size,
            .chunk_size = dfu_start->chunk_size,
            .crc16 = dfu_start->crc16,
            .major_ver = dfu_start->major_ver,
            .minor_ver = dfu_start->minor_ver,
            .filter_key = dfu_start->filter_key,
            .gitSHA = dfu_start->gitSHA,
        };
        rval = bm_dfu_initiate_update(info, dfu_start->node_id, _update_success_cb, NODE_UPDATE_TIMEOUT_MS);
    } while(0);
    return rval;
}

bool ncp_dfu_start_cb(bm_serial_dfu_start_t *dfu_start) {
    bool rval = false;
    do {
        uint16_t computed_crc16;
        if(!_ctx.dfu_cli_partition->crc16(DFU_IMG_START_OFFSET_BYTES, dfu_start->image_size, computed_crc16, IMG_CRC_TIMEOUT_MS)){
            break;
        }
        if(computed_crc16 != dfu_start->crc16) {
            break;
        }
        if(getNodeId() == dfu_start->node_id){
            rval = _do_self_update(dfu_start);
        } else {
            // Don't allow firmware update until the two minute initilzation period of the bridge has elapsed.
            if(!_ctx.power_controller->initPeriodElapsed()) {
                break;
            }
            // We want to disable power control here (which will leave the bus ON, allowing a node to be updated)
            if(_ctx.power_controller->isPowerControlEnabled()){ // If we were already disabled, we don't need to do this.
                _ctx.power_controller->powerControlEnable(false);
                _ctx.power_controller_was_enabled = true;
            } else {
                _ctx.power_controller_was_enabled = false;
            }
            // Wait for the power control to set the bus ON
            if(_ctx.power_controller->waitForSignal(true, pdMS_TO_TICKS(POWER_CONTROLLER_TIMEOUT_MS))){
                printf("Bus is ON! Queueing node update\n");
                rval = _do_bcmp_update(dfu_start);
            }
        }
    } while(0);
    if(!rval) {
        _ncp_dfu_finish(rval, BM_DFU_ERR_ABORTED, dfu_start->node_id);
    }
    return rval;
}
 
bool ncp_dfu_chunk_cb(uint32_t offset, size_t length, uint8_t * data) {
    bool rval = false;
    if(_ctx.dfu_cli_partition->write(DFU_IMG_START_OFFSET_BYTES + offset, data,length, FLASH_WRITE_READ_TIMEOUT_MS)){
        if (bm_serial_dfu_send_chunk(offset, 0, NULL) == BM_SERIAL_OK) {
            rval = true;
        }
    } else {
        bm_serial_dfu_send_chunk(offset | DFU_CHUNK_NAK_BITFLAG, 0, NULL);
    }
    return rval;
}

void ncp_dfu_init(NvmPartition *dfu_cli_partition, BridgePowerController *power_controller) {
    configASSERT(dfu_cli_partition);
    configASSERT(power_controller);
    _ctx.dfu_cli_partition = dfu_cli_partition;
    _ctx.power_controller = power_controller;
    _ctx.power_controller_was_enabled = false;
}

void ncp_dfu_check_for_update(void) {
    if(_reboot_info.magic == REBOOT_MAGIC){
        if(_reboot_info.gitSHA == getGitSHA()){
            boot_set_confirmed();
            _ncp_dfu_finish(true, BM_DFU_ERR_NONE, getNodeId());
        } else {
            _ncp_dfu_finish(false, BM_DFU_ERR_WRONG_VER, getNodeId());
            vTaskDelay(pdMS_TO_TICKS(REBOOT_DELAY_MS)); // Short delay before reboot revert to allow for message to host. 
            resetSystem(RESET_REASON_MCUBOOT);
        }
    }
}
