/************************************************************************************//**
* \file         Source\ARMCM4_STM32F3\can.c
* \brief        Bootloader CAN communication interface source file.
* \ingroup      Target_ARMCM4_STM32F3
* \internal
*----------------------------------------------------------------------------------------
*                          C O P Y R I G H T
*----------------------------------------------------------------------------------------
*   Copyright (c) 2016  by Feaser    http://www.feaser.com    All rights reserved
*
*----------------------------------------------------------------------------------------
*                            L I C E N S E
*----------------------------------------------------------------------------------------
* This file is part of OpenBLT. OpenBLT is free software: you can redistribute it and/or
* modify it under the terms of the GNU General Public License as published by the Free
* Software Foundation, either version 3 of the License, or (at your option) any later
* version.
*
* OpenBLT is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
* PURPOSE. See the GNU General Public License for more details.
*
* You have received a copy of the GNU General Public License along with OpenBLT. It
* should be located in ".\Doc\license.html". If not, contact Feaser to obtain a copy.
*
* \endinternal
****************************************************************************************/


/****************************************************************************************
* Include files
****************************************************************************************/
#include "boot.h"                                /* bootloader generic header          */
#include "stm32f3xx.h"                           /* STM32 CPU and HAL header           */


#if (BOOT_COM_CAN_ENABLE > 0)
/****************************************************************************************
* Macro definitions
****************************************************************************************/
/** \brief Timeout for transmitting a CAN message in milliseconds. */
#define CAN_MSG_TX_TIMEOUT_MS          (50u)


/****************************************************************************************
* Type definitions
****************************************************************************************/
/** \brief Structure type for grouping CAN bus timing related information. */
typedef struct t_can_bus_timing
{
  blt_int8u tseg1;                                    /**< CAN time segment 1          */
  blt_int8u tseg2;                                    /**< CAN time segment 2          */
} tCanBusTiming;


/****************************************************************************************
* Local constant declarations
****************************************************************************************/
/** \brief CAN bittiming table for dynamically calculating the bittiming settings.
 *  \details According to the CAN protocol 1 bit-time can be made up of between 8..25
 *           time quanta (TQ). The total TQ in a bit is SYNC + TSEG1 + TSEG2 with SYNC
 *           always being 1. The sample point is (SYNC + TSEG1) / (SYNC + TSEG1 + SEG2) *
 *           100%. This array contains possible and valid time quanta configurations with
 *           a sample point between 68..78%.
 */
static const tCanBusTiming canTiming[] =
{
  /*  TQ | TSEG1 | TSEG2 | SP  */
  /* ------------------------- */
  {  5, 2 },          /*   8 |   5   |   2   | 75% */
  {  6, 2 },          /*   9 |   6   |   2   | 78% */
  {  6, 3 },          /*  10 |   6   |   3   | 70% */
  {  7, 3 },          /*  11 |   7   |   3   | 73% */
  {  8, 3 },          /*  12 |   8   |   3   | 75% */
  {  9, 3 },          /*  13 |   9   |   3   | 77% */
  {  9, 4 },          /*  14 |   9   |   4   | 71% */
  { 10, 4 },          /*  15 |  10   |   4   | 73% */
  { 11, 4 },          /*  16 |  11   |   4   | 75% */
  { 12, 4 },          /*  17 |  12   |   4   | 76% */
  { 12, 5 },          /*  18 |  12   |   5   | 72% */
  { 13, 5 },          /*  19 |  13   |   5   | 74% */
  { 14, 5 },          /*  20 |  14   |   5   | 75% */
  { 15, 5 },          /*  21 |  15   |   5   | 76% */
  { 15, 6 },          /*  22 |  15   |   6   | 73% */
  { 16, 6 },          /*  23 |  16   |   6   | 74% */
  { 16, 7 },          /*  24 |  16   |   7   | 71% */
  { 16, 8 }           /*  25 |  16   |   8   | 68% */
};


/****************************************************************************************
* Local data declarations
****************************************************************************************/
/** \brief CAN handle to be used in API calls. */
static CAN_HandleTypeDef canHandle;

/** \brief Message buffer for transmitting CAN messages. */
static CanTxMsgTypeDef canTxMessage;

/** \brief Message buffer for receiving CAN messages. */
static CanRxMsgTypeDef canRxMessage;


/************************************************************************************//**
** \brief     Search algorithm to match the desired baudrate to a possible bus
**            timing configuration.
** \param     baud The desired baudrate in kbps. Valid values are 10..1000.
** \param     prescaler Pointer to where the value for the prescaler will be stored.
** \param     tseg1 Pointer to where the value for TSEG2 will be stored.
** \param     tseg2 Pointer to where the value for TSEG2 will be stored.
** \return    BLT_TRUE if the CAN bustiming register values were found, BLT_FALSE
**            otherwise.
**
****************************************************************************************/
static blt_bool CanGetSpeedConfig(blt_int16u baud, blt_int16u *prescaler,
                                  blt_int8u *tseg1, blt_int8u *tseg2)
{
  blt_int8u  cnt;

  /* loop through all possible time quanta configurations to find a match */
  for (cnt=0; cnt < sizeof(canTiming)/sizeof(canTiming[0]); cnt++)
  {
    if (((BOOT_CPU_SYSTEM_SPEED_KHZ/2) % (baud*(canTiming[cnt].tseg1+canTiming[cnt].tseg2+1))) == 0)
    {
      /* compute the prescaler that goes with this TQ configuration */
      *prescaler = (BOOT_CPU_SYSTEM_SPEED_KHZ/2)/(baud*(canTiming[cnt].tseg1+canTiming[cnt].tseg2+1));

      /* make sure the prescaler is valid */
      if ((*prescaler > 0) && (*prescaler <= 1024))
      {
        /* store the bustiming configuration */
        *tseg1 = canTiming[cnt].tseg1;
        *tseg2 = canTiming[cnt].tseg2;
        /* found a good bus timing configuration */
        return BLT_TRUE;
      }
    }
  }
  /* could not find a good bus timing configuration */
  return BLT_FALSE;
} /*** end of CanGetSpeedConfig ***/


/************************************************************************************//**
** \brief     Initializes the CAN controller and synchronizes it to the CAN bus.
** \return    none.
**
****************************************************************************************/
void CanInit(void)
{
  blt_int16u prescaler;
  blt_int8u  tseg1 = 0, tseg2 = 0;
  CAN_FilterConfTypeDef filterConfig;
  blt_int32u rxMsgId = BOOT_COM_CAN_RX_MSG_ID;
  blt_int32u rxFilterId, rxFilterMask;

  /* the current implementation supports CAN1. throw an assertion error in case a
   * different CAN channel is configured.
   */
  ASSERT_CT(BOOT_COM_CAN_CHANNEL_INDEX == 0);
  /* obtain bittiming configuration information. */
  if (CanGetSpeedConfig(BOOT_COM_CAN_BAUDRATE/1000, &prescaler, &tseg1, &tseg2) == BLT_FALSE)
  {
    /* Incorrect configuration. The specified baudrate is not supported for the given
     * clock configuration. Verify the following settings in blt_conf.h:
     *   - BOOT_COM_CAN_BAUDRATE
     *   - BOOT_CPU_XTAL_SPEED_KHZ
     *   - BOOT_CPU_SYSTEM_SPEED_KHZ
     */
    ASSERT_RT(BLT_FALSE);
  }

  /* set the CAN controller configuration. */
  canHandle.Instance = CAN;
  canHandle.pTxMsg = &canTxMessage;
  canHandle.pRxMsg = &canRxMessage;
  canHandle.Init.TTCM = DISABLE;
  canHandle.Init.ABOM = DISABLE;
  canHandle.Init.AWUM = DISABLE;
  canHandle.Init.NART = DISABLE;
  canHandle.Init.RFLM = DISABLE;
  canHandle.Init.TXFP = DISABLE;
  canHandle.Init.Mode = CAN_MODE_NORMAL;
  canHandle.Init.SJW = CAN_SJW_1TQ;
  canHandle.Init.BS1 = ((blt_int32u)tseg1 - 1) << CAN_BTR_TS1_Pos;
  canHandle.Init.BS2 = ((blt_int32u)tseg2 - 1) << CAN_BTR_TS2_Pos;
  canHandle.Init.Prescaler = prescaler;
  /* initialize the CAN controller. this only fails if the CAN controller hardware is
   * faulty. no need to evaluate the return value as there is nothing we can do about
   * a faulty CAN controller.
   */
  (void)HAL_CAN_Init(&canHandle);
  /* determine the reception filter mask and id values such that it only leaves one
   * CAN identifier through (BOOT_COM_CAN_RX_MSG_ID).
   */
  if ((rxMsgId & 0x80000000) == 0)
  {
    rxFilterId = rxMsgId << CAN_RI0R_STID_Pos;
    rxFilterMask = (CAN_RI0R_STID_Msk) | CAN_RI0R_IDE;
  }
  else
  {
    /* negate the ID-type bit */
    rxMsgId &= ~0x80000000;
    rxFilterId = (rxMsgId << CAN_RI0R_EXID_Pos) | CAN_RI0R_IDE;
    rxFilterMask = (CAN_RI0R_EXID_Msk) | CAN_RI0R_IDE;
  }
  /* configure the reception filter. note that the implementation of this function
   * always returns HAL_OK, so no need to evaluate the return value.
   */
  filterConfig.FilterNumber = 0;
  filterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
  filterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
  filterConfig.FilterIdHigh = (rxFilterId >> 16) & 0x0000FFFFu;
  filterConfig.FilterIdLow = rxFilterId & 0x0000FFFFu;
  filterConfig.FilterMaskIdHigh = (rxFilterMask >> 16) & 0x0000FFFFu;
  filterConfig.FilterMaskIdLow = rxFilterMask & 0x0000FFFFu;
  filterConfig.FilterFIFOAssignment = 0;
  filterConfig.FilterActivation = ENABLE;
  /* the bank number is don't care for STM32F3 devices as it only supports one CAN
   * controller.
   */
  filterConfig.BankNumber = 14;
  (void)HAL_CAN_ConfigFilter(&canHandle, &filterConfig);
} /*** end of CanInit ***/


/************************************************************************************//**
** \brief     Transmits a packet formatted for the communication interface.
** \param     data Pointer to byte array with data that it to be transmitted.
** \param     len  Number of bytes that are to be transmitted.
** \return    none.
**
****************************************************************************************/
void CanTransmitPacket(blt_int8u *data, blt_int8u len)
{
  blt_int8u byteIdx;
  blt_int32u txMsgId = BOOT_COM_CAN_TX_MSG_ID;

  /* configure the message that should be transmitted. */
  if ((txMsgId & 0x80000000) == 0)
  {
    /* set the 11-bit CAN identifier. */
    canHandle.pTxMsg->StdId = txMsgId;
    canHandle.pTxMsg->IDE = CAN_ID_STD;
  }
  else
  {
    /* negate the ID-type bit */
    txMsgId &= ~0x80000000;
    /* set the 29-bit CAN identifier. */
    canHandle.pTxMsg->ExtId = txMsgId;
    canHandle.pTxMsg->IDE = CAN_ID_EXT;
  }
  canHandle.pTxMsg->RTR = CAN_RTR_DATA;
  canHandle.pTxMsg->DLC = len;
  /* copy the message data. */
  for (byteIdx = 0; byteIdx < len; byteIdx++)
  {
    canHandle.pTxMsg->Data[byteIdx] = data[byteIdx];
  }
  /* submit the message for transmission. no need to check the return value. if the
   * response cannot be transmitted, then the receiving node will detect a timeout.
   */
  (void)HAL_CAN_Transmit(&canHandle, CAN_MSG_TX_TIMEOUT_MS);
} /*** end of CanTransmitPacket ***/


/************************************************************************************//**
** \brief     Receives a communication interface packet if one is present.
** \param     data Pointer to byte array where the data is to be stored.
** \param     len Pointer where the length of the packet is to be stored.
** \return    BLT_TRUE is a packet was received, BLT_FALSE otherwise.
**
****************************************************************************************/
blt_bool CanReceivePacket(blt_int8u *data, blt_int8u *len)
{
  blt_int32u rxMsgId = BOOT_COM_CAN_RX_MSG_ID;
  blt_bool result = BLT_FALSE;
  blt_bool packetIdMatches = BLT_FALSE;
  blt_int8u byteIdx;

  /* poll for received CAN messages that await processing. */
  if (HAL_CAN_Receive(&canHandle, CAN_FIFO0, 0) == HAL_OK)
  {
    /* check if this message has the configured CAN packet identifier. */
    if ((rxMsgId & 0x80000000) == 0)
    {
      /* was an 11-bit CAN message received that matches? */
      if ( (canHandle.pRxMsg->StdId == rxMsgId) &&
           (canHandle.pRxMsg->IDE == CAN_ID_STD) )
      {
        /* set flag that a packet with a matching CAN identifier was received. */
        packetIdMatches = BLT_TRUE;
      }
    }
    else
    {
      /* negate the ID-type bit */
      rxMsgId &= ~0x80000000;
      /* was an 29-bit CAN message received that matches? */
      if ( (canHandle.pRxMsg->ExtId == rxMsgId) &&
           (canHandle.pRxMsg->IDE == CAN_ID_EXT) )
      {
        /* set flag that a packet with a matching CAN identifier was received. */
        packetIdMatches = BLT_TRUE;
      }
    }

    /* only continue if a packet with a matching CAN identifier was received. */
    if (packetIdMatches == BLT_TRUE)
    {
      /* copy the received package data. */
      for (byteIdx = 0; byteIdx < canHandle.pRxMsg->DLC; byteIdx++)
      {
        data[byteIdx] = canHandle.pRxMsg->Data[byteIdx];
      }
      *len = canHandle.pRxMsg->DLC;
      /* update the return value to indicate that new packet data was received. */
      result = BLT_TRUE;
    }
  }
  /* Give the result back to the caller. */
  return result;
} /*** end of CanReceivePacket ***/
#endif /* BOOT_COM_CAN_ENABLE > 0 */


/*********************************** end of can.c **************************************/
