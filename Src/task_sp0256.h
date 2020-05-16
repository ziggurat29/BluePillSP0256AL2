//the task that runs the SP0256-AL2 interface

#ifndef __TASK_SP0256_H
#define __TASK_SP0256_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cmsis_os.h"
#include "BluePillSP0256AL2_settings.h"


//FreeRTOS task stuff
extern osThreadId g_thSP0256;
extern uint32_t g_tbSP0256[ 128 ];
extern osStaticThreadDef_t g_tcbSP0256;

void thrdfxnSP0256Task ( void const* argument );



//called once at reset to get things ready; must be done first
void SP0256_Initialize ( void );

//ISR calls into here when the nLRQ line falls
void SP0256_GPIO_EXTI_Callback ( uint16_t GPIO_Pin );

//ISR calls into here when TIM4 fires for our sample clock
void SP0256_TIM4_Callback ( void );

//ISR calls into here when DMA is half-complete
void SP0256_DMA_1_7_HALFCPL_Callback ( void );

//ISR calls into here when DMA is fully complete
void SP0256_DMA_1_7_CPL_Callback ( void );



//the following must NOT be called from ISR due to the use of FreeRTOS mutex

//set the current mode of operation
void SP0256_setMode ( enum SP0256MODE eSM );

//discard pending and strobe RESET
void SP0256_reset();

//push data into the queue; returns the amount actually consumed
size_t SP0256_push ( const uint8_t* abyPhonemes, size_t nSize );


//you may implement this if you want it
void SP0256_tbmt ( void );	//the phoneme queue is depleted


//these are debug methods for tuning buffer sizes
#ifdef DEBUG
unsigned int SP0256_queue_max ( void );
#endif


#ifdef __cplusplus
}
#endif

#endif
