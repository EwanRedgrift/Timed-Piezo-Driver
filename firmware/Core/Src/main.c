/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Random 3-voice buzzer song player for STM32L031.
  *                   Plays a random song, then sleeps in STOP mode until the
  *                   RTC wake-up timer fires. The sleep time is selected by
  *                   the SWITCH_Input switch (PA7).
  ******************************************************************************
  * @attention
  *
  * See the LICENSE file in the root of this project.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "notes.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

typedef struct {
  uint16_t freq_v1;     /* Voice 1 (PA1), Hz, 0 = rest */
  uint16_t freq_shared; /* Voices 2 & 3 (PA2 & PA3), Hz, 0 = rest */
  uint16_t duration;    /* Note length in milliseconds */
} SongStep;

typedef struct {
  const SongStep *steps;
  uint32_t length;      /* Number of steps */
} Song;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/*
 * Time to sleep between songs, in seconds.
 * Change these two values to change the delay.
 */
#define SLEEP_LONG_SECONDS    (60U * 60U) /* Switch in the "long" position  */
#define SLEEP_SHORT_SECONDS   (10U * 60U)  /* Switch in the "short" position */

/* Pin level that selects the long sleep time (GPIO_PIN_SET or GPIO_PIN_RESET) */
#define SWITCH_LONG_STATE     GPIO_PIN_SET

/* Fallback pin definitions if SWITCH_Input is not labelled in CubeMX/main.h */
#ifndef SWITCH_Input_Pin
#define SWITCH_Input_Pin        GPIO_PIN_7
#define SWITCH_Input_GPIO_Port  GPIOA
#endif

/*
 * RTC prescalers. The L031 LSI is nominally 38 kHz, so
 * 38000 / ((124 + 1) * (303 + 1)) = 1 Hz. LSI is not very accurate
 * (roughly +/-30%), so sleep times are approximate.
 */
#define RTC_ASYNCH_PREDIV     124U
#define RTC_SYNCH_PREDIV      303U

#define NOTE_GAP_MS           20U /* Short silence between notes so they don't blur */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
#define ARRAY_SIZE(x)         (sizeof(x) / sizeof((x)[0]))
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
RTC_HandleTypeDef hrtc;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim21;
TIM_HandleTypeDef htim22; /* Not used, but referenced by TIM22_IRQHandler in stm32l0xx_it.c */

/* USER CODE BEGIN PV */
static uint32_t random_state = 1;

static const SongStep solo_future[] = {
  { NOTE_G4,  NOTE_G4,  500  },
  { NOTE_F4,  NOTE_F4,  800  },
  { NOTE_F4,  NOTE_C5,  250  },
  { NOTE_DS4, NOTE_DS4, 250  },
  { NOTE_G4,  NOTE_G4,  500  },
  { NOTE_F4,  NOTE_F4,  800  },
  { NOTE_F4,  NOTE_C5,  250  },
  { NOTE_DS4, NOTE_DS4, 250  },
  { NOTE_G4,  NOTE_G4,  500  },
  { NOTE_GS4, NOTE_GS4, 800  },
  { NOTE_GS4, NOTE_C5,  250  },
  { NOTE_DS4, NOTE_DS4, 250  },
  { NOTE_G4,  NOTE_G4,  1300 },
  { NOTE_G4,  NOTE_C5,  250  },
  { NOTE_DS4, NOTE_DS4, 250  },
};

static const SongStep rick_ross_stay_schemin[] = {
  /* First phrase (G minor) */
  { NOTE_G3,  NOTE_G4,  167  },
  { NOTE_G3,  NOTE_AS4, 167  },
  { NOTE_G3,  NOTE_F4,  167  },
  { NOTE_G3,  NOTE_D4,  500  },
  { NOTE_G3,  NOTE_F4,  667  },
  { NOTE_G3,  NOTE_A4,  1000 },

  /* Second phrase (Bb major) */
  { NOTE_AS3, NOTE_G4,  167  },
  { NOTE_AS3, NOTE_AS4, 167  },
  { NOTE_AS3, NOTE_F4,  167  },
  { NOTE_AS3, NOTE_D4,  500  },
  { NOTE_AS3, NOTE_F4,  667  },
  { NOTE_AS3, NOTE_A4,  667  },
  { NOTE_AS3, NOTE_AS4, 333  },

  /* Third section, beats 9-13 (F minor chord) */
  { NOTE_F3,  NOTE_G4,  167  },
  { NOTE_F3,  NOTE_AS4, 167  },
  { NOTE_F3,  NOTE_F4,  167  },
  { NOTE_F3,  NOTE_D4,  500  },
  { NOTE_F3,  NOTE_F4,  667  },
  { NOTE_F3,  NOTE_A4,  1000 },

  /* Fourth section, beats 13-17 (C and D chords) */
  { NOTE_C3,  NOTE_G4,  167  },
  { NOTE_C3,  NOTE_AS4, 167  },
  { NOTE_C3,  NOTE_F4,  167  },
  { NOTE_C3,  NOTE_D4,  500  },
  { NOTE_C3,  NOTE_F4,  667  },
  { NOTE_D3,  NOTE_A4,  667  },
  { NOTE_D3,  NOTE_AS4, 333  },
};

/* All songs available to the random player. Add new songs here. */
static const Song songs[] = {
  { solo_future,            ARRAY_SIZE(solo_future)            },
  { rick_ross_stay_schemin, ARRAY_SIZE(rick_ross_stay_schemin) },
};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_TIM2_Init(void);
static void MX_TIM21_Init(void);
static void MX_RTC_Init(void);
/* USER CODE BEGIN PFP */
static void Play_Tone(TIM_HandleTypeDef *htim, uint32_t channel, uint16_t freq);
static void Play_Shared_Tone(uint16_t freq);
static void Play_Custom_Song(const Song *song);
static uint32_t Random_Number(void);
static void Play_Random_Song(void);
static uint32_t Get_Sleep_Seconds(void);
static void Enter_Stop_Mode(uint32_t seconds);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
  * @brief  Set the frequency of one PWM channel (50% duty cycle).
  * @param  htim    Timer handle
  * @param  channel TIM_CHANNEL_x
  * @param  freq    Frequency in Hz, 0 silences the channel
  */
static void Play_Tone(TIM_HandleTypeDef *htim, uint32_t channel, uint16_t freq)
{
  if (freq == 0)
  {
    __HAL_TIM_SET_COMPARE(htim, channel, 0);
    return;
  }

  uint32_t timer_freq = SystemCoreClock / (htim->Init.Prescaler + 1);
  uint32_t arr = (timer_freq / freq) - 1;

  __HAL_TIM_SET_AUTORELOAD(htim, arr);
  __HAL_TIM_SET_COMPARE(htim, channel, (arr + 1) / 2);
}

/**
  * @brief  Set the frequency of buzzers 2 & 3 (both share TIM21).
  * @param  freq Frequency in Hz, 0 silences both channels
  */
static void Play_Shared_Tone(uint16_t freq)
{
  if (freq == 0)
  {
    __HAL_TIM_SET_COMPARE(&htim21, TIM_CHANNEL_1, 0);
    __HAL_TIM_SET_COMPARE(&htim21, TIM_CHANNEL_2, 0);
    return;
  }

  uint32_t timer_freq = SystemCoreClock / (htim21.Init.Prescaler + 1);
  uint32_t arr = (timer_freq / freq) - 1;

  __HAL_TIM_SET_AUTORELOAD(&htim21, arr);
  __HAL_TIM_SET_COMPARE(&htim21, TIM_CHANNEL_1, (arr + 1) / 2);
  __HAL_TIM_SET_COMPARE(&htim21, TIM_CHANNEL_2, (arr + 1) / 2);
}

/**
  * @brief  Play a song (blocking).
  * @param  song Song to play
  */
static void Play_Custom_Song(const Song *song)
{
  for (uint32_t i = 0; i < song->length; i++)
  {
    Play_Tone(&htim2, TIM_CHANNEL_2, song->steps[i].freq_v1);
    Play_Shared_Tone(song->steps[i].freq_shared);

    HAL_Delay(song->steps[i].duration);

    Play_Tone(&htim2, TIM_CHANNEL_2, 0);
    Play_Shared_Tone(0);
    HAL_Delay(NOTE_GAP_MS);
  }
}

/**
  * @brief  Xorshift32 pseudo-random number generator.
  */
static uint32_t Random_Number(void)
{
  random_state ^= random_state << 13;
  random_state ^= random_state >> 17;
  random_state ^= random_state << 5;

  if (random_state == 0)
  {
    random_state = 1;
  }

  return random_state;
}

/**
  * @brief  Pick one of the songs at random and play it (blocking).
  */
static void Play_Random_Song(void)
{
  Play_Custom_Song(&songs[Random_Number() % ARRAY_SIZE(songs)]);
}

/**
  * @brief  Read the sleep-time switch.
  * @retval Sleep time in seconds
  */
static uint32_t Get_Sleep_Seconds(void)
{
  if (HAL_GPIO_ReadPin(SWITCH_Input_GPIO_Port, SWITCH_Input_Pin) == SWITCH_LONG_STATE)
  {
    return SLEEP_LONG_SECONDS;
  }

  return SLEEP_SHORT_SECONDS;
}

/**
  * @brief  Sleep in STOP mode until the RTC wake-up timer expires.
  * @param  seconds Approximate sleep time (1 to 65536)
  */
static void Enter_Stop_Mode(uint32_t seconds)
{
  if (seconds < 1U)
  {
    seconds = 1U;
  }

  /* CK_SPRE is ~1 Hz, the counter counts (value + 1) ticks */
  if (HAL_RTCEx_SetWakeUpTimer_IT(&hrtc, seconds - 1U,
                                  RTC_WAKEUPCLOCK_CK_SPRE_16BITS) != HAL_OK)
  {
    Error_Handler();
  }

  /* Stop SysTick so it doesn't wake the MCU */
  HAL_SuspendTick();

  HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);

  /* Execution resumes here after the RTC wakes the MCU */
  HAL_ResumeTick();

  /* Clocks are reset on STOP exit, restore the configuration */
  SystemClock_Config();

  HAL_RTCEx_DeactivateWakeUpTimer(&hrtc);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM2_Init();
  MX_TIM21_Init();
  MX_RTC_Init();
  /* USER CODE BEGIN 2 */

  /* Start PWM on all 3 buzzer channels */
  if (HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Start(&htim21, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Start(&htim21, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }

  /*
   * Seed the pseudo-random generator. These values are fairly similar on
   * every power-up, so the first song may often be the same.
   */
  random_state = HAL_GetTick() ^ RTC->SSR ^ SysTick->VAL;
  if (random_state == 0)
  {
    random_state = 1;
  }

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    Play_Random_Song();

    /* The switch is read once per cycle, right before sleeping */
    Enter_Stop_Mode(Get_Sleep_Seconds());
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_5;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                              | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_MSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
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

  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /* Enable access to the backup domain */
  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();

  /** Enable the internal low-speed oscillator (LSI)
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Select LSI as the RTC clock source
  */
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC;
  PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSI;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = RTC_ASYNCH_PREDIV;
  hrtc.Init.SynchPrediv = RTC_SYNCH_PREDIV;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN RTC_Init 2 */

  /* RTC wake-up interrupt, needed to leave STOP mode */
  HAL_NVIC_SetPriority(RTC_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(RTC_IRQn);

  /* USER CODE END RTC_Init 2 */
}

/**
  * @brief TIM2 Initialization Function (PA1 - Buzzer 1)
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{
  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 15;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 65535;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  if (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */
  HAL_TIM_MspPostInit(&htim2);
}

/**
  * @brief TIM21 Initialization Function (PA2 & PA3 - Buzzers 2 & 3, shared)
  * @param None
  * @retval None
  */
static void MX_TIM21_Init(void)
{
  /* USER CODE BEGIN TIM21_Init 0 */

  /* USER CODE END TIM21_Init 0 */

  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM21_Init 1 */

  /* USER CODE END TIM21_Init 1 */
  htim21.Instance = TIM21;
  htim21.Init.Prescaler = 15;
  htim21.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim21.Init.Period = 65535;
  htim21.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim21.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim21) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim21) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  if (HAL_TIM_PWM_ConfigChannel(&htim21, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_ConfigChannel(&htim21, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM21_Init 2 */

  /* USER CODE END TIM21_Init 2 */
  HAL_TIM_MspPostInit(&htim21);
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin : SWITCH_Input_Pin (PA7) */
  GPIO_InitStruct.Pin = SWITCH_Input_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(SWITCH_Input_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
