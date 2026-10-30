#include "usart_board.h"

/* TODO: add vector wrappers after the board and DMA streams are selected.
 * Each wrapper forwards its configured instance to one of:
 *
 *   usart_handle_irq(&lora_usart);        // USART errors only
 *   usart_handle_tx_dma_irq(&lora_usart); // TX DMA completion/errors
 *   usart_handle_rx_dma_irq(&lora_usart); // RX DMA half/full/errors
 *
 * No concrete USARTx/DMAx_Streamy vector is assigned while the board is TBD.
 */
