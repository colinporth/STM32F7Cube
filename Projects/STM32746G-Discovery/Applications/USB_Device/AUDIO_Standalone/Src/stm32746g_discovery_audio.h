#pragma once
#include "wm8994.h"
#include "stm32746g_discovery.h"
//{{{
#ifdef __cplusplus
 extern "C" {
#endif
//}}}

// CODEC_SLOT0 Left | CODEC_SLOT1 Left | CODEC_SLOT0 Right  | CODEC_SLOT1 Right |

/* SAI OUT peripheral configuration defines */
#define AUDIO_OUT_SAIx                           SAI2_Block_A
#define AUDIO_OUT_SAIx_CLK_ENABLE()              __HAL_RCC_SAI2_CLK_ENABLE()
#define AUDIO_OUT_SAIx_CLK_DISABLE()             __HAL_RCC_SAI2_CLK_DISABLE()
#define AUDIO_OUT_SAIx_SCK_AF                    GPIO_AF10_SAI2
#define AUDIO_OUT_SAIx_FS_SD_MCLK_AF             GPIO_AF10_SAI2

#define AUDIO_OUT_SAIx_MCLK_ENABLE()             __HAL_RCC_GPIOI_CLK_ENABLE()
#define AUDIO_OUT_SAIx_MCLK_GPIO_PORT            GPIOI
#define AUDIO_OUT_SAIx_MCLK_PIN                  GPIO_PIN_4
#define AUDIO_OUT_SAIx_SCK_SD_ENABLE()           __HAL_RCC_GPIOI_CLK_ENABLE()
#define AUDIO_OUT_SAIx_SCK_SD_GPIO_PORT          GPIOI
#define AUDIO_OUT_SAIx_SCK_PIN                   GPIO_PIN_5
#define AUDIO_OUT_SAIx_SD_PIN                    GPIO_PIN_6
#define AUDIO_OUT_SAIx_FS_ENABLE()               __HAL_RCC_GPIOI_CLK_ENABLE()
#define AUDIO_OUT_SAIx_FS_GPIO_PORT              GPIOI
#define AUDIO_OUT_SAIx_FS_PIN                    GPIO_PIN_7

/* SAI DMA Stream definitions */
#define AUDIO_OUT_SAIx_DMAx_CLK_ENABLE()         __HAL_RCC_DMA2_CLK_ENABLE()
#define AUDIO_OUT_SAIx_DMAx_STREAM               DMA2_Stream4
#define AUDIO_OUT_SAIx_DMAx_CHANNEL              DMA_CHANNEL_3
#define AUDIO_OUT_SAIx_DMAx_IRQ                  DMA2_Stream4_IRQn
#define AUDIO_OUT_SAIx_DMAx_PERIPH_DATA_SIZE     DMA_PDATAALIGN_HALFWORD
#define AUDIO_OUT_SAIx_DMAx_MEM_DATA_SIZE        DMA_MDATAALIGN_HALFWORD
#define DMA_MAX_SZE                              ((uint16_t)0xFFFF)

#define AUDIO_OUT_SAIx_DMAx_IRQHandler           DMA2_Stream4_IRQHandler

/* Select the interrupt preemption priority for the DMA interrupt */
#define AUDIO_OUT_IRQ_PREPRIO                    ((uint32_t)0x0E)   /* Select the preemption priority level(0 is the highest) */

/* SAI IN peripheral configuration defines */
#define AUDIO_IN_SAIx                            SAI2_Block_B
#define AUDIO_IN_SAIx_CLK_ENABLE()               __HAL_RCC_SAI2_CLK_ENABLE()
#define AUDIO_IN_SAIx_CLK_DISABLE()              __HAL_RCC_SAI2_CLK_DISABLE()
#define AUDIO_IN_SAIx_SD_AF                      GPIO_AF10_SAI2

#define AUDIO_IN_SAIx_SD_ENABLE()                __HAL_RCC_GPIOG_CLK_ENABLE()
#define AUDIO_IN_SAIx_SD_GPIO_PORT               GPIOG
#define AUDIO_IN_SAIx_SD_PIN                     GPIO_PIN_10

#define AUDIO_IN_INT_GPIO_ENABLE()               __HAL_RCC_GPIOH_CLK_ENABLE()
#define AUDIO_IN_INT_GPIO_PORT                   GPIOH
#define AUDIO_IN_INT_GPIO_PIN                    GPIO_PIN_15
#define AUDIO_IN_INT_IRQ                         EXTI15_10_IRQn

/* SAI DMA Stream definitions */
#define AUDIO_IN_SAIx_DMAx_CLK_ENABLE()          __HAL_RCC_DMA2_CLK_ENABLE()
#define AUDIO_IN_SAIx_DMAx_STREAM                DMA2_Stream7
#define AUDIO_IN_SAIx_DMAx_CHANNEL               DMA_CHANNEL_0
#define AUDIO_IN_SAIx_DMAx_IRQ                   DMA2_Stream7_IRQn
#define AUDIO_IN_SAIx_DMAx_PERIPH_DATA_SIZE      DMA_PDATAALIGN_HALFWORD
#define AUDIO_IN_SAIx_DMAx_MEM_DATA_SIZE         DMA_MDATAALIGN_HALFWORD

#define AUDIO_IN_SAIx_DMAx_IRQHandler            DMA2_Stream7_IRQHandler
#define AUDIO_IN_INT_IRQHandler                  EXTI15_10_IRQHandler

/* Select the interrupt preemption priority and subpriority for the IT/DMA interrupt */
#define AUDIO_IN_IRQ_PREPRIO                     ((uint32_t)0x0F)   /* Select the preemption priority level(0 is the highest) */

#define AUDIODATA_SIZE                      ((uint16_t)2)   /* 16-bits audio data size */

/* Audio status definition */
#define AUDIO_OK                            ((uint8_t)0)
#define AUDIO_ERROR                         ((uint8_t)1)
#define AUDIO_TIMEOUT                       ((uint8_t)2)

/* AudioFreq * DataSize (2 bytes) * NumChannels (Stereo: 2) */
#define DEFAULT_AUDIO_IN_FREQ               I2S_AUDIOFREQ_16K
#define DEFAULT_AUDIO_IN_BIT_RESOLUTION     ((uint8_t)16)
#define DEFAULT_AUDIO_IN_CHANNEL_NBR        ((uint8_t)2) /* Mono = 1, Stereo = 2 */
#define DEFAULT_AUDIO_IN_VOLUME             ((uint16_t)64)

/* Delay for the Codec to be correctly reset */
#define CODEC_RESET_DELAY                   ((uint8_t)5)

/* Alias on existing output devices to adapt for 2 headphones output */
#define OUTPUT_DEVICE_HEADPHONE1 OUTPUT_DEVICE_HEADPHONE
#define OUTPUT_DEVICE_HEADPHONE2 OUTPUT_DEVICE_SPEAKER /* Headphone2 is connected to Speaker output of the wm8994 */

extern __IO uint16_t AudioInVolume;

#define DMA_MAX(x)  (((x) <= DMA_MAX_SZE)? (x):DMA_MAX_SZE)

uint8_t BSP_AUDIO_OUT_Init(uint16_t OutputDevice, uint8_t Volume, uint32_t AudioFreq);
uint8_t BSP_AUDIO_OUT_Play(uint16_t* pBuffer, uint32_t Size);
void    BSP_AUDIO_OUT_ChangeBuffer(uint16_t *pData, uint16_t Size);
uint8_t BSP_AUDIO_OUT_Pause(void);
uint8_t BSP_AUDIO_OUT_Resume(void);
uint8_t BSP_AUDIO_OUT_Stop(uint32_t Option);
uint8_t BSP_AUDIO_OUT_SetVolume(uint8_t Volume);
void    BSP_AUDIO_OUT_SetFrequency(uint32_t AudioFreq);
void    BSP_AUDIO_OUT_SetAudioFrameSlot(uint32_t AudioFrameSlot);
uint8_t BSP_AUDIO_OUT_SetMute(uint32_t Cmd);
uint8_t BSP_AUDIO_OUT_SetOutputMode(uint8_t Output);
void    BSP_AUDIO_OUT_DeInit(void);

void BSP_AUDIO_OUT_TransferComplete_CallBack(void);
void BSP_AUDIO_OUT_HalfTransfer_CallBack(void);
void BSP_AUDIO_OUT_Error_CallBack(void);

void  BSP_AUDIO_OUT_ClockConfig(SAI_HandleTypeDef *hsai, uint32_t AudioFreq, void *Params);
void  BSP_AUDIO_OUT_MspInit(SAI_HandleTypeDef *hsai, void *Params);
void  BSP_AUDIO_OUT_MspDeInit(SAI_HandleTypeDef *hsai, void *Params);

uint8_t BSP_AUDIO_IN_Init(uint32_t AudioFreq, uint32_t BitRes, uint32_t ChnlNbr);
uint8_t BSP_AUDIO_IN_InitEx(uint16_t InputDevice, uint32_t AudioFreq, uint32_t BitRes, uint32_t ChnlNbr);
uint8_t BSP_AUDIO_IN_OUT_Init(uint16_t InputDevice, uint16_t OutputDevice, uint32_t AudioFreq, uint32_t BitRes, uint32_t ChnlNbr);
uint8_t BSP_AUDIO_IN_Record(uint16_t *pData, uint32_t Size);
uint8_t BSP_AUDIO_IN_Stop(uint32_t Option);
uint8_t BSP_AUDIO_IN_Pause(void);
uint8_t BSP_AUDIO_IN_Resume(void);
uint8_t BSP_AUDIO_IN_SetVolume(uint8_t Volume);
void    BSP_AUDIO_IN_DeInit(void);
void    BSP_AUDIO_IN_TransferComplete_CallBack(void);
void    BSP_AUDIO_IN_HalfTransfer_CallBack(void);
void    BSP_AUDIO_IN_Error_CallBack(void);

void  BSP_AUDIO_IN_MspInit(SAI_HandleTypeDef *hsai, void *Params);
void  BSP_AUDIO_IN_MspDeInit(SAI_HandleTypeDef *hsai, void *Params);

//{{{
#ifdef __cplusplus
  }
#endif
//}}}
