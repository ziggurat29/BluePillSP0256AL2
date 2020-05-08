//==============================================================
//This various task notification values used for IPC in the system
//This module is part of the BluePillSP0256AL2 project.

#ifndef __TASK_NOTIFCATION_BITS_H
#define __TASK_NOTIFCATION_BITS_H

#ifdef __cplusplus
extern "C" {
#endif



//These bits are used for task notifications of events.
//In FreeRTOS, task notifications are much more resource-friendly than the
//heavyweights, like semaphores, though not as general.  But for most cases the
//task notifications will work fine.  (They can only support notification to a
//single process, and communicate via bitfield.  They are most similar to
//an 'event group', but can used to emulate other primitives.)

//There could be different definitions specific to the related processes, but I
//am just going to use one common definition since there are only a few
//distinct events.
typedef enum TaskNotificationBits TaskNotificationBits;
enum TaskNotificationBits
{
	//these are generally used for byte streams events
	TNB_DAV = 0x00000001,	//data is available (serial receive)
	TNB_TBMT = 0x00000002,	//transmit buffer is empty (serial send)
	//0x00000004,	//reserved; maybe for errors?
	//0x00000008,	//reserved; maybe for errors?

	//bits for the default process
	TNB_LIGHTSCHANGED = 0x00010000,		//the lights have changed

	//bits for the monitor; the lower 16 bits are a parameter value for
	//notifications that take a parameter, so be careful about how you
	//send these to the Monitor task.
	TNB_MON_CLIENT_CONNECT = 0x00010000,	//a client has (probably) connected
	TNB_MON_CLIENT_DISCONNECT = 0x00020000,	//a client has (probably) disconnected
	TNB_MON_DAV = 0x00040000,	//data is available on the stream
	TNB_MON_TBMT = 0x00080000,	//transmit buffer is empty on the stream
	TNB_MON_SETMODE = 0x00100000,	//enter mode specified by lower 16 bits

	//bits for the SP0256 task
	TNB_LRQ = 0x00010000,		//load request transitioned low (asserted)
};



#ifdef __cplusplus
}
#endif

#endif
