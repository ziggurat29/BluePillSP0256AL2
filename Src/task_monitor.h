//the task that runs an interactive monitor on the USB data

#ifndef __TASK_MONITOR_H
#define __TASK_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cmsis_os.h"
#include "system_interfaces.h"

extern osThreadId g_thMonitor;
extern uint32_t g_tbMonitor[ 128 ];
extern osStaticThreadDef_t g_tcbMonitor;

void thrdfxnMonitorTask ( void const* argument );


//called once at reset to get things ready; must be done first
void Monitor_Initialize ( void );

//the IO device to which the monitor is attached; YOU must bind when your
//stream is ready for use
extern const IOStreamIF* g_pMonitorIOIf;


//Notifications; YOU call these when these events occur

//client connect; optional
void Monitor_ClientConnect ( int bIsConnect );
//DAV; data is available on the stream interface; required because we are
//implement with the non-blocking command processor interface
void Monitor_DAV ( void );
//TBMT; 
void Monitor_TBMT ( void );


#ifdef __cplusplus
}
#endif

#endif
