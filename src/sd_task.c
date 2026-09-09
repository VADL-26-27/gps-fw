#include "stm32f4xx_hal.h"
#include "sd_task.h"

static SD_HandleTypeDef hsd;

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

HAL_StatusTypeDef SD_Init(void)
{
    __HAL_RCC_SDIO_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE(); // SDIO is on DMA2, ref. pg. 204 rm0390 Table 29

    SD_GPIO_Init();

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
