//the task that runs the SP0256-AL2 interface

#ifndef __TASK_SP0256_H
#define __TASK_SP0256_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cmsis_os.h"
#include "task_notification_bits.h"

//FreeRTOS task stuff
extern osThreadId g_thSP0256;
extern uint32_t g_tbSP0256[ 128 ];
extern osStaticThreadDef_t g_tcbSP0256;

void thrdfxnSP0256Task ( void const* argument );



//called once at reset to get things ready; must be done first
void SP0256_Initialize ( void );

//ISR calls into here when the nLRQ line falls
void SP0256_GPIO_EXTI_Callback ( uint16_t GPIO_Pin );


//the following must NOT be called from ISR due to the use of FreeRTOS mutex

//discard pending and strobe RESET
void SP0256_reset();

//push data into the queue; returns the amount actually consumed
size_t SP0256_push ( const uint8_t* abyPhonemes, size_t nSize );


//you may implement this if you want it
void SP0256_tbmt ( void );	//the phoneme queue is depleted


#ifdef __cplusplus
}
#endif

#endif
