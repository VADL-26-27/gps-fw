#include "Legacy/stm32_hal_legacy.h"
#include "ff.h"
#include "stm32f411xe.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_def.h"
#include "stm32f4xx_hal_dma.h"
#include "sd_task.h"
#include "FreeRTOS.h"
#include "stm32f4xx_hal_gpio.h"
#include "stm32f4xx_hal_gpio_ex.h"
#include "stm32f4xx_hal_rcc.h"
#include "stm32f4xx_hal_spi.h"
#include "task.h"
#include <stdio.h>
#include <string.h>
#include "gps_structs.h"

SPI_HandleTypeDef hspi; // spi handler
static DMA_HandleTypeDef hdma_tx;   // dma_tx handler
static DMA_HandleTypeDef hdma_rx;   // dma_rx handler

// NOTE: This is written for the 446. When in testing in lab pls refactor

static void SPI_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    // configures pins used by SPI
    // ref. pg. 57 ds10693 Table 11
    //  or ref. pg. 38 SD physical layer spec
    // SPI1 is AF5
    // SPI1_MOSI    -   PA7 - DI    (MCU, peripheral driven)
    // SPI1_MISO    -   PA6 - DO    (SD card output)(pull-up)
    // SPI1_SCK     -   PA5 - SCLK  (MCU, peripheral driven)
    // SPI1_NSS     -   PA4 - CS    (MCU, software driven)


    GPIO_InitStruct.Pin       = GPIO_PIN_7 | GPIO_PIN_6 | GPIO_PIN_5;
    // The above pins will be:
    GPIO_InitStruct.Mode        = GPIO_MODE_AF_PP;  // alternate function, push-pull
    GPIO_InitStruct.Pull        = GPIO_NOPULL;
    GPIO_InitStruct.Speed       = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate   = GPIO_AF5_SPI1;    // SPI is AF5, ref. pg. 57 ds10693 Table 11
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // ref. https://elm-chan.org/docs/mmc/mmc_e.html
    // Since DI and DO are normally high, they should both have pull-up resistors
    // Similarly SCLK should be pulled low
    GPIO_InitStruct.Pin         = GPIO_PIN_7 | GPIO_PIN_6;
    GPIO_InitStruct.Pull        = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    GPIO_InitStruct.Pin         = GPIO_PIN_5;
    GPIO_InitStruct.Pull        = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);


    // ref. pg. 286 SD physical layer spec
    // "The host starts every bus transaction by asserting the CS signal low."
    // if active on low then idle on high
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET); // this changes the ODR preimpetively so when pin inits it immediately outputs correct signal
    GPIO_InitStruct.Pin         = GPIO_PIN_4;
    GPIO_InitStruct.Mode        = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull        = GPIO_NOPULL;
    GPIO_InitStruct.Speed       = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

// this links both dma tx and dma rx. this wasnt needed from sdio implementation because 
// responses could be read from peripheral's responses registers
static HAL_StatusTypeDef SPI1_DMA_Init(void)
{
    __HAL_RCC_DMA2_CLK_ENABLE();

    hdma_tx.Instance                 = DMA2_Stream3;            // ref. pg. 204 rm0390 Table 29
    hdma_tx.Init.Channel             = DMA_CHANNEL_3;           // ref. pg. 204 rm0390 Table 29
    hdma_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    hdma_tx.Init.PeriphInc           = DMA_PINC_DISABLE;        // no increment as SP1 TX Buffer is fixed addr
    hdma_tx.Init.MemInc              = DMA_MINC_ENABLE;         // increment on memory buffer
    // ref. pg. 286 SD physical layer spec
    // "Every command or data block is built of 8-bit bytes"
    hdma_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;     // DMA_SxCR PSIZE
    hdma_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;     // DMA_SxCR MSIZE
    hdma_tx.Init.Mode                = DMA_NORMAL;              // the peripheral decides when DMA should happen
    hdma_tx.Init.Priority            = DMA_PRIORITY_LOW;        // this is low so we dont overread
    hdma_tx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;

    if(HAL_DMA_Init(&hdma_tx) != HAL_OK) return HAL_ERROR;
    __HAL_LINKDMA(&hspi, hdmatx, hdma_tx);

    hdma_rx.Instance                 = DMA2_Stream2;            // ref. pg. 204 rm0390 Table 29
    hdma_rx.Init.Channel             = DMA_CHANNEL_3;           // ref. pg. 204 rm0390 Table 29
    hdma_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_rx.Init.PeriphInc           = DMA_PINC_DISABLE;        // no increment as SP1 RX Buffer is fixed addr
    hdma_rx.Init.MemInc              = DMA_MINC_ENABLE;         // increment on memory buffer
    // ref. pg. 286 SD physical layer spec
    // "Every command or data block is built of 8-bit bytes"
    hdma_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;     // DMA_SxCR PSIZE
    hdma_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;     // DMA_SxCR MSIZE
    hdma_rx.Init.Mode                = DMA_NORMAL;              // the peripheral decides when DMA should happen
    hdma_rx.Init.Priority            = DMA_PRIORITY_HIGH;       // this is high so we read before writing 
    hdma_rx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;

    if(HAL_DMA_Init(&hdma_rx) != HAL_OK) return HAL_ERROR;
    __HAL_LINKDMA(&hspi, hdmarx, hdma_rx);

    // NVIC allows DMA rx and tx transactions to be non-blocking
    HAL_NVIC_SetPriority(DMA2_Stream3_IRQn, 1, 0);  // TODO: these priority values should be changed
    HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);
    HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);

    return HAL_OK;
}

HAL_StatusTypeDef SPI_Init()
{
    // SPI1 is on APB2
    // DMA2 is on AHB1
    __HAL_RCC_SPI1_CLK_ENABLE();    
    __HAL_RCC_DMA2_CLK_ENABLE();    // SPI1_TX is on DMA2, ref. pg. 204 rm0390 Table 29
    // These two clocks use the System Clock, which is by default the 16 MHz HSI on f446
    // SPI peripheral clock runs at 16 MHz

    SPI_GPIO_Init();
    SPI1_DMA_Init();

    hspi.Instance               = SPI1;
    hspi.Init.Mode              = SPI_MODE_MASTER;
    hspi.Init.Direction         = SPI_DIRECTION_2LINES;
    hspi.Init.DataSize          = SPI_DATASIZE_8BIT;
    // ref. https://elm-chan.org/docs/mmc/mmc_e.html
    // "SPI mode 0 (CPHA=0, CPOL=0) is the proper setting to control MMC/SDC"
    hspi.Init.CLKPolarity       = SPI_POLARITY_LOW;
    hspi.Init.CLKPhase          = SPI_PHASE_1EDGE;
    hspi.Init.NSS               = SPI_NSS_SOFT;     // this allows software to manage NSS pin
    hspi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;
    // 16 MHz / 128 = 125 kHz , within 100-400 KHz range for identification
    // change this to prescaler = 2 later for larger speed
    hspi.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    hspi.Init.TIMode            = SPI_TIMODE_DISABLED;
    hspi.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLED;

    HAL_StatusTypeDef status = HAL_SPI_Init(&hspi);

    return status;
}

void SD_Task(void)
{
    FATFS   fs;     // filesystem object struct
    FIL     file;   // file object struct (selfexplanatoryblahbalhablah)
    FRESULT res;    // file function return code

    res = f_mount(&fs, "", 1);  // in fs mount the default drive (path="") immediately (opt=1)
                                // this ends up calling my disk_initialize() implementation in diskio.c
    if (res != FR_OK)
    {
        for (;;) { vTaskDelay(pdMS_TO_TICKS(1000)); }   // TODO: mount fail implementation
    }

    res = f_open(&file, "test.txt", FA_WRITE | FA_CREATE_ALWAYS);
    // it doesnt say in ff.h but 
    //  FA_CREATE_ALWAYS creates a new file or truncates and overwrites existing.
    //  FA_WRITE gives write access
    // ref: elm-chan.org/fsw/ff/doc/open.html

    if (res == FR_OK)
    {
        UINT bytes_written;
        const char *msg = "hello world\r\n";    // \r -> carriage return, holy fricking unc
        f_write(&file, msg, strlen(msg), &bytes_written);
        f_close(&file);
    }
        
    for (;;)
    {
        gps_fix_t fix;
        GPS_GetFix(&fix);

        char line[100];
        // snprintf writes to the provided char* buffer
        int len = snprintf(line, sizeof(line), 
                "%lu,%d,%d,%d\r\n",
                (unsigned long)fix.timestamp_epoch,
                fix.latitude,                   // -> .6f, good enough?
                fix.longitude,                  // -> .6f, good enough?
                fix.altitude_msl
                );

        // FA_OPEN_APPEND as apposed to FA_CREATE_ALWAYS adds to EOF rather than clearing and overwriting
        res = f_open(&file, "gps_log.csv", FA_WRITE | FA_OPEN_APPEND);  // nasty ORing of bitmaps (C :P)
        if (res == FR_OK)
        {
            UINT bytes_written;
            f_write(&file, line, (UINT)len, &bytes_written);
            f_close(&file);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// TODO PLACEHOLDER!!! REMOVE
void GPS_GetFix(gps_fix_t *fix)
{
    fix->latitude   = 0.0f;
    fix->longitude  = 0.0f;
    fix->altitude_msl   = 0.0f;
    fix->timestamp_epoch  = 0u;
}
