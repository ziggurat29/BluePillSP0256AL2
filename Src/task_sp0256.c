

#include "main.h"
#include "cmsis_os.h"

#include "task_sp0256.h"
#include "util_circbuff2.h"

#include "task_notification_bits.h"



//the task that runs the SP0256-AL2 interface
osThreadId g_thSP0256 = NULL;
uint32_t g_tbSP0256[ 128 ];
osStaticThreadDef_t g_tcbSP0256;




//====================================================
//SP0256 task
//The SP0256 task has a circular buffer of phoneme data to emit.


//the circular buffer of phoneme data we are transmitting
SemaphoreHandle_t mtxTXbuff;	//note; FreeRTOS mutexes must never be used in an ISR
StaticSemaphore_t mtxbufTXbuff;
CIRCBUF(SP0256_txbuff,uint8_t,512);



//called into here when the nLRQ line falls; called at ISR time
void SP0256_GPIO_EXTI_Callback ( uint16_t GPIO_Pin )
{
	//because it is at ISR time, we don't do work here.  Rather,
	//we set a task notification bit and let the task do the work.
	if ( NULL != g_thSP0256 )	//(can get some strays during boot before we have a task)
	{
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		xTaskNotifyFromISR ( g_thSP0256, TNB_LRQ, eSetBits, &xHigherPriorityTaskWoken );
		portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
	}
}


//weak symbol for notification if the phoneme queue is depleted
__weak void SP0256_tbmt ( void )
{
	//nothing special by default
}



//write data
void _writeSP0256 ( uint8_t nData )
{
	//XXX is there an STM way to RMW a set of bits in the port?
	uint16_t nA = GPIOA->ODR;
	nA &= 0xffc0;
	nA |= (nData&0x3f);
	GPIOA->ODR = nA;
	//ts2 can be zero, so no delay here
}


//strobe 'address'
void _strobeSP0256 ( void )
{
	HAL_GPIO_WritePin(SP_nALD_GPIO_Port, SP_nALD_Pin, GPIO_PIN_RESET);
	//th2 can be as much as 1120 ns, so delay
	uint32_t nStart = DWT->CYCCNT;
	while ( DWT->CYCCNT - nStart < 81 ) ;
	HAL_GPIO_WritePin(SP_nALD_GPIO_Port, SP_nALD_Pin, GPIO_PIN_SET);
}



void _strobeSP0256_reset ( void )
{
	HAL_GPIO_WritePin(SP_RST_GPIO_Port, SP_RST_Pin, GPIO_PIN_SET);
	//tpw2 = 25 us
	uint32_t nStart = DWT->CYCCNT;
	while ( DWT->CYCCNT - nStart < 1800 ) ;
	HAL_GPIO_WritePin(SP_RST_GPIO_Port, SP_RST_Pin, GPIO_PIN_RESET);
}



void _writeandstrobeSP0256 ( uint8_t nData )
{
	_writeSP0256 ( nData );
	_strobeSP0256();
}



//these are debug methods for tuning buffer sizes
#ifdef DEBUG

unsigned int SP0256_queue_max ( void )
{
	//(it's not uber critical to take the mutex for this)
	return circbuff_max ( &SP0256_txbuff );
}

#endif



void SP0256_reset()
{
	if ( pdTRUE == xSemaphoreTake ( mtxTXbuff, pdMS_TO_TICKS(1000) ) )
	{
		circbuff_init ( &SP0256_txbuff );	//clear any pending stuff
		_strobeSP0256_reset();	//strobe the reset
		xSemaphoreGive ( mtxTXbuff );
	}
	else
	{}	//horror; timeout taking semaphore
}




//push data into the queue; returns the amount actually consumed
//we use a FreeRTOS mutex, so it must NOT be called from an ISR
size_t SP0256_push ( const uint8_t* abyPhonemes, size_t nSize )
{
	if ( NULL == abyPhonemes || 0 == nSize )	//silly case
		return 0;

	size_t nOrigSize = nSize;

	if ( pdTRUE == xSemaphoreTake ( mtxTXbuff, pdMS_TO_TICKS(1000) ) )
	{
		//int bSBY = ( GPIO_PIN_RESET != HAL_GPIO_ReadPin ( SP_SBY_GPIO_Port, SP_SBY_Pin ) );
		//int bnLRQ = ( GPIO_PIN_RESET != HAL_GPIO_ReadPin ( SP_nLRQ_GPIO_Port, SP_nLRQ_Pin ) );

		//while not empty and nLRQ asserted, feed from existing
		//items in the circular buffer first.
		while ( ! circbuff_empty ( &SP0256_txbuff ) && 
				( GPIO_PIN_RESET == HAL_GPIO_ReadPin ( SP_nLRQ_GPIO_Port, SP_nLRQ_Pin ) ) )
		{
			uint8_t by;
			circbuff_dequeue ( &SP0256_txbuff, &by );
			//write data and strobe
			_writeandstrobeSP0256 ( by );
		}

		//now, while we have some of our own stuff, but only if there
		//is not existing stuff in the circular buffer, then pluck from
		//the front of our stuff directly and feed it in.  do this until
		//the SP0256 cries uncle or we run out of stuff.
		while ( 0 != nSize && 
				circbuff_empty ( &SP0256_txbuff ) &&
				( GPIO_PIN_RESET == HAL_GPIO_ReadPin ( SP_nLRQ_GPIO_Port, SP_nLRQ_Pin ) ) )
		{
			//pluck the first char, which we will send right now
			uint8_t by = abyPhonemes[0];
			++abyPhonemes;
			--nSize;
			//write data and strobe
			_writeandstrobeSP0256 ( by );
		}

		//finally, any remaining stuff must be enqueued for later
		while ( 0 != nSize && 
				circbuff_enqueue ( &SP0256_txbuff, &abyPhonemes[0] ) )
		{
			++abyPhonemes;
			--nSize;
		}

		xSemaphoreGive ( mtxTXbuff );
	}
	else
	{}	//horror; timeout taking semaphore

	return nOrigSize - nSize;
}



//called once at boot to get things ready
void SP0256_Initialize ( void )
{
	//init our circular buffer
	circbuff_init(&SP0256_txbuff);
	mtxTXbuff = xSemaphoreCreateMutexStatic ( &mtxbufTXbuff );
	_strobeSP0256_reset();	//reset the device
}



void thrdfxnSP0256Task ( void const* argument )
{
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
			//when nLRQ falls we get a task notification rather than handle the
			//request in the ISR itself.
			if ( ulNotificationValue & TNB_LRQ )
			{
				if ( pdTRUE == xSemaphoreTake ( mtxTXbuff, pdMS_TO_TICKS(1000) ) )
				{
					//while not empty and nLRQ asserted, feed from existing
					//items in the circular buffer.
					while ( ! circbuff_empty ( &SP0256_txbuff ) && 
							( GPIO_PIN_RESET == HAL_GPIO_ReadPin ( SP_nLRQ_GPIO_Port, SP_nLRQ_Pin ) ) )
					{
						uint8_t by;
						circbuff_dequeue ( &SP0256_txbuff, &by );
						//write data and strobe
						_writeandstrobeSP0256 ( by );
					}
					int bEmpty = circbuff_empty ( &SP0256_txbuff );
					xSemaphoreGive ( mtxTXbuff );
					if ( bEmpty )
					{
						SP0256_tbmt();	//notify anyone interested
					}
				}
				else
				{}	//horror; timeout taking semaphore
			}

		//(other task notification bits)

		}
	}
}



