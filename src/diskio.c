#include "ff.h"
#include "diskio.h"
#include "sd_task.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_sd.h"
#include <stdint.h>

#define SD_TIMEOUT 1000U

/* Results of Disk Functions */
/*
// DSTATUS ENUM REFERENCE
RES_OK      0   Successful
RES_ERROR   1   R/W Error
RES_WRPRT   2   Write Protected
RES_NOTRDY  3   Not Ready
RES_PARERR  4   Invalid Parameter
*/
// VIM: Crtl-I to jump foward as Ctrl-O is jump foward

DSTATUS disk_initialize (BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;   // only drive 0 should exist

    if (SD_Init() != HAL_OK)
        return STA_NOINIT;

    return RES_OK;
}

DSTATUS disk_status (BYTE pdrv) {
    if (pdrv != 0) return STA_NOINIT;   // only drive 0 should exist

    if(HAL_SD_GetCardState(&hsd) != HAL_SD_CARD_TRANSFER)   // card is not in transfer state
        return STA_NOINIT;  // so drive is not initialized

    return RES_OK;
}

// we wont be using this below method? i guess i didnt need to implement
DRESULT disk_read (BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv != 0) return RES_PARERR;   // asked for a drive that doesnt exist

    if (HAL_SD_ReadBlocks_DMA(&hsd, buff, sector, count) != HAL_OK)
        return RES_ERROR;

    // HAL_SD_ReadBlocks_DMA returns immediately as it is non-blocking
    // lets poll for this finishing until timeout hits
    
    uint32_t start = HAL_GetTick();
    while (HAL_SD_GetCardState(&hsd) != HAL_SD_CARD_TRANSFER)
    {
        if ((HAL_GetTick() - start) > SD_TIMEOUT)
            return RES_ERROR;
    }

    return RES_OK;
}

DRESULT disk_write (BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    if (pdrv != 0) return RES_PARERR;   // asked for a drive that doesnt exist

    if (HAL_SD_WriteBlocks_DMA(&hsd, (BYTE*) buff, sector, count) != HAL_OK)
        return RES_ERROR;

    uint32_t start = HAL_GetTick();
    while (HAL_SD_GetCardState(&hsd) != HAL_SD_CARD_TRANSFER)
    {
        if ((HAL_GetTick() - start) > SD_TIMEOUT)
            return RES_ERROR;
    }

    return RES_OK;
}

DRESULT disk_ioctl (BYTE pdrv, BYTE cmd, void* buff) {
    if (pdrv != 0) return RES_PARERR;   // asked for a drive that doesnt exist

    /* Generic command (Used by FatFs) */
    /*
    CTRL_SYNC		    0   Complete pending write process (needed at FF_FS_READONLY == 0)
    GET_SECTOR_COUNT	1	Get media size (needed at FF_USE_MKFS == 1)
    GET_SECTOR_SIZE		2	Get sector size (needed at FF_MAX_SS != FF_MIN_SS)
    GET_BLOCK_SIZE		3	Get erase block size (needed at FF_USE_MKFS == 1)
    CTRL_TRIM			4	Inform device that the data on the block of sectors is no longer used (needed at FF_USE_TRIM == 1)
    */

    switch (cmd)
    {
        case CTRL_SYNC:
            return RES_OK;

        case GET_SECTOR_COUNT:
            *(LBA_t *)buff = hsd.SdCard.LogBlockNbr;
            return RES_OK;

        case GET_SECTOR_SIZE:
            *(WORD *)buff = hsd.SdCard.LogBlockSize;
            return RES_OK;

        case GET_BLOCK_SIZE:
            *(DWORD *)buff = 1; // set to 1, unoptimized default
            return RES_OK;

        default:
            return RES_PARERR;
    }
}
