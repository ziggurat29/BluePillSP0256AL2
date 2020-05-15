

#include "task_monitor.h"
#include "command_processor.h"
#include "task_notification_bits.h"
#include "BluePillSP0256AL2_commands.h"
#include "BluePillSP0256AL2_settings.h"


#include <string.h>


//the task that runs an interactive monitor on the USB data
osThreadId g_thMonitor = NULL;
uint32_t g_tbMonitor[ 128 ];
osStaticThreadDef_t g_tcbMonitor;


const IOStreamIF* g_pMonitorIOIf = NULL;	//the IO device to which the monitor is attached



//====================================================
//Monitor task
//The monitor handles the serial port presented by a stream interface to which
//we are bound.  Since this project has several 'modes', it is somewhat easier
//to implement if we switch our behaviour on mode, rather than provide for
//unbinding of the stream interface.
//The three modes we support are:
//0 -- command processor (default)
//  This mode interprets interactive commands from a serial terminal.
//2 -- serial-to-SP0256-AL2
//  This mode accepts binary data from the stream and forwards it to the SP0256
//  task, with XON/XOFF handshaking.
//3 -- text-to-speech
//  This mode accepts text over the stream and performs text-to-speech,
//  forwarding the generated phonemes to the SP0256, with XON/XOFF handshaking.



void Monitor_Initialize ( void )
{
	//(XXX nothing yet)
}



//client connect; optional
void Monitor_ClientConnect ( int bIsConnect )
{
	if ( bIsConnect )
	{
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		xTaskNotifyFromISR ( g_thMonitor, TNB_MON_CLIENT_CONNECT, eSetBits, &xHigherPriorityTaskWoken );
		portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
	}
	else
	{
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		xTaskNotifyFromISR ( g_thMonitor, TNB_MON_CLIENT_DISCONNECT, eSetBits, &xHigherPriorityTaskWoken );
		portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
	}
}



//DAV; data is available on the stream interface
void Monitor_DAV ( void )
{
	if ( NULL != g_thMonitor )	//only if we have a notificand
	{
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		xTaskNotifyFromISR ( g_thMonitor, TNB_MON_DAV, eSetBits, &xHigherPriorityTaskWoken );
		portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
	}
}



//TBMT; 
void Monitor_TBMT ( void )
{
	if ( NULL != g_thMonitor )	//only if we have a notificand
	{
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		xTaskNotifyFromISR ( g_thMonitor, TNB_MON_TBMT, eSetBits, &xHigherPriorityTaskWoken );
		portYIELD_FROM_ISR( xHigherPriorityTaskWoken );
	}
}



//implementation for the command processor; bind IO to a stream


void thrdfxnMonitorTask ( void const* argument )
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
			//if we got a new client connection, do a greeting
			if ( ulNotificationValue & TNB_MON_CLIENT_CONNECT )
			{
				enum MONMODE eMode = Settings_getStruct()->_eMM;
				if ( MM_CMD == eMode )
				{
					CWCMD_SendGreeting ( g_pMonitorIOIf );
					CWCMD_SendPrompt ( g_pMonitorIOIf );
				}
				//XXX else
			}
			if ( ulNotificationValue & TNB_MON_DAV )
			{
				enum MONMODE eMode = Settings_getStruct()->_eMM;
				if ( MM_CMD == eMode )
				{
					//we use the non-blocking version in this notification loop
					CMDPROC_process_nb ( g_pMonitorIOIf, g_aceCommands, g_nAceCommands );
				}
				//XXX else
			}
			if ( ulNotificationValue & TNB_MON_SETMODE )
			{
				//extract parameter field as mode
				enum MONMODE eMode = (enum MONMODE)( ulNotificationValue & 0x0000ffff );
				Settings_getStruct()->_eMM = eMode;
			}
		}
	}
}



