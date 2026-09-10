#include "ff.h"
#include "stm32f411xe.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_dma.h"
#include "sd_task.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

SD_HandleTypeDef hsd;        // handler sd
static DMA_HandleTypeDef hdma_tx;   // handler dma

static void SD_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    // configures SDIO pins
    // ref. pg. 58 ds10693 Table 11
    //  or ref. pg. 38 SD physical layer spec
    // SDIO is AF12
    // SDIO_CMD- PD2 <- configured below
    // SDIO_D0 - PC8
    // SDIO_D1 - PC9
    // SDIO_D2 - PC10
    // SDIO_D3 - PC11
    // SDIO_CK - PC12
    // ref. pg. 963 rm0390
    // All data lines are push-pull
    // SDIO_CMD is push-pull for SD cards
    // SDIO_CK is input (ref. pg.38 SD physical layer spec, Table 3-1)
    GPIO_InitStruct.Pin       = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
    // The above pins will be:
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;            // alternate function, push-pull
    GPIO_InitStruct.Pull      = GPIO_PULLUP;                // 
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF12_SDIO;             // SDIO is AF12, ref. pg. 58 ds10693 Table 11
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    // SDIO_CMD is on a different GPIO bus, same deal though
    // We are adding the pin to the existing struct so it abides by the above defined rules
    GPIO_InitStruct.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
}

// note that this method only links dma tx (memory to peripheral) (those who read)
static void SD_DMA_Init(void)
{
    __HAL_RCC_DMA2_CLK_ENABLE();

    hdma_tx.Instance                 = DMA2_Stream3;            // ref. pg. 204 rm0390 Table 29, Stream6 also valid
    hdma_tx.Init.Channel             = DMA_CHANNEL_4;           // ref. pg. 204 rm0390 Table 29
    hdma_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
    hdma_tx.Init.PeriphInc           = DMA_PINC_DISABLE;        // SDIO fifo, dont increment
    hdma_tx.Init.MemInc              = DMA_MINC_ENABLE;         // increment on memory buffer
    hdma_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;     // DMA_SxCR MSIZE, word = 32 bit
    hdma_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_WORD;     // DMA_SxCR PSIZE
    hdma_tx.Init.Mode                = DMA_PFCTRL;              // the peripheral decides when DMA should happen
    hdma_tx.Init.Priority            = DMA_PRIORITY_VERY_HIGH;  // TODO: change this maybe? could be a concern to someone
    hdma_tx.Init.FIFOMode            = DMA_FIFOMODE_ENABLE;     // need fifo to burst
    hdma_tx.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_FULL; // fill the DMA fifo complete before flush
    hdma_tx.Init.MemBurst            = DMA_MBURST_INC4;         // writes 4 words in one transaction (memory to DMA fifo)
    hdma_tx.Init.PeriphBurst         = DMA_PBURST_INC4;         // writes 4 words in one transaction (DMA fifo to SD fifo)

    HAL_DMA_Init(&hdma_tx);
    __HAL_LINKDMA(&hsd, hdmatx, hdma_tx);

    // NVIC allows DMA transfer and SDIO transactions to be non-blocking
    HAL_NVIC_SetPriority(DMA2_Stream3_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream3_IRQn);

    HAL_NVIC_SetPriority(SDIO_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(SDIO_IRQn);
}

HAL_StatusTypeDef SD_Init(void)
{
    __HAL_RCC_SDIO_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE(); // SDIO is on DMA2, ref. pg. 204 rm0390 Table 29

    SD_GPIO_Init();
    SD_DMA_Init();

    hsd.Instance                 = SDIO;
    hsd.Init.ClockEdge           = SDIO_CLOCK_EDGE_RISING;
    hsd.Init.ClockBypass         = SDIO_CLOCK_BYPASS_DISABLE;
    hsd.Init.ClockPowerSave      = SDIO_CLOCK_POWER_SAVE_DISABLE;
    hsd.Init.BusWide             = SDIO_BUS_WIDE_1B;
        // Must be 1 wide, Can be changed after initialization
        // ref. pg. 963 rm0390 
    hsd.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
    hsd.Init.ClockDiv            = SDIO_TRANSFER_CLK_DIV;               // sets SDIO_CK frequency

    HAL_StatusTypeDef status = HAL_SD_Init(&hsd);   // runs card identification (CMD0/CMD8/ACMD41/CMD2/CMD3...)
                                                    // This shit sucks, thank you blessed HAL
    if (status != HAL_OK)
    {
        return status;
    }

    return HAL_SD_ConfigWideBusOperation(&hsd, SDIO_BUS_WIDE_4B);   // now here we can switch
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
        
    for (;;) { vTaskDelay(pdMS_TO_TICKS(1000)); }   // TODO: write from memory buffer
}
