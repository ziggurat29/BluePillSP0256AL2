//==============================================================
//This realizes the various serial devices in the system.
//This module is part of the BluePillSP0256AL2 project.
//Some parts of this logical component are realized in main.c, as a
//consequence of the STM32CubeMX code generator.
//Additionally, there are many modifications to the machine-generated
//USB CDC middleware that are required for that stuff to work as well.

#include "serial_devices.h"

#include "main.h"
#include "stm32f1xx_hal.h"
#include "cmsis_os.h"
#include "usb_device.h"
#include "usbd_cdc_if.h"

#include "lamps.h"

#include "util_circbuff2.h"



//Because of the peculiarities of the STM32CubeMX, we are leaving these things
//in main.c.  They come from generated code, and if we fight the system, we
//will be in an eternal struggle of light against darkness. So we go to the
//dark side and leave them there.
//extern UART_HandleTypeDef huart1;
//NOTE there is not handle-y thing for the CDC for us (well, sort of, there is
//an object buried in the 'middleware', but we don't need it.)


//USB CDC transmit/receive circular buffers
CIRCBUF(CDC_txbuff,uint8_t,128);
CIRCBUF(CDC_rxbuff,uint8_t,128);





//these are debug methods for tuning buffer sizes
#ifdef DEBUG

unsigned int CDC_txbuff_max ( void )
{
	return circbuff_max ( &CDC_txbuff );
}

unsigned int CDC_rxbuff_max ( void )
{
	return circbuff_max ( &CDC_rxbuff );
}

#endif


//========================================================================



static void USBCDC_flushTtransmit ( const IOStreamIF* pthis );
static size_t USBCDC_transmitFree ( const IOStreamIF* pthis );
static void USBCDC_flushReceive ( const IOStreamIF* pthis );
static size_t USBCDC_receiveAvailable ( const IOStreamIF* pthis );
static size_t USBCDC_transmit ( const IOStreamIF* pthis, const void* pv, size_t nLen );
static size_t USBCDC_receive ( const IOStreamIF* pthis, void* pv, const size_t nLen );


static int Serial_transmitCompletely ( const IOStreamIF* pcom, const void* pv, size_t nLen, uint32_t to );
static int Serial_receiveCompletely ( const IOStreamIF* pcom, void* pv, const size_t nLen, uint32_t to );


const IOStreamIF g_pifCDC = {
	USBCDC_flushTtransmit,
	USBCDC_transmitFree,
	USBCDC_transmit,
	USBCDC_flushReceive,
	USBCDC_receiveAvailable,
	USBCDC_receive,
	Serial_transmitCompletely,
	Serial_receiveCompletely,
	NULL
};




//====================================================
//USB CDC support


//The ST USB CDC 'middleware' is also a bit goofy (like the UART
//implementation.  Actually, I think it's worse).  We have attempted to spackle
//over the oddities with a different API that uses circular buffers to push and
//pull data from the USB CDC channel.  The modified middleware implementation
//needs to get data from these circular buffers; it does this by way of these
//callbacks.
//Eventually, I may move this internal to the USB CDC implementation, but it is
//not yet clear that this will improve things, so I am keeping them here for
//the moment, ugly though that may be.
size_t XXX_Pull_USBCDC_TxData ( uint8_t* pbyBuffer, const size_t nMax )
{
	size_t nPulled;
	UBaseType_t uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();	//lock queue
	size_t nToPull = circbuff_count(&CDC_txbuff);	//max you could pull
	if ( nMax < nToPull )	//no buffer overruns, please
		nToPull = nMax;
	for ( nPulled = 0; nPulled < nToPull; ++nPulled )
	{
		circbuff_dequeue(&CDC_txbuff,&pbyBuffer[nPulled]);
	}
	taskEXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);	//unlock queue
	return nPulled;
}

size_t XXX_Push_USBCDC_RxData ( const uint8_t* pbyBuffer, const size_t nAvail )
{
	size_t nPushed;
	UBaseType_t uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();	//lock queue
	size_t nToPush = circbuff_capacity(&CDC_rxbuff) - circbuff_count(&CDC_rxbuff);	//max you could push
	if ( nAvail < nToPush )	//no buffer overruns, please
		nToPush = nAvail;
	for ( nPushed = 0; nPushed < nToPush; ++nPushed )
	{
		circbuff_enqueue ( &CDC_rxbuff, &pbyBuffer[nPushed] );
	}
	taskEXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);	//unlock queue
	return nPushed;
}



//====================================================
//USB CDC read/write API



static void USBCDC_flushTtransmit ( const IOStreamIF* pthis )
{
	UBaseType_t uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();	//lock queue
	circbuff_init(&CDC_txbuff);
	taskEXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);	//unlock queue
}


static void USBCDC_flushReceive ( const IOStreamIF* pthis )
{
	UBaseType_t uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();	//lock queue
	circbuff_init(&CDC_rxbuff);
	taskEXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);	//unlock queue
}


static size_t USBCDC_transmit ( const IOStreamIF* pthis, const void* pv, size_t nLen )
{
	size_t nPushed;
	UBaseType_t uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();	//lock queue
	size_t nToPush = circbuff_capacity(&CDC_txbuff) - circbuff_count(&CDC_txbuff);	//max you could push
	if ( nLen < nToPush )	//no buffer overruns, please
		nToPush = nLen;
	for ( nPushed = 0; nPushed < nToPush; ++nPushed )
	{
		circbuff_enqueue ( &CDC_txbuff, &((uint8_t*)pv)[nPushed] );
	}
	taskEXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);	//unlock queue
	//notify to kick-start transmission, if needed
	CDC_Transmit_FS(NULL, 0);
	return nPushed;
}



static size_t USBCDC_receive ( const IOStreamIF* pthis, void* pv, const size_t nLen )
{
	size_t nPulled;
	UBaseType_t uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();	//lock queue
	size_t nToPull = circbuff_count(&CDC_rxbuff);	//max you could pull
	if ( nLen < nToPull )	//no buffer overruns, please
		nToPull = nLen;
	for ( nPulled = 0; nPulled < nToPull; ++nPulled )
	{
		circbuff_dequeue(&CDC_rxbuff,&((uint8_t*)pv)[nPulled]);
	}
	taskEXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);	//unlock queue
	return nPulled;
}



//what are the number of bytes available to be read now
static size_t USBCDC_receiveAvailable ( const IOStreamIF* pthis )
{
	size_t n;
	UBaseType_t uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();	//lock queue
	n = circbuff_count(&CDC_rxbuff);
	taskEXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);	//unlock queue
	return n;
}



//how much can be pushed into the transmitter buffers now
static size_t USBCDC_transmitFree ( const IOStreamIF* pthis )
{
	size_t n;
	UBaseType_t uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();	//lock queue
	n = circbuff_capacity(&CDC_txbuff) - circbuff_count(&CDC_txbuff);
	taskEXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);	//unlock queue
	return n;
}



//our stub implementation of the optional notification callbacks
__weak void USBCDC_DataAvailable ( void ){}
__weak void USBCDC_TransmitEmpty ( void ){}




//====================================================
//Generalized blocking serial transmit/receive functions; these take function
//pointers to the send/receive methods for particular serial ports



//A simple blocking transmit function.  This just does a sleep when the
//transmit buffer cannot accept more.  Not sophisticated, but still better than
//polling.
//The return value is the portion of nLen that has /not/ been processed; so
//0 means success, and non-zero means failure, and nLen-(return) means how
//much /was/ processed.
static int Serial_transmitCompletely ( const IOStreamIF* pcom, const void* pv, size_t nLen, uint32_t to )
{
	uint32_t tsStart;
	size_t nIdxNow;
	size_t nRemaining;
	size_t nDone;
	
	tsStart = HAL_GetTick();
	
	nIdxNow = 0;
	while ( nRemaining = nLen - nIdxNow, 0 != nRemaining )
	{
		nDone = pcom->_transmit ( pcom, &((const uint8_t*)pv)[nIdxNow], nRemaining );
		nIdxNow += nDone;
		if ( nDone != nRemaining )
		{
			if ( ( HAL_GetTick() - tsStart ) > to )
			{
				return nLen - nIdxNow;	//(must recompute since we're at this point)
			}
			osDelay(1);
		}
	}
	
/*
	//Reeeeally completely
	while ( ! UART2_txbuff_empty() )
	{
		osDelay ( 1 );
	}
*/
	return 0;	//tada!
}



//A simple blocking receive function.  This just does a sleep when the receive
//buffer cannot produce what is requested.  Not sophisticated, but still better
//than polling.
static int Serial_receiveCompletely ( const IOStreamIF* pcom, void* pv, const size_t nLen, uint32_t to )
{
	uint32_t tsStart;
	size_t nIdxNow;
	size_t nRemaining;
	size_t nDone;
	
	tsStart = HAL_GetTick();
	
	nIdxNow = 0;
	while ( nRemaining = nLen - nIdxNow, 0 != nRemaining )
	{
		nDone = pcom->_receive ( pcom, &((uint8_t*)pv)[nIdxNow], nRemaining );
		nIdxNow += nDone;
		if ( nDone != nRemaining )
		{
			if ( ( HAL_GetTick() - tsStart ) > to )
			{
				return nLen - nIdxNow;	//(must recompute since we're at this point)
			}
			osDelay(1);
		}
	}
	return 0;	//tada!
}



//========================================================================



void USBCDC_Init ( void )
{
	circbuff_init(&CDC_txbuff);
	circbuff_init(&CDC_rxbuff);
}

