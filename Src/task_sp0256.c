

#include "main.h"
#include "cmsis_os.h"

#include "task_sp0256.h"
#include "util_circbuff2.h"
#include "util_altlib.h"

#include "phonemes_adpcm.h"
#include "adpcm.h"

#include "task_notification_bits.h"



extern TIM_HandleTypeDef htim3;	//for PWM
extern TIM_HandleTypeDef htim4;	//for sample clock
extern DMA_HandleTypeDef hdma_tim4_up;	//for the DMA sample driver




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


//effective mode: physical, simulated
enum SP0256MODE g_eSM = SM_PHYSICAL;



//weak symbol for notification if the phoneme queue is depleted
__weak void SP0256_tbmt ( void )
{
	//nothing special by default
}



//====================================================
//physical SP0256 interface


//called into here when the nLRQ line falls; called at ISR time
void SP0256_GPIO_EXTI_Callback ( uint16_t GPIO_Pin )
{
	//because it is at ISR time, we don't do work here.  Rather,
	//we set a task notification bit and let the task do the work.
	if ( NULL != g_thSP0256 )	//(can get some strays during boot before we have a task)
	{
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		xTaskNotifyFromISR ( g_thSP0256, TNB_SP_LRQ, eSetBits, &xHigherPriorityTaskWoken );
		portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
	}
}


//write data
void _writeData_physical ( uint8_t nData )
{
	//XXX is there an STM way to RMW a set of bits in the port?
	uint16_t nA = GPIOA->ODR;
	nA &= 0xffc0;
	nA |= (nData&0x3f);
	GPIOA->ODR = nA;
	//ts2 can be zero, so no delay here
}


//strobe 'address'
void _strobeData_physical ( void )
{
	HAL_GPIO_WritePin(SP_nALD_GPIO_Port, SP_nALD_Pin, GPIO_PIN_RESET);
	//th2 can be as much as 1120 ns, so delay
	uint32_t nStart = DWT->CYCCNT;
	while ( DWT->CYCCNT - nStart < 81 ) ;
	HAL_GPIO_WritePin(SP_nALD_GPIO_Port, SP_nALD_Pin, GPIO_PIN_SET);
}



void _strobeReset_physical ( void )
{
	HAL_GPIO_WritePin(SP_RST_GPIO_Port, SP_RST_Pin, GPIO_PIN_SET);
	//tpw2 = 25 us
	uint32_t nStart = DWT->CYCCNT;
	while ( DWT->CYCCNT - nStart < 1800 ) ;
	HAL_GPIO_WritePin(SP_RST_GPIO_Port, SP_RST_Pin, GPIO_PIN_RESET);
}



void _writeandstrobe_physical ( uint8_t nData )
{
	_writeData_physical ( nData );
	_strobeData_physical();
}



//int bSBY = ( GPIO_PIN_RESET != HAL_GPIO_ReadPin ( SP_SBY_GPIO_Port, SP_SBY_Pin ) );
//int bnLRQ = ( GPIO_PIN_RESET != HAL_GPIO_ReadPin ( SP_nLRQ_GPIO_Port, SP_nLRQ_Pin ) );

//is the nLRQ line asserted? (low)
int _isLRQ_physical ( void )
{
	return ( GPIO_PIN_RESET == HAL_GPIO_ReadPin ( SP_nLRQ_GPIO_Port, SP_nLRQ_Pin ) );
}



//====================================================
//simulated SP0256 interface via PWM and recorded (adpcm) audio


//we have two sample buffers.  One will be 'active' and playing via the
//sample output driving mechanism.  The other will be 'pending' and
//filled when the active buffer gets low (signalled by a 'half complete'
//task notification).
//Note that the gist of this scheme is that the buffer filling operation
//is 'free-wheeling', i.e. the hardware-based driving mechanism is
//expecting the task to be able to keep up with always providing a feed
//of buffers if it is expecting continuous output.  It is expected that
//the sample driver can freely access the 'active' buffer without
//interlocks, and that the task can freely access the 'pending' buffer
//without locks.  Locks are performed only when switching active/pending.

typedef struct SampleBuffer
{
	uint8_t	_abySamples[512];	//@ 11025 this is about 46.5 ms
	int	_len;	//how many are in this buffer
	int	_idx;	//where we are outputting next
} SampleBuffer;

SampleBuffer g_asb[2];	//our two sample buffers
int g_nActive = 1;	//which buffer is active (playing) right now
int g_nUsed = 0;	//how many buffers have been used


//adpcm decompressor
ADPCMstate g_adpcm = { 0, 0 };
int g_phone = 0;		//our current phoneme; -1 means 'none'
int g_idxPhoneNyb = 0;	//our current nybble


//our phoneme fifo
CIRCBUF(g_SP0256_fifo,uint8_t,16);



//this callback is used only when we are timer interrupt driven
void SP0256_TIM4_Callback ( void )
{
	//if we are simulating, and we have samples, send it out

	UBaseType_t uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();
	if ( 0 == g_nUsed )	//no buffers in use?
	{
		taskEXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
		return;
	}

	//see if we need to swap
	SampleBuffer* pbuff = &g_asb[g_nActive];

	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, pbuff->_abySamples[pbuff->_idx]);
	++pbuff->_idx;
	if ( pbuff->_idx == pbuff->_len/2 )	//transfer is half-complete
	{
		if ( NULL != g_thSP0256 )	//(can get some strays during boot before we have a task)
		{
			BaseType_t xHigherPriorityTaskWoken = pdFALSE;
			xTaskNotifyFromISR ( g_thSP0256, TNB_SP_PREPBUFF, eSetBits, &xHigherPriorityTaskWoken );
			portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
		}
	}
	else if ( pbuff->_idx >= pbuff->_len )	//we must swap
	{
		//set this buffer as empty
		pbuff->_len = 0;
		pbuff->_idx = 0;
		g_nActive += 1;	//next buffer
		if ( g_nActive >= COUNTOF(g_asb) )	//wrap
			g_nActive = 0;
		--g_nUsed;	//one down

		if ( 0 == g_nUsed )	//now no buffers in use? xfer complete
		{
			if ( NULL != g_thSP0256 )	//(can get some strays during boot before we have a task)
			{
				BaseType_t xHigherPriorityTaskWoken = pdFALSE;
				xTaskNotifyFromISR ( g_thSP0256, TNB_SP_DONE, eSetBits, &xHigherPriorityTaskWoken );
				portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
			}
		}
	}

	taskEXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
}



//these callbacks are used only when we are DMA drive
void SP0256_DMA_1_7_HALFCPL_Callback ( void )
{
	if ( NULL != g_thSP0256 )	//(can get some strays during boot before we have a task)
	{
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		xTaskNotifyFromISR ( g_thSP0256, TNB_SP_PREPBUFF, eSetBits, &xHigherPriorityTaskWoken );
		portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
	}
}



//these callbacks are used only when we are DMA drive
void SP0256_DMA_1_7_CPL_Callback ( void )
{
	UBaseType_t uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();

	SampleBuffer* pbuff = &g_asb[g_nActive];	//get this (completed) buffer

	//set this buffer as empty
	pbuff->_len = 0;
	pbuff->_idx = 0;
	g_nActive += 1;	//next buffer
	if ( g_nActive >= COUNTOF(g_asb) )	//wrap
		g_nActive = 0;
	--g_nUsed;	//one down

	if ( 0 == g_nUsed )	//now no buffers in use? xfer complete
	{
		if ( NULL != g_thSP0256 )	//(can get some strays during boot before we have a task)
		{
			BaseType_t xHigherPriorityTaskWoken = pdFALSE;
			xTaskNotifyFromISR ( g_thSP0256, TNB_SP_DONE, eSetBits, &xHigherPriorityTaskWoken );
			portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
		}
	}
	else
	{
		pbuff = &g_asb[g_nActive];	//get the next buffer
		HAL_DMA_Start_IT(&hdma_tim4_up, 
				(uint32_t)&(pbuff->_abySamples), 
				(uint32_t)&htim3.Instance->CCR3, 
				pbuff->_len);
		//XXX required? __HAL_TIM_ENABLE_DMA ( &htim4, TIM_DMA_UPDATE );
	}

	taskEXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
}




//_prepareNextBufferLoad
//would it be smarter to instead mask the interrupt source from the sample driver?
//	taskENTER_CRITICAL();
//	taskEXIT_CRITICAL();
//HAL_NVIC_EnableIRQ(TIM4_IRQn);
//__HAL_TIM_ENABLE_IT(htim4, TIM_IT_UPDATE);
//#define __HAL_TIM_ENABLE_IT(__HANDLE__, __INTERRUPT__)    ((__HANDLE__)->Instance->DIER |= (__INTERRUPT__))
//#define __HAL_TIM_DISABLE_IT(__HANDLE__, __INTERRUPT__)   ((__HANDLE__)->Instance->DIER &= ~(__INTERRUPT__))



void _prepareNextBufferLoad ( void )
{
	//get the next buffer we can fill (if possible)
	taskENTER_CRITICAL();
	if ( COUNTOF(g_asb) == g_nUsed )	//all buffers in use?
	{
		//can happen when we're full and we're putting stuff in the fifo
		taskEXIT_CRITICAL();
		return;
	}
	int nNext = g_nActive + 1;	//next buffer
	if ( nNext >= COUNTOF(g_asb) )	//wrap
		nNext = 0;
	SampleBuffer* pbuff = &g_asb[nNext];	//our next buffer
	taskEXIT_CRITICAL();

	//OK, fill this buffer to the extent possible
	int bDidFill = 0;
	int bContinue = 1;
	while ( bContinue )
	{
		bContinue = 0;	//one pass unless we need to try again

		//is a buffer fill already in progress?  continue
		if ( g_phone >= 0 )
		{
			//decompress this phoneme as much as possible
			const struct PhonemeEntry* ppe = &g_apePhonemes[g_phone];
			while ( pbuff->_len < COUNTOF(pbuff->_abySamples) )
			{
				int code = ppe->_pbyADPCM[g_idxPhoneNyb>>1];
				if ( g_idxPhoneNyb & 1 )
					code &= 0x0f;
				else	//upper nybble first
					code >>= 4;

				int samp = adpcm_decode_sample ( code, &g_adpcm );
				uint8_t samp8 = (uint8_t) ( ( samp / 256 ) + 128 );

				pbuff->_abySamples[pbuff->_len] = samp8;	//stick sample in
				++pbuff->_len;
				bDidFill = 1;

				++g_idxPhoneNyb;	//next nybble
				if ( ppe->_nLenUnc == g_idxPhoneNyb )	//finished with this phoneme
				{
					g_phone = -1;
					g_idxPhoneNyb = 0;		//reset decompressor
					g_adpcm.prevsample = 0;
					g_adpcm.previndex = 0;
					break;
				}
			}
		}

		//is there more space?
		if ( pbuff->_len < COUNTOF(pbuff->_abySamples) )
		{
			//are there more phonemes?
			if ( pdTRUE == xSemaphoreTake ( mtxTXbuff, pdMS_TO_TICKS(1000) ) )
			{
				if ( ! circbuff_empty ( &g_SP0256_fifo ) )
				{
					int lrqBefore = ! circbuff_full ( &g_SP0256_fifo );
					//get one from the fifo, if any
					uint8_t by;
					circbuff_dequeue ( &g_SP0256_fifo, &by );
					g_phone = by;
					bContinue = 1;
					if ( ! lrqBefore )	//if not LRQ before, surely LRQ now
					{
						xTaskNotify ( g_thSP0256, TNB_SP_LRQ, eSetBits );
					}
				}
				else
				{}
				xSemaphoreGive ( mtxTXbuff );
			}
			else
			{}	//horror; timeout taking semaphore
		}
		else
		{}
	}

	//bump the number of buffers
	taskENTER_CRITICAL();
	if ( bDidFill )
	{
		if ( 0 == g_nUsed )
		{
			//must prime the pump
			g_nActive = nNext;
			HAL_DMA_Start_IT(&hdma_tim4_up, 
					(uint32_t)&(g_asb[g_nActive]._abySamples), 
					(uint32_t)&htim3.Instance->CCR3, 
					g_asb[g_nActive]._len);
			//XXX required? __HAL_TIM_ENABLE_DMA ( &htim4, TIM_DMA_UPDATE );
		}
		++g_nUsed;
	}
	taskEXIT_CRITICAL();
}




void _strobeReset_simulated ( void )
{
	//abort any PWM, dump any buffer, return PWM to '0' (128)
	taskENTER_CRITICAL();

	//XXX required? __HAL_TIM_DISABLE_DMA ( &htim4, TIM_DMA_UPDATE );	//stop triggering DMA
	HAL_DMA_Abort_IT(&hdma_tim4_up);	//stop DMA'ing (and unlock DMA)
	//XXX required? HAL_TIM_Base_Stop(&htim4);	//stop clocking

	g_asb[0]._len = 0;	//'empty' our buffers
	g_asb[0]._idx = 0;	//arbitrarily reset
	g_asb[1]._len = 0;
	g_asb[1]._idx = 0;
	g_nActive = 0;	//arbitrarily reset
	g_nUsed = 0;
	g_adpcm.prevsample = 0;
	g_adpcm.previndex = 0;
	g_phone = -1;
	g_idxPhoneNyb = 0;
	__HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 128);
	circbuff_init(&g_SP0256_fifo);	//NOTE!  we are already in the mutex
	taskEXIT_CRITICAL();
}



void _writeandstrobe_simulated ( uint8_t nData )
{
	//NOTE!  we are already in the mutex
	circbuff_enqueue ( &g_SP0256_fifo, &nData );
	//use task notification to cause the bufferload to be prepared (maybe)
	xTaskNotify ( g_thSP0256, TNB_SP_PREPBUFF, eSetBits );
}



int _isLRQ_simulated ( void )
{
	//NOTE!  we are already in the mutex
	return ! circbuff_full ( &g_SP0256_fifo );
}



//====================================================
//generalized SP0256 interface to either physical or simulated synth


void _strobeReset_SP0256 ( void )
{
	if ( SM_PHYSICAL == g_eSM )
	{
		_strobeReset_physical();
	}
	else	//(we only have two modes)
	{
		_strobeReset_simulated();
	}
}


void _writeandstrobe_SP0256 ( uint8_t nData )
{
	if ( SM_PHYSICAL == g_eSM )
	{
		_writeandstrobe_physical ( nData );
	}
	else	//(we only have two modes)
	{
		_writeandstrobe_simulated ( nData );
	}
}


int _isLRQ_SP0256 ( void )
{
	if ( SM_PHYSICAL == g_eSM )
	{
		return _isLRQ_physical();
	}
	else	//(we only have two modes)
	{
		return _isLRQ_simulated();
	}
}


//====================================================
//these are debug methods for tuning buffer sizes
#ifdef DEBUG

unsigned int SP0256_queue_max ( void )
{
	//(it's not uber critical to take the mutex for this)
	return circbuff_max ( &SP0256_txbuff );
}

#endif



//called once at boot to get things ready
void SP0256_Initialize ( void )
{
	//init our circular buffer
	circbuff_init(&SP0256_txbuff);
	mtxTXbuff = xSemaphoreCreateMutexStatic ( &mtxbufTXbuff );
	g_eSM = Settings_getStruct()->_eSM;	//get mode from persistent state
	_strobeReset_SP0256();	//reset the device
}



void SP0256_reset()
{
	if ( pdTRUE == xSemaphoreTake ( mtxTXbuff, pdMS_TO_TICKS(1000) ) )
	{
		circbuff_init ( &SP0256_txbuff );	//clear any pending stuff
		_strobeReset_SP0256();	//strobe the reset
		xSemaphoreGive ( mtxTXbuff );
	}
	else
	{}	//horror; timeout taking semaphore
}



//set our current mode of operation
void SP0256_setMode ( enum SP0256MODE eSM )
{
	SP0256_reset();	//reset our current mode
	g_eSM = eSM;	//change effective mode
	SP0256_reset();	//reset into new mode
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
		//while not empty and nLRQ asserted, feed from existing
		//items in the circular buffer first.
		while ( ! circbuff_empty ( &SP0256_txbuff ) && _isLRQ_SP0256() )
		{
			uint8_t by;
			circbuff_dequeue ( &SP0256_txbuff, &by );
			//write data and strobe
			_writeandstrobe_SP0256 ( by );
		}

		//now, while we have some of our own stuff, but only if there
		//is not existing stuff in the circular buffer, then pluck from
		//the front of our stuff directly and feed it in.  do this until
		//the SP0256 cries uncle or we run out of stuff.
		while ( 0 != nSize && 
				circbuff_empty ( &SP0256_txbuff ) &&
				_isLRQ_SP0256() )
		{
			//pluck the first char, which we will send right now
			uint8_t by = abyPhonemes[0];
			++abyPhonemes;
			--nSize;
			//write data and strobe
			_writeandstrobe_SP0256 ( by );
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



//our task (thread) function.  we do most of the real work in this task, rather
//than the ISR (to the extent possible).  Because this is time sensitive, this
//task is created with high priority.
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
			if ( ulNotificationValue & TNB_SP_LRQ )
			{
				if ( pdTRUE == xSemaphoreTake ( mtxTXbuff, pdMS_TO_TICKS(1000) ) )
				{
					//while not empty and nLRQ asserted, feed from existing
					//items in the circular buffer.
					while ( ! circbuff_empty ( &SP0256_txbuff ) && _isLRQ_SP0256() )
					{
						uint8_t by;
						circbuff_dequeue ( &SP0256_txbuff, &by );
						//write data and strobe
						_writeandstrobe_SP0256 ( by );
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

		//not would be a great time to get a move on and prepare a buffer
		//load of samples before we need them at ISR time
		if ( ulNotificationValue & TNB_SP_PREPBUFF )
		{
			_prepareNextBufferLoad();
		}

		//the samples transfer is fully complete.
		if ( ulNotificationValue & TNB_SP_DONE )
		{
			//we don't really do anything here at present
		}

		//(other task notification bits)

		}
	}
}



