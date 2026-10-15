/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include "radio_e5.h"
#include "adc_soil.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 512 * 4
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  uint32_t seq = 0;

  printf("[Node] LoRa telemetry sensor starting (868 MHz, SF7, BW125)\r\n");

  if (!radio_e5_init())
  {
    printf("[Node] Radio init failed\r\n");
  }

  adc_soil_init();

  for (;;)
  {
    seq++;
    LL_GPIO_SetOutputPin(LED_USER_GPIO_Port, LED_USER_Pin);

    uint16_t raw_adc = adc_soil_read_raw();
    uint16_t permille = adc_soil_raw_to_permille(raw_adc);
    uint16_t vdda_mv = adc_read_vdda_mv();

    char tx_payload[128];
    int len = snprintf(tx_payload, sizeof(tx_payload),
                       "{\"id\":\"plant_pot_1\",\"hum\":%u.%u,\"raw\":%u,\"vbat\":%u.%02u,\"seq\":%lu}",
                       permille / 10U, permille % 10U,
                       (unsigned int)raw_adc,
                       vdda_mv / 1000U, (vdda_mv % 1000U) / 10U,
                       (unsigned long)seq);

    printf("[TX #%lu] %s (%d B)\r\n", (unsigned long)seq, tx_payload, len);

    if (radio_e5_send((const uint8_t *)tx_payload, len, 1000))
    {
      /* Listen for ACK within 1500 ms */
      uint8_t rx_buf[64] = {0};
      float rssi = 0.0f, snr = 0.0f;
      int16_t rx_len = radio_e5_receive(rx_buf, sizeof(rx_buf) - 1, 1500, &rssi, &snr);

      if (rx_len > 0)
      {
        rx_buf[rx_len] = '\0';
        printf("[ACK] '%s' (RSSI: %.1f dBm, SNR: %.1f dB)\r\n", (char *)rx_buf, rssi, snr);
      }
      else if (rx_len == 0)
      {
        printf("[ACK] Timeout\r\n");
      }
      else
      {
        printf("[ACK] RX error (%d)\r\n", rx_len);
      }
    }
    else
    {
      printf("[TX] Failed to transmit\r\n");
    }

    LL_GPIO_ResetOutputPin(LED_USER_GPIO_Port, LED_USER_Pin);

    osDelay(5000);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
