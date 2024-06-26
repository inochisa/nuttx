/****************************************************************************
 * arch/xtensa/src/esp32/esp32_clockconfig.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <sys/param.h>

#include "xtensa.h"
#include "xtensa_attr.h"
#include "hardware/esp32_dport.h"
#include "hardware/esp32_soc.h"
#include "hardware/esp32_uart.h"
#include "esp32_rtc.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_ESP_CONSOLE_UART_NUM
#define CONFIG_ESP_CONSOLE_UART_NUM 0
#endif

#define DEFAULT_CPU_FREQ  80

/****************************************************************************
 * Private Types
 ****************************************************************************/

enum cpu_freq_e
{
  CPU_80M = 0,
  CPU_160M = 1,
  CPU_240M = 2,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: esp32_uart_tx_wait_idle
 *
 * Description:
 *   Wait until uart tx full empty and the last char send ok.
 *
 * Input Parameters:
 *   uart_no   - 0 for UART0, 1 for UART1, 2 for UART2
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static inline void esp32_uart_tx_wait_idle(uint8_t uart_no)
{
  uint32_t status;
  do
    {
      status = getreg32(UART_STATUS_REG(uart_no));

      /* either tx count or state is non-zero */
    }
  while ((status & (UART_ST_UTX_OUT_M | UART_TXFIFO_CNT_M)) != 0);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

extern uint32_t g_ticks_per_us_pro;
#ifdef CONFIG_SMP
extern uint32_t g_ticks_per_us_app;
#endif

/****************************************************************************
 * Name:  esp32_update_cpu_freq
 *
 * Description:
 *   Set the real CPU ticks per us to the ets, so that ets_delay_us
 *   will be accurate. Call this function when CPU frequency is changed.
 *
 * Input Parameters:
 *   ticks_per_us - CPU ticks per us
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void IRAM_ATTR esp32_update_cpu_freq(uint32_t ticks_per_us)
{
  /* Update scale factors used by esp_rom_delay_us */

  g_ticks_per_us_pro = ticks_per_us;
#ifdef CONFIG_SMP
  g_ticks_per_us_app = ticks_per_us;
#endif
}

/****************************************************************************
 * Name: esp32_set_cpu_freq
 *
 * Description:
 *   Switch to one of PLL-based frequencies.
 *   Current frequency can be XTAL or PLL.
 *
 * Input Parameters:
 *   cpu_freq_mhz      - new CPU frequency
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void IRAM_ATTR esp32_set_cpu_freq(int cpu_freq_mhz)
{
  int dbias = DIG_DBIAS_80M_160M;
  int per_conf = CPU_240M;
  uint32_t  value;

  switch (cpu_freq_mhz)
    {
      case 160:
        per_conf = CPU_160M;
        break;

      case 240:
        dbias = DIG_DBIAS_240M;
        per_conf = CPU_240M;
        break;

      case 80:
        per_conf = CPU_80M;

      default:
        break;
    }

  value = (((80 * MHZ) >> 12) & UINT16_MAX) |
          ((((80 * MHZ) >> 12) & UINT16_MAX) << 16);
  putreg32(per_conf, DPORT_CPU_PER_CONF_REG);
  REG_SET_FIELD(RTC_CNTL_REG, RTC_CNTL_DIG_DBIAS_WAK, dbias);
  REG_SET_FIELD(RTC_CNTL_CLK_CONF_REG, RTC_CNTL_SOC_CLK_SEL,
                RTC_CNTL_SOC_CLK_SEL_PLL);
  putreg32(value, RTC_APB_FREQ_REG);
  esp32_update_cpu_freq(cpu_freq_mhz);
  esp32_rtc_wait_for_slow_cycle();
}

/****************************************************************************
 * Name: esp32_clockconfig
 *
 * Description:
 *   Called to initialize the ESP32.  This does whatever setup is needed to
 *   put the  SoC in a usable state.  This includes the initialization of
 *   clocking using the settings in board.h.
 *
 ****************************************************************************/

void esp32_clockconfig(void)
{
  uint32_t freq_mhz = CONFIG_ESP32_DEFAULT_CPU_FREQ_MHZ;
  uint32_t old_freq_mhz;
  uint32_t source_freq_mhz;
  enum esp32_rtc_xtal_freq_e xtal_freq = RTC_XTAL_FREQ_40M;

  old_freq_mhz = esp_rtc_clk_get_cpu_freq();
  if (old_freq_mhz == freq_mhz)
    {
      return;
    }

  switch (freq_mhz)
    {
      case 240:
        source_freq_mhz = RTC_PLL_FREQ_480M;
        break;

      case 160:
        source_freq_mhz = RTC_PLL_FREQ_320M;
        break;

      case 80:
        source_freq_mhz = RTC_PLL_FREQ_320M;
        break;

      default:
        return;
    }

  esp32_uart_tx_wait_idle(CONFIG_ESP_CONSOLE_UART_NUM);
  esp32_rtc_update_to_xtal(xtal_freq, 1);
  esp32_rtc_bbpll_enable();
  esp32_rtc_bbpll_configure(xtal_freq, source_freq_mhz);
  esp32_set_cpu_freq(freq_mhz);
}
