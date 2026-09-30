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

  printf("\r\n");
  printf("==================================================\r\n");
  printf("  Seeed Studio LoRa-E5 mini (STM32WLE5JC)\r\n");
  printf("  Wezel Telemetryczny (Czujnik LoRa Point-to-Point)\r\n");
  printf("  Parametry: 868.0 MHz | SF7 | BW 125kHz | CR 4/5\r\n");
  printf("==================================================\r\n\r\n");

  // Inicjalizacja modemu radiowego LoRa
  if (!radio_e5_init())
  {
    printf("[BLAD KRYTYCZNY] Inicjalizacja radia LoRa nie powiodla sie!\r\n");
  }

  /* Infinite loop */
  for(;;)
  {
    seq++;
    LL_GPIO_SetOutputPin(LED_USER_GPIO_Port, LED_USER_Pin); // Włącz LED na czas transmisji

    // Symulacja pomiaru (później podłączymy fizyczny ADC wilgotności i pomiar baterii)
    float hum = 45.0f + (float)(seq % 10) * 0.8f;
    float vbat = 4.15f - (float)(seq % 20) * 0.01f;

    char tx_payload[128];
    int len = snprintf(tx_payload, sizeof(tx_payload),
                       "{\"id\":\"plant_pot_1\",\"hum\":%.1f,\"vbat\":%.2f,\"seq\":%lu}",
                       hum, vbat, (unsigned long)seq);

    printf("--------------------------------------------------\r\n");
    printf("[LoRa-E5 TX #%lu] Wysylam: %s (%d B)\r\n", (unsigned long)seq, tx_payload, len);

    // 1. Nadanie ramki telemetrycznej z timeoutem 1000 ms
    bool tx_ok = radio_e5_send((const uint8_t*)tx_payload, len, 1000);
    if (tx_ok)
    {
      printf("  -> Ramka wyslana pomyslnie. Czekam na ACK (max 1500 ms)...\r\n");

      // 2. Otwarcie okna nasłuchu na potwierdzenie ACK od bramy
      uint8_t rx_buf[64] = {0};
      float rssi = 0.0f, snr = 0.0f;
      int16_t rx_len = radio_e5_receive(rx_buf, sizeof(rx_buf) - 1, 1500, &rssi, &snr);

      if (rx_len > 0)
      {
        rx_buf[rx_len] = '\0';
        printf("  [SUKCES] Odebrano ACK: '%s' | RSSI: %.1f dBm | SNR: %.1f dB\r\n",
               (char*)rx_buf, rssi, snr);
      }
      else if (rx_len == 0)
      {
        printf("  [TIMEOUT] Brak odpowiedzi ACK od bramy w oknie 1500 ms.\r\n");
      }
      else
      {
        printf("  [BLAD] Blad ramki RX (kod: %d)!\r\n", rx_len);
      }
    }
    else
    {
      printf("  [BLAD] Nie udalo sie nadac pakietu!\r\n");
    }

    LL_GPIO_ResetOutputPin(LED_USER_GPIO_Port, LED_USER_Pin); // Wyłącz LED
    printf("--------------------------------------------------\r\n\r\n");

    // Odstęp między kolejnymi wysyłkami (5 sekund)
    osDelay(5000);
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
