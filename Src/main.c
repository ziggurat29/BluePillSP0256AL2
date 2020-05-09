/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2019 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "usbd_cdc.h"	//just for the XXX_USBCDC_PresenceHack()
#include "system_interfaces.h"
#include "serial_devices.h"
#include "util_circbuff2.h"

#include "lamps.h"
#include "task_notification_bits.h"

#include "BluePillSP0256AL2_settings.h"

#include "task_monitor.h"
#include "task_sp0256.h"

#include "backup_registers.h"


#ifndef COUNTOF
#define COUNTOF(arr) (sizeof(arr)/sizeof(arr[0]))
#endif

#if ! HAVE_UART1 && ! HAVE_USBCDC
#error You must set at least one of HAVE_UART1 HAVE_USBCDC to 1 in project settings
#endif

//This controls whether we use the FreeRTOS heap implementation to also provide
//the libc malloc() and friends.
#define USE_FREERTOS_HEAP_IMPL 1

#ifdef DEBUG
volatile size_t g_nHeapFree;
volatile size_t g_nMinEverHeapFree;
volatile int g_nMaxMonTxQueue;
volatile int g_nMaxMonRxQueue;
volatile int g_nMaxSP0256Queue;
volatile int g_nMinStackFreeDefault;
volatile int g_nMinStackFreeMonitor;
volatile int g_nMinStackFreeSP0256;
#endif

#if USE_FREERTOS_HEAP_IMPL

#if configAPPLICATION_ALLOCATED_HEAP
//we define our heap (to be used by FreeRTOS heap_4.c implementation) to be
//exactly where we want it to be.
__attribute__((aligned(8))) 
uint8_t ucHeap[ configTOTAL_HEAP_SIZE ];
#endif
//we implemented a 'realloc' for a heap_4 derived implementation
extern void* pvPortRealloc( void* pvOrig, size_t xWantedSize );
//we implemented a 'heapwalk' function
typedef int (*CBK_HEAPWALK) ( void* pblk, uint32_t nBlkSize, int bIsFree, void* pinst );
extern int vPortHeapWalk ( CBK_HEAPWALK pfnWalk, void* pinst );

//'wrapped functions' for library interpositioning
//you must specify these gcc (linker-directed) options to cause the wrappers'
//delights to be generated:

//-Wl,--wrap,malloc -Wl,--wrap,free -Wl,--wrap,realloc -Wl,--wrap,calloc
//-Wl,--wrap,_malloc_r -Wl,--wrap,_free_r -Wl,--wrap,_realloc_r -Wl,--wrap,_calloc_r

//hmm; can I declare these 'inline' and save a little code and stack?
void* __wrap_malloc ( size_t size ) { return pvPortMalloc ( size ); }
void __wrap_free ( void* pv ) { vPortFree ( pv ); }
void* __wrap_realloc ( void* pv, size_t size ) { return pvPortRealloc ( pv, size ); }

void* __wrap__malloc_r ( struct _reent* r, size_t size ) { return pvPortMalloc ( size ); }
void __wrap__free_r ( struct _reent* r, void* pv ) { vPortFree ( pv ); }
void* __wrap__realloc_r ( struct _reent* r, void* pv, size_t size ) { return pvPortRealloc ( pv, size ); }

#endif

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
RTC_HandleTypeDef hrtc;

TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;

UART_HandleTypeDef huart1;

osThreadId defaultTaskHandle;
uint32_t defaultTaskBuffer[ 128 ];
osStaticThreadDef_t defaultTaskControlBlock;
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_RTC_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM4_Init(void);
static void MX_USART1_UART_Init(void);
void StartDefaultTask(void const * argument);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
	//enable the core debug cycle counter to be used as a precision timer
	CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
	DWT->CYCCNT = 0;
	DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  /* USER CODE END 1 */
  

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

	//if you get a linker fail on the following, it is because some manual
	//changes to:
	//  .\Middlewares\ST\STM32_USB_Device_Library\Class\CDC\Inc\usbd_cdc.h
	//  .\Middlewares\ST\STM32_USB_Device_Library\Class\CDC\Src\usbd_cdc.c
	//  .\Src\usbd_cdc_if.c
	//must be applied.  There are backups of those files to help with that.
	//This has to be done manually, because the changes are in tool generated
	//code that gets overwritten when you re-run STM32CubeMX.  The nature of
	//those changes are such that when they are overwritten, you will still
	//be able to build but stuff won't work at runtime.  This hack will cause
	//the build to fail if you forget to merge those changes back on, thus
	//prompting you to do so.
	//Sorry for the inconvenience, but I don't think there is any better way
	//of making it obvious that this chore simply must be done.
	XXX_USBCDC_PresenceHack();	//this does nothing real; do not delete

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

	//do a dummy alloc to cause the heap to be init'ed and so the memory stats as well
	vPortFree ( pvPortMalloc ( 0 ) );

	//get system config depersisted from flash before we do much more
	Settings_depersist();

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_RTC_Init();
  MX_TIM3_Init();
  MX_TIM4_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

	//give the task processors an early opportunity to setup any internal
	//stuff (e.g. FreeRTOS objects, etc.).  In general, we should do this
	//after the peripherals have been configured.
	SP0256_Initialize();	//setup whatnot for the SP0256 task processor and reset
	Monitor_Initialize();	//setup whatnot for the Monitor task

  /* USER CODE END 2 */

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
  /* definition and creation of defaultTask */
  osThreadStaticDef(defaultTask, StartDefaultTask, osPriorityNormal, 0, 128, defaultTaskBuffer, &defaultTaskControlBlock);
  defaultTaskHandle = osThreadCreate(osThread(defaultTask), NULL);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* Start scheduler */
  osKernelStart();
  
  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the CPU, AHB and APB busses clocks 
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.LSEState = RCC_LSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB busses clocks 
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC|RCC_PERIPHCLK_USB;
  PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef DateToUpdate = {0};

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */
  /** Initialize RTC Only 
  */
  hrtc.Instance = RTC;
  hrtc.Init.AsynchPrediv = RTC_AUTO_1_SECOND;
  hrtc.Init.OutPut = RTC_OUTPUTSOURCE_NONE;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN Check_RTC_BKUP */
	//we use one of the 'backup registers' to store some flags; the 'RTC set'
	//is used to indicate we have set the clock at some point.  If this flag is
	//set in the backup register, then we have /not power-cycled the board, and
	//so we avoid the generated code, below which will blast the RTC setting.
	HAL_PWR_EnableBkUpAccess();	//... and leave it that way
	uint32_t flags = HAL_RTCEx_BKUPRead ( &hrtc, FLAGS_REGISTER );
	if ( ! ( flags & FLAG_HAS_SET_RTC ) )
	{	//ugly hack to skip needlessly setting the RTC via generated code
  /* USER CODE END Check_RTC_BKUP */

  /** Initialize RTC and set the Time and Date 
  */
  sTime.Hours = 0;
  sTime.Minutes = 0;
  sTime.Seconds = 0;

  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }
  DateToUpdate.WeekDay = RTC_WEEKDAY_FRIDAY;
  DateToUpdate.Month = RTC_MONTH_JULY;
  DateToUpdate.Date = 26;
  DateToUpdate.Year = 19;

  if (HAL_RTC_SetDate(&hrtc, &DateToUpdate, RTC_FORMAT_BIN) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */
	}
else
	{
		//If we have reset or woken up but aren't going to set the RTC because
		//it was already set, then we at least need to do the following or we
		//won't ever be able to write to the RTC (e.g. for setting alarm)
		//NOTE:  on the 'F1's, the RTC is 'V1', which means that there is not
		//date.  The date is emulated in software, and so is lost on reset,
		//etc.
		HAL_RTC_WaitForSynchro(&hrtc);
	}	//end ugly hack to avoid some generated code

  /* USER CODE END RTC_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 5;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 254;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 128;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 0;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 6530;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim4, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LED2_GPIO_Port, LED2_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, A1_Pin|A2_Pin|A3_Pin|A4_Pin 
                          |A5_Pin|A6_Pin|SP_RST_Pin|TWIGGLE_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SP_nALD_GPIO_Port, SP_nALD_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin : LED2_Pin */
  GPIO_InitStruct.Pin = LED2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED2_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : A1_Pin A2_Pin A3_Pin A4_Pin 
                           A5_Pin A6_Pin SP_RST_Pin */
  GPIO_InitStruct.Pin = A1_Pin|A2_Pin|A3_Pin|A4_Pin 
                          |A5_Pin|A6_Pin|SP_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PA7 PA15 */
  GPIO_InitStruct.Pin = GPIO_PIN_7|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : SP_nALD_Pin */
  GPIO_InitStruct.Pin = SP_nALD_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(SP_nALD_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : BOOT1_Pin SP_SBY_Pin */
  GPIO_InitStruct.Pin = BOOT1_Pin|SP_SBY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : SP_nLRQ_Pin */
  GPIO_InitStruct.Pin = SP_nLRQ_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(SP_nLRQ_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PB12 PB13 PB14 PB15 
                           PB3 PB4 PB5 PB6 
                           PB7 PB8 PB9 */
  GPIO_InitStruct.Pin = GPIO_PIN_12|GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15 
                          |GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6 
                          |GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : TWIGGLE_Pin */
  GPIO_InitStruct.Pin = TWIGGLE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(TWIGGLE_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

}

/* USER CODE BEGIN 4 */



//====================================================


//this is made a function simply for tidiness, and locals lifetime
void __startWorkerTasks ( void )
{
	//kick off the SP0256 thread, which handles sending phoneme data
	{
	osThreadStaticDef(taskSP0256, thrdfxnSP0256Task, osPriorityNormal, 0, COUNTOF(g_tbSP0256), g_tbSP0256, &g_tcbSP0256);
	g_thSP0256 = osThreadCreate(osThread(taskSP0256), NULL);
	}
	//kick off the monitor thread, which handles the user interactions
	{
	osThreadStaticDef(taskMonitor, thrdfxnMonitorTask, osPriorityNormal, 0, COUNTOF(g_tbMonitor), g_tbMonitor, &g_tcbMonitor);
	g_thMonitor = osThreadCreate(osThread(taskMonitor), NULL);
	}
}


//====================================================
//miscellaneous hooks of our creation



//XXX sync this with interface binding, below
#if HAVE_UART1
#elif HAVE_USBCDC

//well-discplined serial clients will assert DTR, and we
//can use that as an indication that a client application
//opened the port.
//NOTE:  These lines are often also set to an initial state
//by the host's driver, so do not consider these to be
//exclusively an indication of a client connecting.  Hosts
//usually will deassert these signals when this device
//enumerates.  Lastly, there is no guarantee that a client
//will assert DTR, so it's not 100% guarantee, just a pretty
//good indicator.
//NOTE:  we are in an ISR at this time
void USBCDC_DTR ( int bAssert )
{
	Monitor_ClientConnect ( bAssert );
}

//(unneeded)
//void USBCDC_RTS ( int bAssert ) { }
#endif




//XXX sync this with interface binding, below
#if HAVE_UART1
void UART1_DataAvailable ( void )
#elif HAVE_USBCDC
void USBCDC_DataAvailable ( void )
#endif
{
	//this notification is required because our Monitor is implemented with the
	//non-blocking command interface, so we need to know when to wake and bake.
	Monitor_DAV();
}


//XXX sync this with interface binding, below
#if HAVE_UART1
void UART1_TransmitEmpty ( void )
#elif HAVE_USBCDC
void USBCDC_TransmitEmpty ( void )
#endif
{
	//we don't really need this, but here's how you do it
	Monitor_TBMT();
}



//====================================================
//FreeRTOS hooks



void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
	/* Run time stack overflow checking is performed if
	configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
	called if a stack overflow is detected. */
	volatile int i = 0;
	(void)i;
}



void vApplicationMallocFailedHook(void)
{
	/* vApplicationMallocFailedHook() will only be called if
	configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h. It is a hook
	function that will get called if a call to pvPortMalloc() fails.
	pvPortMalloc() is called internally by the kernel whenever a task, queue,
	timer or semaphore is created. It is also called by various parts of the
	demo application. If heap_1.c or heap_2.c are used, then the size of the
	heap available to pvPortMalloc() is defined by configTOTAL_HEAP_SIZE in
	FreeRTOSConfig.h, and the xPortGetFreeHeapSize() API function can be used
	to query the size of free heap space that remains (although it does not
	provide information on how the remaining heap might be fragmented). */
	volatile int i = 0;
	(void)i;
}



//====================================================
//EXTI support


void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	switch ( GPIO_Pin )
	{
		case SP_nLRQ_Pin:	//nLRQ falling edge
			SP0256_GPIO_EXTI_Callback ( GPIO_Pin );
		break;

		default:
			//XXX que?
		break;
	}
}



void HAL_RTC_AlarmAEventCallback(RTC_HandleTypeDef *hrtc)
{
}



/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used 
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void const * argument)
{
    
    
                 
  /* init code for USB_DEVICE */
  MX_USB_DEVICE_Init();

  /* USER CODE BEGIN 5 */

	//crank up serial ports
#if HAVE_UART1
	UART1_Init();	//UART1, alternative monitor
#endif
#if HAVE_USBCDC
	USBCDC_Init();	//CDC == monitor
#endif

	//bind the interfaces to the relevant devices
	//these 'HAVE_xxx' macros are in the preprocessor defs of the project
	//XXX the following logic must be kept in sync with logic up above
#if HAVE_UART1
	//we'll prefer the physical UART if we've defined support for both
	g_pMonitorIOIf = &g_pifUART1;	//monitor is on UART1
#elif HAVE_USBCDC
	g_pMonitorIOIf = &g_pifCDC;		//monitor is on USB CDC
#endif
	//light some lamps on a countdown
	LightLamp ( 1000, &g_lltGn, _ledOnGn );

	//start up worker threads
	__startWorkerTasks();

	//================================================
	//temporary test crap
	{
	volatile size_t nPushed;

#if HAVE_UART1 && defined(DEBUG)
	//the uart1 monitor is for my debugging convenience, but it doesn't have a
	//'client connected' event, so squirt out a string to make it obvious we
	//are live
	g_pifUART1._transmitCompletely ( &g_pifUART1, "Hi, there!\r\n", 12, 1000 );
#endif

	//this must be done to get the PWM output started
	HAL_TIM_Base_Start(&htim3); //Starts the TIM Base generation
	if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3) != HAL_OK)//Starts the PWM signal generation
	{
		/* PWM Generation Error */
		Error_Handler();
	}

	//this is how you set the PWM value.  we have already setup for 0-255.
	//Note that the values in the setup of the timer are +1 values, and
	//maximums.  So to set up for 0-255 you need to set the reload at 254,
	//and to divide by 6, you need to select a prescaler of 5.  As such
	//0 will be low all the time, and 255 will be high all the time.
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 0);
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 1);
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 254);
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 255);
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 127);


	//this must be done to get the sample clock started
	HAL_TIM_Base_Start_IT(&htim4);

	(void) nPushed;
	nPushed = 0;	//(just for breakpoint)
	}

	//================================================
	//continue running this task
	//This task, the 'default' task, was generated by the tool, and it's easier
	//to just keep it than to fight the tool to destroy it (though some of that
	//fighting can be made a little easier if it was dynamically allocated,
	//then just exited).  We repurpose it after init to handle the lamps and
	//periodically sample performance data (useful for tuning pre-release).

	//Infinite loop
	uint32_t msWait = 1000;
	for(;;)
	{
		//wait on various task notifications
		uint32_t ulNotificationValue;
		BaseType_t xResult = xTaskNotifyWait( pdFALSE,	//Don't clear bits on entry.
				0xffffffff,	//Clear all bits on exit.
				&ulNotificationValue,	//Stores the notified value.
				pdMS_TO_TICKS(msWait) );
		if( xResult == pdPASS )
		{
			//the lights have changed
			if ( ulNotificationValue & TNB_LIGHTSCHANGED )
			{
				//XXX
			}
		}
		else	//timeout on wait
		{
			//XXX things to do on periodic idle timeout
		}
		
#ifdef DEBUG
		//XXX these are to tune the freertos heap size; if we have a heap
#if USE_FREERTOS_HEAP_IMPL
		g_nHeapFree = xPortGetFreeHeapSize();
		g_nMinEverHeapFree = xPortGetMinimumEverFreeHeapSize();
#else
		g_nMinEverHeapFree = (char*)platform_get_last_free_ram( 0 ) - (char*)platform_get_first_free_ram( 0 );
#endif
#if HAVE_UART1
		g_nMaxMonTxQueue = UART1_txbuff_max();
		g_nMaxMonRxQueue = UART1_rxbuff_max();
#endif
#if HAVE_USBCDC
		g_nMaxMonTxQueue = CDC_txbuff_max();
		g_nMaxMonRxQueue = CDC_rxbuff_max();
#endif
		g_nMaxSP0256Queue = SP0256_queue_max();
		//free stack space measurements
		g_nMinStackFreeDefault = uxTaskGetStackHighWaterMark ( defaultTaskHandle );
		g_nMinStackFreeMonitor = uxTaskGetStackHighWaterMark ( g_thMonitor );
		g_nMinStackFreeSP0256 = uxTaskGetStackHighWaterMark ( g_thSP0256 );
		//XXX others
#endif
		
		//turn out the lights, the party's over
		uint32_t now = HAL_GetTick();
		uint32_t remMin = 0xffffffff;	//nothing yet
		ProcessLightOffTime ( now, &remMin, &g_lltGn, _ledOffGn );

		//don't wait longer than 3 sec
		if ( remMin > 3000 )
			remMin = 3000;
		
		msWait = remMin;
	}

  /* USER CODE END 5 */ 
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM2 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM2) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

	else if (htim->Instance == TIM4)
	{
		//XXX this will eventually become our sample clock
		HAL_GPIO_TogglePin (TWIGGLE_GPIO_Port, TWIGGLE_Pin);
	}

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
	volatile int i = 0;
	(void)i;
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{ 
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
	volatile int i = 0;
	(void)i;
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
