/************************************************************************************//**
* \file         Source/ARMCM4_XMC4/cpu.c
* \brief        Bootloader cpu module source file.
* \ingroup      Target_ARMCM4_XMC4
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


/****************************************************************************************
* Macro definitions
****************************************************************************************/
/** \brief Pointer to the user program's reset vector. */
#define CPU_USER_PROGRAM_STARTADDR_PTR    ((blt_addr)(NvmGetUserProgBaseAddress() + 0x00000004))
/** \brief Pointer to the user program's vector table. */
#define CPU_USER_PROGRAM_VECTABLE_OFFSET  ((blt_int32u)NvmGetUserProgBaseAddress())


/****************************************************************************************
* Register definitions
****************************************************************************************/
/** \brief Vector table offset register. */
#define SCB_VTOR    (*((volatile blt_int32u *) 0xE000ED08))


/****************************************************************************************
* Hook functions
****************************************************************************************/
#if (BOOT_CPU_USER_PROGRAM_START_HOOK > 0)
extern blt_bool CpuUserProgramStartHook(void);
#endif


/************************************************************************************//**
** \brief     Initializes the CPU module.
** \return    none.
**
****************************************************************************************/
void CpuInit(void)
{
  /* bootloader runs in polling mode so disable the global interrupts. this is done for
   * safety reasons. if the bootloader was started from a running user program, it could
   * be that the user program did not properly disable the interrupt generation of
   * peripherals. */
  CpuIrqDisable();
} /*** end of CpuInit ***/


/************************************************************************************//**
** \brief     Starts the user program, if one is present. In this case this function
**            does not return.
** \return    none.
**
****************************************************************************************/
void CpuStartUserProgram(void)
{
  void (*pProgResetHandler)(void);

  /* check if a user program is present by verifying the checksum */
  if (NvmVerifyChecksum() == BLT_FALSE)
  {
    /* not a valid user program so it cannot be started */
    return;
  }
#if (BOOT_CPU_USER_PROGRAM_START_HOOK > 0)
  /* invoke callback */
  if (CpuUserProgramStartHook() == BLT_FALSE)
  {
    /* callback requests the user program to not be started */
    return;
  }
#endif
#if (BOOT_COM_ENABLE > 0)
  /* release the communication interface */
  ComFree();
#endif
  /* reset the timer */
  TimerReset();
  /* remap user program's vector table */
  SCB_VTOR = CPU_USER_PROGRAM_VECTABLE_OFFSET & (blt_int32u)0x1FFFFF80;
  /* set the address where the bootloader needs to jump to. this is the address of
   * the 2nd entry in the user program's vector table. this address points to the
   * user program's reset handler.
   */
  pProgResetHandler = (void(*)(void))(*((blt_addr *)CPU_USER_PROGRAM_STARTADDR_PTR));
  /* The Cortex-M4 core has interrupts enabled out of reset. the bootloader
   * explicitly disables these for security reasons. Enable them here again, so it does
   * not have to be done by the user program.
   */
  CpuIrqEnable();
  /* start the user program by activating its reset interrupt service routine */
  pProgResetHandler();
} /*** end of CpuStartUserProgram ***/


/************************************************************************************//**
** \brief     Copies data from the source to the destination address.
** \param     dest Destination address for the data.
** \param     src  Source address of the data.
** \param     len  length of the data in bytes.
** \return    none.
**
****************************************************************************************/
void CpuMemCopy(blt_addr dest, blt_addr src, blt_int16u len)
{
  blt_int8u *from, *to;

  /* set casted pointers */
  from = (blt_int8u *)src;
  to = (blt_int8u *)dest;

  /* copy all bytes from source address to destination address */
  while (len-- > 0)
  {
    /* store byte value from source to destination */
    *to++ = *from++;
    /* keep the watchdog happy */
    CopService();
  }
} /*** end of CpuMemCopy ***/


/************************************************************************************//**
** \brief     Sets the bytes at the destination address to the specified value.
** \param     dest Destination address for the data.
** \param     value Value to write.
** \param     len  Number of bytes to write.
** \return    none.
**
****************************************************************************************/
void CpuMemSet(blt_addr dest, blt_int8u value, blt_int16u len)
{
  blt_int8u *to;

  /* set casted pointer */
  to = (blt_int8u *)dest;

  /* set all bytes at the destination address to the specified value */
  while (len-- > 0)
  {
    /* set byte value */
    *to++ = value;
    /* keep the watchdog happy */
    CopService();
  }
} /*** end of CpuMemSet ***/


/*********************************** end of cpu.c **************************************/