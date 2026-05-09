#include "can_init.h"

#include "main.h"
#include "app_log.h"

#include <stdbool.h>
#include <string.h>

#if defined(HAL_FDCAN_MODULE_ENABLED)

static FDCAN_HandleTypeDef hfdcan1;

static bool CAN_InitInternalLoopback(void)
{
    hfdcan1.Instance = FDCAN1;
    hfdcan1.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
    hfdcan1.Init.Mode = FDCAN_MODE_INTERNAL_LOOPBACK;
    hfdcan1.Init.AutoRetransmission = DISABLE;
    hfdcan1.Init.TransmitPause = DISABLE;
    hfdcan1.Init.ProtocolException = DISABLE;

    /* 80 MHz FDCAN kernel clock with 500 kbit/s nominal bitrate:
     * tq = (Prescaler / fclk) = 10 / 80 MHz = 125 ns
     * bit time = (1 + 13 + 2) * tq = 16 * 125 ns = 2 us */
    hfdcan1.Init.NominalPrescaler = 10;
    hfdcan1.Init.NominalSyncJumpWidth = 2;
    hfdcan1.Init.NominalTimeSeg1 = 13;
    hfdcan1.Init.NominalTimeSeg2 = 2;

    hfdcan1.Init.DataPrescaler = 1;
    hfdcan1.Init.DataSyncJumpWidth = 1;
    hfdcan1.Init.DataTimeSeg1 = 1;
    hfdcan1.Init.DataTimeSeg2 = 1;

    hfdcan1.Init.MessageRAMOffset = 0;
    hfdcan1.Init.StdFiltersNbr = 1;
    hfdcan1.Init.ExtFiltersNbr = 0;
    hfdcan1.Init.RxFifo0ElmtsNbr = 1;
    hfdcan1.Init.RxFifo0ElmtSize = FDCAN_DATA_BYTES_8;
    hfdcan1.Init.RxFifo1ElmtsNbr = 0;
    hfdcan1.Init.RxFifo1ElmtSize = FDCAN_DATA_BYTES_8;
    hfdcan1.Init.RxBuffersNbr = 0;
    hfdcan1.Init.RxBufferSize = FDCAN_DATA_BYTES_8;
    hfdcan1.Init.TxEventsNbr = 0;
    hfdcan1.Init.TxBuffersNbr = 0;
    hfdcan1.Init.TxFifoQueueElmtsNbr = 1;
    hfdcan1.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
    hfdcan1.Init.TxElmtSize = FDCAN_DATA_BYTES_8;

    if (HAL_FDCAN_Init(&hfdcan1) != HAL_OK) {
        APP_LOGE("CAN", "HAL_FDCAN_Init failed");
        return false;
    }

    FDCAN_FilterTypeDef filter = {0};
    filter.IdType = FDCAN_STANDARD_ID;
    filter.FilterIndex = 0;
    filter.FilterType = FDCAN_FILTER_MASK;
    filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filter.FilterID1 = 0x123;
    filter.FilterID2 = 0x7FF;
    if (HAL_FDCAN_ConfigFilter(&hfdcan1, &filter) != HAL_OK) {
        APP_LOGE("CAN", "HAL_FDCAN_ConfigFilter failed");
        return false;
    }

    if (HAL_FDCAN_Start(&hfdcan1) != HAL_OK) {
        APP_LOGE("CAN", "HAL_FDCAN_Start failed");
        return false;
    }

    return true;
}

static bool CAN_RunLoopbackSelfTest(void)
{
    const uint8_t tx_payload[8] = {0x41, 0x57, 0x54, 0x4F, 0xCA, 0x01, 0x02, 0x03};
    uint8_t rx_payload[8] = {0};

    FDCAN_TxHeaderTypeDef tx = {0};
    tx.Identifier = 0x123;
    tx.IdType = FDCAN_STANDARD_ID;
    tx.TxFrameType = FDCAN_DATA_FRAME;
    tx.DataLength = FDCAN_DLC_BYTES_8;
    tx.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    tx.BitRateSwitch = FDCAN_BRS_OFF;
    tx.FDFormat = FDCAN_CLASSIC_CAN;
    tx.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    tx.MessageMarker = 0;

    if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &tx, tx_payload) != HAL_OK) {
        APP_LOGE("CAN", "AddMessageToTxFifoQ failed");
        return false;
    }

    uint32_t start = HAL_GetTick();
    while (HAL_FDCAN_GetRxFifoFillLevel(&hfdcan1, FDCAN_RX_FIFO0) == 0U) {
        if ((HAL_GetTick() - start) > 100U) {
            APP_LOGE("CAN", "loopback timeout waiting for Rx frame");
            return false;
        }
    }

    FDCAN_RxHeaderTypeDef rx = {0};
    if (HAL_FDCAN_GetRxMessage(&hfdcan1, FDCAN_RX_FIFO0, &rx, rx_payload) != HAL_OK) {
        APP_LOGE("CAN", "GetRxMessage failed");
        return false;
    }

    if ((rx.Identifier != tx.Identifier) || (rx.DataLength != tx.DataLength) ||
        (memcmp(tx_payload, rx_payload, sizeof(tx_payload)) != 0)) {
        APP_LOGE("CAN", "loopback mismatch id=0x%lx dlc=0x%lx",
                 (unsigned long)rx.Identifier,
                 (unsigned long)rx.DataLength);
        return false;
    }

    return true;
}

bool CAN_BringupAndSelfTest(void)
{
    APP_LOGI("CAN", "bring-up start (FDCAN1 internal loopback)");

    if (!CAN_InitInternalLoopback()) {
        return false;
    }

    if (!CAN_RunLoopbackSelfTest()) {
        return false;
    }

    APP_LOGI("CAN", "bring-up ok (internal loopback TX/RX passed)");
    return true;
}

void HAL_FDCAN_MspInit(FDCAN_HandleTypeDef *hfdcan)
{
    if (hfdcan->Instance != FDCAN1) {
        return;
    }

    RCC_PeriphCLKInitTypeDef periph_clk = {0};
    periph_clk.PeriphClockSelection = RCC_PERIPHCLK_FDCAN;
    periph_clk.FdcanClockSelection = RCC_FDCANCLKSOURCE_PLL;

    if (HAL_RCCEx_PeriphCLKConfig(&periph_clk) != HAL_OK) {
        Error_Handler();
    }

    __HAL_RCC_FDCAN_CLK_ENABLE();
}

void HAL_FDCAN_MspDeInit(FDCAN_HandleTypeDef *hfdcan)
{
    if (hfdcan->Instance != FDCAN1) {
        return;
    }

    __HAL_RCC_FDCAN_CLK_DISABLE();
}

#else

bool CAN_BringupAndSelfTest(void)
{
    APP_LOGW("CAN", "HAL_FDCAN_MODULE_ENABLED is off");
    return false;
}

#endif
