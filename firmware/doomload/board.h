/* Register map and pin assignment of the SG-004A (AT32F403AVGT7), recovered from stock firmware V3.0 */
#pragma once
#include <stdint.h>

#define REG(a)          (*(volatile uint32_t *)(a))
#define REG16(a)        (*(volatile uint16_t *)(a))

#define SCB_VTOR        REG(0xE000ED08u)
#define IWDG_KR         REG(0x40003000u)

#define RCC_BASE        0x40021000u
#define RCC_CR          REG(RCC_BASE + 0x00)
#define RCC_CFGR        REG(RCC_BASE + 0x04)
#define RCC_AHBENR      REG(RCC_BASE + 0x14)
#define RCC_APB2ENR     REG(RCC_BASE + 0x18)

#define AFIO_MAPR       REG(0x40010004u)

#define GPIOA_BASE      0x40010800u
#define GPIOB_BASE      0x40010C00u
#define GPIOC_BASE      0x40011000u
#define GPIOD_BASE      0x40011400u
#define GPIOE_BASE      0x40011800u
#define GPIO_CRL        0x00
#define GPIO_CRH        0x04
#define GPIO_IDR        0x08
#define GPIO_ODR        0x0C
#define GPIO_BSRR       0x10
#define GPIO_BRR        0x14

/* GPIO mode nibbles (F1-style CRL/CRH) */
#define GM_IN_PULL      0x8u
#define GM_OUT_PP_2M    0x2u
#define GM_AF_PP_50M    0xBu

/* FSMC/XMC bank 1, NE1; RS is address line A16, so data lives at +0x20000 */
#define FSMC_BCR1       REG(0xA0000000u)
#define FSMC_BTR1       REG(0xA0000004u)
#define LCD_CMD         REG16(0x60000000u)
#define LCD_DATA        REG16(0x60020000u)

/* Power: PB7 holds the power latch (1 = stay on), PB8 is the power key (0 = pressed) */
#define PWR_HOLD_PORT   GPIOB_BASE
#define PWR_HOLD_PIN    7
#define PWR_KEY_PORT    GPIOB_BASE
#define PWR_KEY_PIN     8

/* Keys, active low with pull-up; mapping confirmed on the device. PB3/PB4 need JTAG disabled. */
#define KEY_IN_PORT     GPIOC_BASE
#define KEY_IN_PIN      14
#define KEY_OUT_PORT    GPIOC_BASE
#define KEY_OUT_PIN     15
#define KEY_FN_PORT     GPIOC_BASE
#define KEY_FN_PIN      13
#define KEY_UP_PORT     GPIOC_BASE      /* RCVK */
#define KEY_UP_PIN      12
#define KEY_DOWN_PORT   GPIOB_BASE      /* CONVRET */
#define KEY_DOWN_PIN    4
#define KEY_LEFT_PORT   GPIOD_BASE      /* AUTO */
#define KEY_LEFT_PIN    2
#define KEY_RIGHT_PORT  GPIOB_BASE      /* CURVE */
#define KEY_RIGHT_PIN   6
#define KEY_OK_PORT     GPIOB_BASE      /* ON/OFF, centre */
#define KEY_OK_PIN      3

/* LCD: PD6 reset, PC6 backlight (TIM8_CH1 in stock fw; active low) */
#define LCD_RST_PORT    GPIOD_BASE
#define LCD_RST_PIN     6
#define BL_PORT         GPIOC_BASE
#define BL_PIN          6

/* ---------- additions for the Doom port ---------- */
#define RCC_MISC2       REG(RCC_BASE + 0x54)    /* AT32 CRM_MISC2: AUTO_STEP_EN = bits 5:4 */
#define SCB_CPACR       REG(0xE000ED88u)
#define SCB_CFSR        REG(0xE000ED28u)
#define SCB_HFSR        REG(0xE000ED2Cu)
#define SYST_CSR        REG(0xE000E010u)
#define SYST_RVR        REG(0xE000E014u)
#define SYST_CVR        REG(0xE000E018u)
