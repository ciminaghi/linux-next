#ifndef __MCUIO_IDS_H__
#define __MCUIO_IDS_H__

/* Various class definitions */

#define MCUIO_CLASS_UNDEFINED			0x000000
/* Hardware implementation of host controller */
#define MCUIO_CLASS_HOST_CONTROLLER		0x000001
/* Software implementation of host controller (line discipline) */
#define MCUIO_CLASS_SOFT_HOST_CONTROLLER	0x000011
/* GPIOs */
#define MCUIO_CLASS_GPIO			0x000002
/* GPIOs with spontaneous messages support */
#define MCUIO_CLASS_GPIO_SM			0x010002
/* ADC */
#define MCUIO_CLASS_ADC				0x000003
/* ADC with spontaneous messages support */
#define MCUIO_CLASS_ADC_SM			0x010003
/* DAC */
#define MCUIO_CLASS_DAC				0x000004
/* DAC with spontaneous messages support */
#define MCUIO_CLASS_DAC_SM			0x010004
/* PWM */
#define MCUIO_CLASS_PWM				0x000005
/* PWM with spontaneous messages support */
#define MCUIO_CLASS_PWM_SM			0x010005
/* Graphic display */
#define MCUIO_CLASS_GRAPHIC_DISPLAY		0x000006
/* Graphic display with spontaneous messages support */
#define MCUIO_CLASS_GRAPHIC_DISPLAY_SM		0x010006
/* Text display */
#define MCUIO_CLASS_TEXT_DISPLAY		0x000007
/* Text display with spontaneous messages support */
#define MCUIO_CLASS_TEXT_DISPLAY_SM		0x010007
/* I2C controller */
#define MCUIO_CLASS_I2C_CONTROLLER		0x000008
/* I2C controller with spontaneous messages support */
#define MCUIO_CLASS_I2C_CONTROLLER_SM		0x010008
/* SPI controller */
#define MCUIO_CLASS_SPI_CONTROLLER		0x000009
/* SPI controller with spontaneous messages support */
#define MCUIO_CLASS_SPI__CONTROLLER_SM		0x010009
/* Wire base irq controller */
#define MCUIO_CLASS_IRQ_CONTROLLER_WIRE		0x00000a

/* Invalid device id (used for id table termination */
#define MCUIO_NO_DEVICE				0x0000

#endif /* __MCUIO_IDS_H__ */
