// main.c
char* kVersion = "USB audio 18/2/18";
//{{{  includes
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "stm32f7xx_hal.h"

#include "usbd.h"

#include "stm32746g_discovery_lcd.h"
#include "stm32746g_discovery_ts.h"
#include "stm32746g_discovery_audio.h"
//}}}
static TS_StateTypeDef gTsState;
static USBD_HandleTypeDef gUsbDevice;
static PCD_HandleTypeDef gPcdHandle;
//{{{  interrupt, system handlers
void NMI_Handler() {}
void SVC_Handler() {}
void PendSV_Handler() {}
void DebugMon_Handler() {}

void BusFault_Handler() { while (1) {} }
void HardFault_Handler() { while (1) {} }
void MemManage_Handler() { while (1) {} }
void UsageFault_Handler() { while (1) {} }

void SysTick_Handler() { HAL_IncTick(); }

void OTG_FS_IRQHandler() { HAL_PCD_IRQHandler (&gPcdHandle); }
void OTG_HS_IRQHandler() { HAL_PCD_IRQHandler (&gPcdHandle); }

extern SAI_HandleTypeDef haudio_out_sai;
void DMA2_Stream4_IRQHandler() { HAL_DMA_IRQHandler (haudio_out_sai.hdmatx); }
//}}}
//{{{  global debug vars
#define DEBUG_DISPLAY_LINES 16
#define DEBUG_MAX_LINES     1000

static int gTick = 0;
static int gLayer = 1;

static uint16_t gDebugLine = 0;
static char* gDebugStr[DEBUG_MAX_LINES];
static uint32_t gDebugTicks[DEBUG_MAX_LINES];
static uint32_t gDebugColour[DEBUG_MAX_LINES];

enum eHit { eReleased, eProx, ePressed, eScroll };
static enum eHit gHit = eReleased;
static int gHitX = 0;
static int gHitY = 0;
static int gLastX = 0;
static int gLastY = 0;

static int gScroll = 0;
//}}}
//{{{
static int getScrollScale() {
  return BSP_LCD_GetFont()->Height / 3;
  }
//}}}
//{{{
static int getScrollLines() {
  return gScroll / getScrollScale();
  }
//}}}
//{{{
static void setScrollValue (int scroll) {

  gScroll = scroll;

  if (scroll < 0)
    gScroll = 0;
  else if (getScrollLines() >  gDebugLine - DEBUG_DISPLAY_LINES)
    gScroll = (gDebugLine - DEBUG_DISPLAY_LINES) * getScrollScale();
  }
//}}}

//{{{  packet defines
#define CHANNELS              4
#define SAMPLE_RATE           48000
#define BYTES_PER_SAMPLE      2

#define PACKETS_PER_SECOND    1000
#define PACKETS               8
#define PACKET_SAMPLES        (SAMPLE_RATE / PACKETS_PER_SECOND)
#define PACKET_SIZE           (CHANNELS * PACKET_SAMPLES * BYTES_PER_SAMPLE)

#define SLOTS                 4
#define SLOTS_PACKET_SIZE     (SLOTS * BYTES_PER_SAMPLE * PACKET_SAMPLES)
#define SLOTS_PACKET_BUF_SIZE (PACKETS * SLOTS_PACKET_SIZE)
//}}}
//{{{  global waveform vars
// waveform
static uint16_t gPackets = 0;
static uint16_t gSample = 0;
static uint16_t gCentreX = 0;
static uint16_t gCentreY = 0;
static unsigned sumFL = 0;
static unsigned sumFR = 0;
static unsigned sumRL = 0;
static unsigned sumRR = 0;
static uint16_t gFL[240];
static uint16_t gFR[240];
static uint16_t gRL[240];
static uint16_t gRR[240];
//}}}
static int gFaster = 1;
static int writePtrOnRead = 0;

//{{{
static void initLcd() {

  BSP_LCD_Init();

  BSP_LCD_LayerDefaultInit (0, LCD_FB_START_ADDRESS);
  BSP_LCD_SelectLayer(0);
  BSP_LCD_SetBackColor (LCD_COLOR_BLACK);
  BSP_LCD_SetTextColor (LCD_COLOR_WHITE);
  BSP_LCD_Clear (LCD_COLOR_BLACK);
  BSP_LCD_SetFont (&Font16);
  BSP_LCD_SetLayerVisible (0, ENABLE);
  BSP_LCD_SetTransparency (0, 255);

  BSP_LCD_LayerDefaultInit (1, LCD_FB_START_ADDRESS + (BSP_LCD_GetXSize() * BSP_LCD_GetYSize() * 4));
  BSP_LCD_SelectLayer(1);
  BSP_LCD_SetBackColor (LCD_COLOR_BLACK);
  BSP_LCD_SetTextColor (LCD_COLOR_WHITE);
  BSP_LCD_Clear (LCD_COLOR_BLACK);
  BSP_LCD_SetFont (&Font16);
  BSP_LCD_SetLayerVisible (1, ENABLE);
  BSP_LCD_SetTransparency (1, 0);

  BSP_LCD_DisplayOn();

  for (int i = 0; i < DEBUG_MAX_LINES; i++)
    gDebugStr[i] = NULL;
  }
//}}}
//{{{
static void showLcd() {

  BSP_LCD_SelectLayer (gLayer);
  BSP_LCD_Clear (LCD_COLOR_BLACK);

  uint32_t wait = 20 - (HAL_GetTick() % 20);
  HAL_Delay (wait);
  gTick = HAL_GetTick();

  char str1[40];
  sprintf (str1, "%s %s %d %d %d", kVersion, gFaster ? "fast" : "slow", writePtrOnRead, (int)gTick, (int)wait);
  BSP_LCD_SetTextColor (gFaster ? LCD_COLOR_WHITE : LCD_COLOR_YELLOW);
  BSP_LCD_DisplayStringAtLine (0, str1);

  if (!BSP_PB_GetState (BUTTON_KEY)) {
    for (int displayLine = 0; (displayLine < gDebugLine) && (displayLine < DEBUG_DISPLAY_LINES); displayLine++) {
      int debugLine = (gDebugLine <= DEBUG_DISPLAY_LINES) ?
                        displayLine : gDebugLine - DEBUG_DISPLAY_LINES + displayLine - getScrollLines();
      debugLine = debugLine % DEBUG_MAX_LINES;
      BSP_LCD_SetTextColor (LCD_COLOR_WHITE);
      char tickStr[20];
      sprintf (tickStr, "%2d.%03d", (int)gDebugTicks[debugLine] / 1000, (int)gDebugTicks[debugLine] % 1000);
      BSP_LCD_DisplayStringAtLineColumn (1+displayLine, 0, tickStr);
      BSP_LCD_SetTextColor (gDebugColour[debugLine]);
      BSP_LCD_DisplayStringAtLineColumn (1+displayLine, 7, gDebugStr[debugLine]);
      }
    }

  BSP_LCD_SetTextColor (LCD_COLOR_GREEN);
  uint16_t sample = gSample;
  for (int i = 0; i < gCentreX; i++) {
    sample = (sample + 1) % gCentreX;
    BSP_LCD_FillRect (i*2, gCentreY - gFL[sample], 2, gFL[sample]+gFR[sample]);
    }

  for (int i = 0; i < gTsState.touchDetected; i++) {
    BSP_LCD_SetTextColor (LCD_COLOR_YELLOW);
    BSP_LCD_FillCircle (gTsState.touchX[i], gTsState.touchY[i], 10);
    }

  BSP_LCD_SetTransparency (gLayer, 255);
  gLayer = !gLayer;
  BSP_LCD_SetTransparency (gLayer, 0);
  }
//}}}
//{{{
static void debug (uint32_t colour, const char* format, ... ) {

  gDebugColour[gDebugLine] = colour;
  gDebugTicks[gDebugLine] = HAL_GetTick();

  va_list args;
  va_start (args, format);
  free (gDebugStr[gDebugLine]);
  gDebugStr[gDebugLine] = (char*)malloc (vsnprintf (NULL, 0, format, args) + 1);
  vsnprintf (gDebugStr[gDebugLine], 40, format, args);
  va_end (args);

  gDebugLine = (gDebugLine+1) % DEBUG_MAX_LINES;
  }
//}}}

//{{{  usbd pcd handler
//{{{
void HAL_PCD_MspInit (PCD_HandleTypeDef* pcdHandle) {

  GPIO_InitTypeDef GPIO_InitStruct;

  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* Configure DM DP Pins */
  GPIO_InitStruct.Pin = (GPIO_PIN_11 | GPIO_PIN_12);
  GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF10_OTG_FS;
  HAL_GPIO_Init (GPIOA, &GPIO_InitStruct);

  /* Enable USB FS Clock */
  __HAL_RCC_USB_OTG_FS_CLK_ENABLE();

  /* Set USBFS Interrupt priority */
  HAL_NVIC_SetPriority (OTG_FS_IRQn, 5, 0);

  /* Enable USBFS Interrupt */
  HAL_NVIC_EnableIRQ (OTG_FS_IRQn);
  }
//}}}
//{{{
void HAL_PCD_MspDeInit (PCD_HandleTypeDef* pcdHandle) {

  /* Disable USB FS Clock */
  __HAL_RCC_USB_OTG_FS_CLK_DISABLE();
  __HAL_RCC_SYSCFG_CLK_DISABLE();
  }
//}}}
//{{{
void HAL_PCD_SetupStageCallback (PCD_HandleTypeDef* pcdHandle) {
  USBD_LL_SetupStage(pcdHandle->pData, (uint8_t*)pcdHandle->Setup);
  }
//}}}
//{{{
void HAL_PCD_DataOutStageCallback (PCD_HandleTypeDef* pcdHandle, uint8_t epnum) {
  USBD_LL_DataOutStage (pcdHandle->pData, epnum, pcdHandle->OUT_ep[epnum].xfer_buff);
  }
//}}}
//{{{
void HAL_PCD_DataInStageCallback (PCD_HandleTypeDef* pcdHandle, uint8_t epnum) {
  USBD_LL_DataInStage (pcdHandle->pData, epnum, pcdHandle->IN_ep[epnum].xfer_buff);
  }
//}}}
//{{{
void HAL_PCD_SOFCallback (PCD_HandleTypeDef* pcdHandle) {
  USBD_LL_SOF (pcdHandle->pData);
  }
//}}}
//{{{
void HAL_PCD_ResetCallback (PCD_HandleTypeDef* pcdHandle) {

  USBD_SpeedTypeDef speed = USBD_SPEED_FULL;

  /* Set USB Current Speed */
  switch (pcdHandle->Init.speed) {
    case PCD_SPEED_HIGH: speed = USBD_SPEED_HIGH; break;
    case PCD_SPEED_FULL: speed = USBD_SPEED_FULL; break;
    default: speed = USBD_SPEED_FULL; break;
    }

  /* Reset Device */
  USBD_LL_Reset (pcdHandle->pData);
  USBD_LL_SetSpeed (pcdHandle->pData, speed);
  }
//}}}
//{{{
void HAL_PCD_SuspendCallback (PCD_HandleTypeDef* pcdHandle) {
  USBD_LL_Suspend (pcdHandle->pData);
  }
//}}}
//{{{
void HAL_PCD_ResumeCallback (PCD_HandleTypeDef* pcdHandle) {
  USBD_LL_Resume (pcdHandle->pData);
  }
//}}}
//{{{
void HAL_PCD_ConnectCallback (PCD_HandleTypeDef* pcdHandle) {
  USBD_LL_DevConnected (pcdHandle->pData);
  }
//}}}
//{{{
void HAL_PCD_DisconnectCallback (PCD_HandleTypeDef* pcdHandle) {
  USBD_LL_DevDisconnected (pcdHandle->pData);
  }
//}}}
//{{{
void HAL_PCD_ISOOUTIncompleteCallback (PCD_HandleTypeDef* pcdHandle, uint8_t epnum) {
  USBD_LL_IsoOUTIncomplete (pcdHandle->pData, epnum);
  }
//}}}
//{{{
void HAL_PCD_ISOINIncompleteCallback (PCD_HandleTypeDef* pcdHandle, uint8_t epnum) {
  USBD_LL_IsoINIncomplete (pcdHandle->pData, epnum);
  }
//}}}

//{{{
USBD_StatusTypeDef USBD_LL_Init (USBD_HandleTypeDef* device) {

  /* Set LL Driver parameters */
  gPcdHandle.Instance = USB_OTG_FS;
  gPcdHandle.Init.dev_endpoints = 4;
  gPcdHandle.Init.use_dedicated_ep1 = 0;
  gPcdHandle.Init.ep0_mps = 0x40;
  gPcdHandle.Init.dma_enable = 0;
  gPcdHandle.Init.low_power_enable = 0;
  gPcdHandle.Init.phy_itface = PCD_PHY_EMBEDDED;
  gPcdHandle.Init.Sof_enable = 0;
  gPcdHandle.Init.speed = PCD_SPEED_FULL;
  gPcdHandle.Init.vbus_sensing_enable = 0;
  gPcdHandle.Init.lpm_enable = 0;

  /* Link The driver to the stack */
  gPcdHandle.pData = device;
  device->pData = &gPcdHandle;

  /* Initialize LL Driver */
  HAL_PCD_Init (&gPcdHandle);
  HAL_PCDEx_SetRxFiFo (&gPcdHandle, 0x80);
  HAL_PCDEx_SetTxFiFo (&gPcdHandle, 0, 0x60);

  return USBD_OK;
  }
//}}}
//{{{
USBD_StatusTypeDef USBD_LL_DeInit (USBD_HandleTypeDef* device) {
  HAL_PCD_DeInit (device->pData);
  return USBD_OK;
  }
//}}}
//{{{
USBD_StatusTypeDef USBD_LL_Start (USBD_HandleTypeDef* device) {
  HAL_PCD_Start (device->pData);
  return USBD_OK;
  }
//}}}
//{{{
USBD_StatusTypeDef USBD_LL_Stop (USBD_HandleTypeDef* device) {
  HAL_PCD_Stop (device->pData);
  return USBD_OK;
  }
//}}}
//{{{
USBD_StatusTypeDef USBD_LL_OpenEP (USBD_HandleTypeDef* device, uint8_t ep_addr, uint8_t ep_type, uint16_t ep_mps) {
  HAL_PCD_EP_Open (device->pData, ep_addr, ep_mps, ep_type);
  return USBD_OK;
  }
//}}}
//{{{
USBD_StatusTypeDef USBD_LL_CloseEP (USBD_HandleTypeDef* device, uint8_t ep_addr) {
  HAL_PCD_EP_Close (device->pData, ep_addr);
  return USBD_OK;
  }
//}}}
//{{{
USBD_StatusTypeDef USBD_LL_FlushEP (USBD_HandleTypeDef* device, uint8_t ep_addr) {
  HAL_PCD_EP_Flush (device->pData, ep_addr);
  return USBD_OK;
  }
//}}}
//{{{
USBD_StatusTypeDef USBD_LL_StallEP (USBD_HandleTypeDef* device, uint8_t ep_addr) {
  HAL_PCD_EP_SetStall (device->pData, ep_addr);
  return USBD_OK;
  }
//}}}
//{{{
USBD_StatusTypeDef USBD_LL_ClearStallEP (USBD_HandleTypeDef* device, uint8_t ep_addr) {
  HAL_PCD_EP_ClrStall (device->pData, ep_addr);
  return USBD_OK;
  }
//}}}
//{{{
uint8_t USBD_LL_IsStallEP (USBD_HandleTypeDef* device, uint8_t ep_addr) {

  PCD_HandleTypeDef* gPcdHandle = device->pData;
  if ((ep_addr & 0x80) == 0x80)
    return gPcdHandle->IN_ep[ep_addr & 0xF].is_stall;
  else
    return gPcdHandle->OUT_ep[ep_addr & 0xF].is_stall;
  }
//}}}
//{{{
USBD_StatusTypeDef USBD_LL_SetUSBAddress (USBD_HandleTypeDef* device, uint8_t device_addr) {
  HAL_PCD_SetAddress (device->pData, device_addr);
  return USBD_OK;
  }
//}}}
//{{{
USBD_StatusTypeDef USBD_LL_Transmit (USBD_HandleTypeDef* device, uint8_t ep_addr, uint8_t* pbuf, uint16_t size) {
  HAL_PCD_EP_Transmit (device->pData, ep_addr, pbuf, size);
  return USBD_OK;
  }
//}}}
//{{{
USBD_StatusTypeDef USBD_LL_PrepareReceive (USBD_HandleTypeDef* device, uint8_t ep_addr, uint8_t* pbuf, uint16_t size) {
  HAL_PCD_EP_Receive (device->pData, ep_addr, pbuf, size);
  return USBD_OK;
  }
//}}}
//{{{
uint32_t USBD_LL_GetRxDataSize (USBD_HandleTypeDef* device, uint8_t ep_addr) {
  return HAL_PCD_EP_GetRxCount (device->pData, ep_addr);
  }
//}}}
//{{{
void USBD_LL_Delay (uint32_t Delay) {
  HAL_Delay (Delay);
  }
//}}}
//}}}

//{{{  device descriptor
#define USBD_VID  0x0483
#define USBD_PID  0x5730

__ALIGN_BEGIN static const uint8_t kDeviceDescriptor[USB_LEN_DEV_DESC] __ALIGN_END = {
  0x12, USB_DESC_TYPE_DEVICE,
  0x01, 0x01,                          // bcdUSB 1.1
  0x00,                                // bDeviceClass
  0x00,                                // bDeviceSubClass
  0x00,                                // bDeviceProtocol
  USB_MAX_EP0_SIZE,                    // bMaxPacketSize
  LOBYTE(USBD_VID), HIBYTE(USBD_VID),  // vid
  LOBYTE(USBD_PID), HIBYTE(USBD_PID),  // pid
  0x01, 0x01,                          // bcdDevice rel 1.01
  USBD_IDX_MFC_STR,                    // Index of manufacturer string
  USBD_IDX_PRODUCT_STR,                // Index of product string
  USBD_IDX_SERIAL_STR,                 // Index of serial number string
  USBD_MAX_NUM_CONFIGURATION           // bNumConfigurations
  };
//}}}
//{{{  configuration descriptor
#define CONFIG_DESC_SIZ     109
#define AUDIO_OUT_ENDPOINT  1

__ALIGN_BEGIN static const uint8_t kConfigurationDescriptor[CONFIG_DESC_SIZ] __ALIGN_END = {
  // Configuration Descriptor
  9, USB_DESC_TYPE_CONFIGURATION,
  LOBYTE(CONFIG_DESC_SIZ), HIBYTE(CONFIG_DESC_SIZ), // wTotalLength
  2,    // bNumInterfaces
  1,    // bConfigurationValue
  0,    // iConfiguration
  0xC0, // bmAttributes - BUS Powred
  0x32, // bMaxPower = 100 mA

  // Control Interface Descriptor
  9, USB_DESC_TYPE_INTERFACE,
  0, // bInterfaceNumber
  0, // bAlternateSetting
  0, // bNumEndpoints
  1, // bInterfaceClass - audio device class 1
  1, // bInterfaceSubClass - control
  0, // bInterfaceProtocol
  0, // iInterface

  //{{{  audio control descriptors
  //  Audio Control Interface Descriptor
  9, 36, 1,
  0,1,       // bcdADC - 1.00
  39,0,      // wTotalLength
  1,         // bInCollection
  1,         // baInterfaceNr

  // Audio Control Input Terminal Descriptor
  12, 36, 2,
  1,         // bTerminalID = input 1
  1,1,       // wTerminalType AUDIO_TERMINAL_USB_STREAMING - 0x0101
  0x00,      // bAssocTerminal
  CHANNELS,  // bNrChannels
  0x33,0x00, // wChannelConfig - 0x0033 - leftFront rightFront, leftSurround, rightSurround
  0,         // iChannelNames
  0,         // iTerminal

  // Audio Control Feature Unit Descriptor
  9, 36, 6,
  2,   // bUnitID = 2
  1,   // bSourceID
  1,   // bControlSize
  1,0, // bmaControls =  mute
  0,   // iTerminal

  // Audio Control Output Terminal Descriptor
  9, 36, 3,
  3,   // bTerminalID = 3
  1,3, // wTerminalType - speaker 0x0301
  0,   // bAssocTerminal
  2,   // bSourceID
  0,   // iTerminal
  //}}}
  //{{{  audio streaming descriptors
  // Audio Streaming Interface Descriptor - Zero Band - Interface 1 - Alternate Setting 0
  9, USB_DESC_TYPE_INTERFACE,
  1, // bInterfaceNumber
  0, // bAlternateSetting
  0, // bNumEndpoints
  1, // bInterfaceClass - audio device class
  2, // bInterfaceSubClass - streaming
  0, // bInterfaceProtocol
  0, // iInterface

  // Audio Streaming Interface Descriptor - Operational - Interface 1 - Alternate Setting 1
  9, USB_DESC_TYPE_INTERFACE,
  1, // bInterfaceNumber
  1, // bAlternateSetting
  1, // bNumEndpoints
  1, // bInterfaceClass - audio device class
  2, // bInterfaceSubClass - streaming
  0, // bInterfaceProtocol
  0, // iInterface

  // Audio Streaming Interface Descriptor
  7, 36, 1,
  1,   // bTerminalLink
  1,   // bDelay
  1,0, // wFormatTag AUDIO_FORMAT_PCM - 0x0001

  // Audio Streaming Descriptor Audio Type I Format
  11, 36, 2,
  1,        // bFormatType - type I
  CHANNELS, // bNrChannels
  2,        // bSubFrameSize - 2bytes per frame (16bits)
  16,       // bBitResolution - 16bits per sample
  1,        // bSamFreqType - single frequency supported
  48000 & 0xFF, (48000 >> 8) & 0xFF, 48000 >> 16, // audio sampling frequency in 3 bytes
  //}}}

  // Standard AS Isochronous Synch Endpoint Descriptor - out endPoint 1
  9, USB_DESC_TYPE_ENDPOINT,
  AUDIO_OUT_ENDPOINT, // bEndpointAddress - out endpoint 1
  5,            // bmAttributes - isochronous,asynchronous
  PACKET_SIZE & 0xFF, (PACKET_SIZE >> 8) & 0xFF, // wMaxPacketSize bytes
  1,            // bInterval
  0,            // bRefresh
  0,            // bSynchAddress

  // Class-Specific AS Isochronous Audio Data Endpoint Descriptor
  7, 37, 1,
  1, // bmAttributes - sampling frequency control
  0, // bLockDelayUnits
  0, // wLockDelay
  };
//}}}
//{{{  device qualifier descriptor
__ALIGN_BEGIN static const uint8_t kDeviceQualifierDescriptor[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END = {
  USB_LEN_DEV_QUALIFIER_DESC, USB_DESC_TYPE_DEVICE_QUALIFIER, // bDescriptorType
  0x00, 0x02,                     // bcdUSb
  0x00,                           // bDeviceClass
  0x00,                           // bDeviceSubClass
  0x00,                           // bDeviceProtocol
  0x40,                           // bMaxPacketSize0
  0x01,                           // bNumConfigurations
  0x00,                           // bReserved
  };
//}}}
//{{{  language id descriptor
#define USBD_LANGID_STRING    0x409

__ALIGN_BEGIN static const uint8_t kLangIdDescriptor[USB_LEN_LANGID_STR_DESC] __ALIGN_END = {
  USB_LEN_LANGID_STR_DESC,
  USB_DESC_TYPE_STRING,
  LOBYTE(USBD_LANGID_STRING), HIBYTE(USBD_LANGID_STRING),
  };
//}}}
//{{{  serial string descriptor
#define USB_SIZ_STRING_SERIAL 0x1A

__ALIGN_BEGIN static uint8_t kStringSerial[USB_SIZ_STRING_SERIAL] __ALIGN_END = {
  USB_SIZ_STRING_SERIAL,
  USB_DESC_TYPE_STRING,
  };
//}}}

//{{{  audioDescriptor handler
__ALIGN_BEGIN static uint8_t strDesc[256] __ALIGN_END;
//{{{
static void intToUnicode (uint32_t value, uint8_t* pbuf, uint8_t len) {

  uint8_t idx = 0;
  for (idx = 0; idx < len; idx ++) {
    if (((value >> 28)) < 0xA )
      pbuf[2 * idx] = (value >> 28) + '0';
    else
      pbuf[2 * idx] = (value >> 28) + 'A' - 10;

    value = value << 4;
    pbuf[2 * idx + 1] = 0;
    }
  }
//}}}
//{{{
static void getSerialNum() {

  uint32_t deviceserial0 = *(uint32_t*)0x1FFF7A10;
  uint32_t deviceserial1 = *(uint32_t*)0x1FFF7A14;
  uint32_t deviceserial2 = *(uint32_t*)0x1FFF7A18;

  deviceserial0 += deviceserial2;
  if (deviceserial0 != 0) {
    intToUnicode (deviceserial0, &kStringSerial[2], 8);
    intToUnicode (deviceserial1, &kStringSerial[18], 4);
    }
  }
//}}}

//{{{
static uint8_t* deviceDescriptor (USBD_SpeedTypeDef speed, uint16_t* length) {
  *length = sizeof(kDeviceDescriptor);
  return (uint8_t*)kDeviceDescriptor;
  }
//}}}
//{{{
static uint8_t* langIdStringDescriptor (USBD_SpeedTypeDef speed, uint16_t* length) {
  *length = sizeof(kLangIdDescriptor);
  return (uint8_t*)kLangIdDescriptor;
  }
//}}}
//{{{
static uint8_t* manufacturerStringDescriptor (USBD_SpeedTypeDef speed, uint16_t* length) {
  #define USBD_MANUFACTURER_STRING  "colin"
  USBD_GetString ((uint8_t*)USBD_MANUFACTURER_STRING, strDesc, length);
  return strDesc;
  }
//}}}
//{{{
static uint8_t* productStringDescriptor (USBD_SpeedTypeDef speed, uint16_t* length) {
  USBD_GetString ((uint8_t*)((speed == USBD_SPEED_HIGH) ? "Stm32 HS USB audio 1.0" : "Stm32 FS USB audio 1.0"), strDesc, length);
  return strDesc;
  }
//}}}
//{{{
static uint8_t* serialStringDescriptor (USBD_SpeedTypeDef speed, uint16_t* length) {

  *length = USB_SIZ_STRING_SERIAL;
  getSerialNum();
  return (uint8_t*)kStringSerial;
  }
//}}}
//{{{
static uint8_t* configurationStringDescriptor (USBD_SpeedTypeDef speed, uint16_t* length) {
  #define USBD_CONFIGURATION_STRING "audio config"
  USBD_GetString ((uint8_t*)USBD_CONFIGURATION_STRING, strDesc, length);
  return strDesc;
  }
//}}}
//{{{
static uint8_t* interfaceStringDescriptor (USBD_SpeedTypeDef speed, uint16_t* length) {

  #define USBD_INTERFACE_STRING "audio Interface"
  USBD_GetString ((uint8_t*)USBD_INTERFACE_STRING, strDesc, length);
  return strDesc;
  }
//}}}

static USBD_DescriptorsTypeDef audioDescriptor = {
  deviceDescriptor,
  langIdStringDescriptor,
  manufacturerStringDescriptor,
  productStringDescriptor,
  serialStringDescriptor,
  configurationStringDescriptor,
  interfaceStringDescriptor,
  };
//}}}
//{{{  audioClass handler
typedef struct {
  uint8_t       mBuffer[SLOTS_PACKET_BUF_SIZE];
  uint8_t       mPlayStarted;
  uint16_t      mWritePtr;
  __IO uint32_t mAltSetting;

  uint8_t       mCommand;
  uint8_t       mData[USB_MAX_EP0_SIZE];
  uint8_t       mLength;
  uint8_t       mUnit;

  uint16_t      mMute;
  uint16_t      mCurVolume;
  uint16_t      mMinVolume;
  uint16_t      mMaxVolume;
  uint16_t      mResVolume;
  uint16_t      mFrequency;
  } tAudioData;

//{{{
static uint8_t usbInit (USBD_HandleTypeDef* device, uint8_t cfgidx) {

  // Open EP OUT
  USBD_LL_OpenEP (device, AUDIO_OUT_ENDPOINT, USBD_EP_TYPE_ISOC, PACKET_SIZE);

  // allocate audioData
  tAudioData* audioData = malloc (sizeof (tAudioData));
  audioData->mPlayStarted = 0;
  audioData->mWritePtr = 0;
  audioData->mAltSetting = 0;

  audioData->mMute = 0;
  audioData->mCurVolume = 50;
  audioData->mMinVolume = 0;
  audioData->mMaxVolume = 100;
  audioData->mResVolume = 1;
  audioData->mFrequency = SAMPLE_RATE;
  device->pClassData = audioData;

  BSP_AUDIO_OUT_Init (OUTPUT_DEVICE_BOTH, 100, SAMPLE_RATE);
  BSP_AUDIO_OUT_SetAudioFrameSlot (SAI_SLOTACTIVE_0 | SAI_SLOTACTIVE_1 | SAI_SLOTACTIVE_2 | SAI_SLOTACTIVE_3);

  // Prepare Out endpoint to receive 1st packet
  USBD_LL_PrepareReceive (device, AUDIO_OUT_ENDPOINT, audioData->mBuffer, PACKET_SIZE);

  return USBD_OK;
  }
//}}}
//{{{
static uint8_t usbDeInit (USBD_HandleTypeDef* device, uint8_t cfgidx) {

  // close out ep
  USBD_LL_CloseEP (device, AUDIO_OUT_ENDPOINT);

  // stop audio
  BSP_AUDIO_OUT_Stop (CODEC_PDWN_SW);
  free (device->pClassData);
  device->pClassData = NULL;

  return USBD_OK;
  }
//}}}

//{{{
static uint8_t usbSetup (USBD_HandleTypeDef* device, USBD_SetupReqTypedef* req) {

  debug (LCD_COLOR_WHITE, "setup %02x:%02x %d:%d:%d",
                        req->bmRequest, req->bRequest, req->wLength, req->wValue, req->wIndex);

  tAudioData* audioData = (tAudioData*)device->pClassData;
  switch (req->bmRequest & USB_REQ_TYPE_MASK) {
    case USB_REQ_TYPE_STANDARD:
      switch (req->bRequest) {
        //{{{
        case USB_REQ_GET_DESCRIPTOR:
          debug (LCD_COLOR_RED, "- USB_REQ_GET_DESCRIPTOR");
          break;
        //}}}
        //{{{
        case USB_REQ_GET_INTERFACE :
          USBD_CtlSendData (device, (uint8_t*)&audioData->mAltSetting, 1);
          debug (LCD_COLOR_YELLOW, "- usbReqGetInterface alt%x", (int)audioData->mAltSetting);
          break;
        //}}}
        //{{{
        case USB_REQ_SET_INTERFACE :
          if (req->wValue <= USBD_MAX_NUM_INTERFACES) {
            audioData->mAltSetting = (uint8_t)req->wValue;
            debug (LCD_COLOR_YELLOW, "%d - usbReqSetInterface alt:%x", req->wValue);
            }
          else {// NAK
            USBD_CtlError (device, req);
            debug (LCD_COLOR_RED, "%d - usbReqSetInterface");
            }
          break;
        //}}}
        //{{{
        default:
          USBD_CtlError (device, req);
          return USBD_FAIL;
        //}}}
        }
      break;

    case USB_REQ_TYPE_CLASS :
      switch (req->bRequest) {
        //{{{
        case 0x01: // setCur
          if ((req->wValue >> 8) == 1) {
            if (req->wLength) {
              // rx buffer from ep0
              USBD_CtlPrepareRx (device, audioData->mData, req->wLength);
              audioData->mCommand = 0x01;             // set request value
              audioData->mLength = req->wLength;      // set request data length
              audioData->mUnit = HIBYTE(req->wIndex); // set request target unit
              }
            }

          debug (LCD_COLOR_YELLOW, "- setCur %d:%x", req->wLength, audioData->mData[0]);
          break;
        //}}}
        //{{{
        case 0x02: // setMin
          debug (LCD_COLOR_YELLOW, "- setMin %d", req->wLength);
          break;
        //}}}
        //{{{
        case 0x03: // setMax
          debug (LCD_COLOR_YELLOW, "- setMax %d", req->wLength);
          break;
        //}}}
        //{{{
        case 0x04: // setRes
          debug (LCD_COLOR_YELLOW, "- setRes %d", req->wLength);
          break;
        //}}}
        //{{{
        case 0x81: // getCur
          if ((req->wValue >> 8) == 1) {
            USBD_CtlSendData (device, (uint8_t*)(&audioData->mMute), req->wLength);
            debug (LCD_COLOR_YELLOW, "- getCur mute %d:%x", req->wLength, audioData->mMute);
            }
          else if ((req->wValue >> 8) == 2) {
            USBD_CtlSendData (device, (uint8_t*)(&audioData->mCurVolume), req->wLength);
            debug (LCD_COLOR_YELLOW, "- getCur volume %d:%x", req->wLength, audioData->mCurVolume);
            }
          else
            debug (LCD_COLOR_RED, "- getCur %d", req->wLength);
          break;
        //}}}
        //{{{
        case 0x82: // getMin
          USBD_CtlSendData (device, (uint8_t*)(&audioData->mMinVolume), req->wLength);
          debug (LCD_COLOR_YELLOW, "- getMin %d:%x", req->wLength, audioData->mMinVolume);
          break;
        //}}}
        //{{{
        case 0x83: // getMax
          USBD_CtlSendData (device, (uint8_t*)(&audioData->mMaxVolume), req->wLength);
          debug (LCD_COLOR_YELLOW, "- getMax %d:%x", req->wLength, audioData->mMaxVolume);
          break;
        //}}}
        //{{{
        case 0x84: // getRes
          USBD_CtlSendData (device, (uint8_t*)(&audioData->mResVolume), req->wLength);
          debug (LCD_COLOR_YELLOW, "- getRes %d:%x", req->wLength, audioData->mResVolume);
          break;
        //}}}
        default:
          debug (LCD_COLOR_YELLOW, "- default %d", req->wLength);
          USBD_CtlError (device, req);
          return USBD_FAIL;
        }
    }
  return USBD_OK;
  }
//}}}

//{{{
static uint8_t usbEp0TxReady (USBD_HandleTypeDef* device) {
  return USBD_OK;
  }
//}}}
//{{{
static uint8_t usbEp0RxReady (USBD_HandleTypeDef* device) {
// only SET_CUR request is managed

  tAudioData* audioData = (tAudioData*)device->pClassData;
  if (audioData->mCommand == 0x01) {
    if (audioData->mUnit == 0) {
      audioData->mFrequency = audioData->mData[0] + (audioData->mData[1] << 8) + (audioData->mData[2] << 16);
      //BSP_AUDIO_OUT_SetFrequency (audioData->mFrequency);
      debug (LCD_COLOR_GREEN, "setFreq %d", audioData->mFrequency);
      }
    else if (audioData->mUnit == 2) {
      audioData->mMute = audioData->mData[0];
      BSP_AUDIO_OUT_SetMute (audioData->mMute);
      debug (LCD_COLOR_GREEN, "%s", audioData->mMute ? "muted" : "unmuted");
      }
    else
      debug (LCD_COLOR_RED, "epRx audReqSetCur %d cmd:%d unit:%d",
               audioData->mLength, audioData->mCommand, audioData->mUnit);
    }
  else
    debug (LCD_COLOR_YELLOW, "epRx:%d cmd:%d unit:%d data:%x",
                           audioData->mLength, audioData->mCommand, audioData->mUnit, audioData->mData[0]);

  audioData->mCommand = 0;
  audioData->mLength = 0;

  return USBD_OK;
  }
//}}}

//{{{
static uint8_t usbDataIn (USBD_HandleTypeDef* device, uint8_t epNum) {

  debug (LCD_COLOR_RED, "usbDataIn");
  return USBD_OK;
  }
//}}}
//{{{
static uint8_t usbDataOut (USBD_HandleTypeDef* device, uint8_t epNum) {

  if (epNum == AUDIO_OUT_ENDPOINT) {
    tAudioData* audioData = (tAudioData*)device->pClassData;
    if (!audioData->mPlayStarted && (audioData->mWritePtr >= SLOTS_PACKET_BUF_SIZE/2)) {
      //{{{  start playing
      BSP_AUDIO_OUT_Play ((uint16_t*)audioData->mBuffer, SLOTS_PACKET_BUF_SIZE);
      audioData->mPlayStarted = 1;
      }
      //}}}
    int16_t* ptr = (int16_t*)(audioData->mBuffer + audioData->mWritePtr);
    for (int sample = 0; sample < PACKET_SAMPLES; sample++) {
      //{{{  swap FR,RL and sum
      int16_t srcRL = *(ptr+2);
      *(ptr+2) = *(ptr+1);
      *(ptr+1) = srcRL;

      // sum
      sumFL += abs (*ptr++);
      sumFR += abs (*ptr++);
      sumRL += abs (*ptr++);
      sumRR += abs (*ptr++);
      }
      //}}}
    gPackets++;
    if (gPackets >= 20) {
      //{{{  save every 20ms
      gPackets = 0;
      gFL[gSample] = sumFL >> 16;
      gFR[gSample] = sumFR >> 16;
      gRL[gSample] = sumRL >> 16;
      gRR[gSample] = sumRR >> 16;
      gSample = (gSample+1) % gCentreX;
      sumFR = 0;
      sumFL = 0;
      sumRL = 0;
      sumRR = 0;
      }
      //}}}

    // prepare outEndpoint to rx next audio packet
    audioData->mWritePtr += SLOTS_PACKET_SIZE;
    if (audioData->mWritePtr >= SLOTS_PACKET_BUF_SIZE)
      audioData->mWritePtr = 0;
    USBD_LL_PrepareReceive (device, AUDIO_OUT_ENDPOINT, &audioData->mBuffer[audioData->mWritePtr], PACKET_SIZE);
    }

  return USBD_OK;
  }
//}}}

//{{{
static uint8_t usbSof (USBD_HandleTypeDef* device) {
  return USBD_OK;
  }
//}}}

//{{{
static uint8_t usbIsoInInComplete (USBD_HandleTypeDef* device, uint8_t epNum) {
  return USBD_OK;
  }
//}}}
//{{{
static uint8_t usbIsoOutInComplete (USBD_HandleTypeDef* device, uint8_t epNum) {
  return USBD_OK;
  }
//}}}

//{{{
static uint8_t* usbGetConfigDescriptor (uint16_t* length) {
  *length = sizeof (kConfigurationDescriptor);
  return (uint8_t*)kConfigurationDescriptor;
  }
//}}}
//{{{
static uint8_t* usbGetDeviceQualifierDescriptor (uint16_t *length) {
  *length = sizeof (kDeviceQualifierDescriptor);
  return (uint8_t*)kDeviceQualifierDescriptor;
  }
//}}}

static USBD_ClassTypeDef audioClass = {
  usbInit,
  usbDeInit,
  usbSetup,
  usbEp0TxReady,
  usbEp0RxReady,
  usbDataIn,
  usbDataOut,
  usbSof,
  usbIsoInInComplete,
  usbIsoOutInComplete,
  usbGetConfigDescriptor,
  usbGetConfigDescriptor,
  usbGetConfigDescriptor,
  usbGetDeviceQualifierDescriptor,
  };
//}}}

//{{{
static void audioClock (int faster) {
// Set the PLL configuration according to the audio frequency
// target = 48000*2*2 * 256 = 49.152Mhz

// slower
// - PLLI2S_VCO: VCO_344 N
// - I2S_CLK = PLLI2S_VCO / PLLI2SQ = 344/7 = 49.142 Mhz
// - I2S_CLK1 = I2S_CLK / PLLI2SDIVQ = 49.142/1 = 49.142 Mhz

// faster
// - PLLI2S_VCO: VCO_295 N
// - I2S_CLK = PLLI2S_VCO / PLLI2SQ = 295/6 = 49.166 Mhz
// - I2S_CLK1 = I2S_CLK / PLLI2SDIVQ = 49.1666/1 = 49.142 Mhz

  if (faster != gFaster) {
    RCC_PeriphCLKInitTypeDef RCC_ExCLKInitStruct;
    HAL_RCCEx_GetPeriphCLKConfig (&RCC_ExCLKInitStruct);
    RCC_ExCLKInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SAI2;
    RCC_ExCLKInitStruct.Sai2ClockSelection = RCC_SAI2CLKSOURCE_PLLI2S;
    RCC_ExCLKInitStruct.PLLI2S.PLLI2SP = 8;
    RCC_ExCLKInitStruct.PLLI2S.PLLI2SN = faster ? 295 : 344;
    RCC_ExCLKInitStruct.PLLI2S.PLLI2SQ = faster ? 6 : 7;
    RCC_ExCLKInitStruct.PLLI2SDivQ = 1;
    HAL_RCCEx_PeriphCLKConfig (&RCC_ExCLKInitStruct);
    gFaster = faster;
    }
  }
//}}}
//{{{
void BSP_AUDIO_OUT_ClockConfig (SAI_HandleTypeDef* hsai, uint32_t freq, void* Params) {
  audioClock (0);
  }
//}}}
//{{{
void BSP_AUDIO_OUT_TransferComplete_CallBack() {

  writePtrOnRead = ((tAudioData*)gUsbDevice.pClassData)->mWritePtr / SLOTS_PACKET_SIZE;
  if (writePtrOnRead > PACKETS/2) // faster
    audioClock (1);
  else if (writePtrOnRead < PACKETS/2) // slower
    audioClock (0);
  }
//}}}

static void onProx (int x, int y, int z) {}
static void onPress (int x, int y) {}
//{{{
static void onMove (int x, int y, int z) {

  gScroll += y;
  setScrollValue (gScroll + y);
  }
//}}}
//{{{
static void onScroll (int x, int y, int z) {

  gScroll += y;
  setScrollValue (gScroll + y);
  }
//}}}
static void onRelease (int x, int y) {}
//{{{
static void touch() {

  BSP_TS_GetState (&gTsState);

  //if (gTsState.touchDetected)
  //  debug (LCD_COLOR_YELLOW, "%d x:%d y:%d w:%d e:%d a:%d g:%d",
  //         gTsState.touchDetected, gTsState.touchX[0],gTsState.touchY[0], gTsState.touchWeight[0],
  //         gTsState.touchEventId[0], gTsState.touchArea[0], gTsState.gestureId);

  if (gTsState.touchDetected) {
    // pressed
    if (gTsState.touchDetected > 1) {
      gHit = eScroll;
      onScroll (gTsState.touchX[0] - gLastX, gTsState.touchY[0] - gLastY, gTsState.touchWeight[0]);
      }
    else if (gHit == ePressed)
      onMove (gTsState.touchX[0] - gLastX, gTsState.touchY[0] - gLastY, gTsState.touchWeight[0]);
    else if ((gHit == eReleased) && (gTsState.touchWeight[0] > 50)) {
      // press
      gHitX = gTsState.touchX[0];
      gHitY = gTsState.touchY[0];
      onPress (gHitX, gHitY);
      gHit = ePressed;
      }
    else if (gHit == eProx)
      onProx (gTsState.touchX[0] - gLastX, gTsState.touchY[0] - gLastY, gTsState.touchWeight[0]);
    else
      gHit = eProx;
    gLastX = gTsState.touchX[0];
    gLastY = gTsState.touchY[0];
    }
  else {
    // release
    if (gHit == ePressed)
      onRelease (gLastX, gLastY);
    gHit = eReleased;
    }
  }
//}}}

//{{{
int main() {

  SCB_EnableICache();
  SCB_EnableDCache();
  HAL_Init();
  //{{{  config system clock
  // System Clock source            = PLL (HSE)
  // SYSCLK(Hz)                     = 216000000
  // HCLK(Hz)                       = 216000000
  // AHB Prescaler                  = 1
  // APB1 Prescaler                 = 4
  // APB2 Prescaler                 = 2
  // HSE Frequency(Hz)              = 25000000
  // PLL_M                          = 25
  // PLL_N                          = 432
  // PLL_P                          = 2
  // PLLSAI_N                       = 384
  // PLLSAI_P                       = 8
  // VDD(V)                         = 3.3
  // Main regulator output voltage  = Scale1 mode
  // Flash Latency(WS)              = 7

  // Enable HSE Oscillator and activate PLL with HSE as source
  RCC_OscInitTypeDef RCC_OscInitStruct;
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_OFF;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 432;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 9;
  if (HAL_RCC_OscConfig (&RCC_OscInitStruct) != HAL_OK)
    while (1) {}

  // Activate the OverDrive to reach the 216 Mhz Frequency
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
    while (1) {}

  // Select PLL as system clock source and configure the HCLK, PCLK1 and PCLK2 clocks dividers
  RCC_ClkInitTypeDef RCC_ClkInitStruct;
  RCC_ClkInitStruct.ClockType = (RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                                 RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2);
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;
  if (HAL_RCC_ClockConfig (&RCC_ClkInitStruct, FLASH_LATENCY_7) != HAL_OK)
    while (1) {}
  //}}}

  BSP_LED_Init (LED1);
  BSP_PB_Init (BUTTON_KEY, BUTTON_MODE_GPIO);

  initLcd();
  BSP_TS_Init (BSP_LCD_GetXSize(), BSP_LCD_GetYSize());
  gCentreX = BSP_LCD_GetXSize()/2;
  gCentreY = BSP_LCD_GetYSize()/2;

  // init usbDevice library
  gUsbDevice.pClassData = NULL;
  USBD_Init (&gUsbDevice, &audioDescriptor, 0);
  USBD_RegisterClass (&gUsbDevice, &audioClass);
  USBD_Start (&gUsbDevice);

  while (1) {
    touch();
    showLcd();
    }
  }
//}}}
