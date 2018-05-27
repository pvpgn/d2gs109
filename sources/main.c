/*
 * main.c: main routine of this program
 *
 * 2001-08-20 faster
 *   add initialization routine and main loop of this program
 */

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <conio.h>
#include "d2gelib/d2server.h"
#include "d2gs.h"
#include "eventlog.h"
#include "vars.h"
#include "config.h"
#include "d2ge.h"
#include "net.h"
#include "timer.h"
#include "telnetd.h"
#include "d2gamelist.h"
#include "handle_s2s.h"


 /* function declarations */
int  DoCleanup(void);
BOOL D2GSCheckRunning(void);
int  CleanupRoutineForServerMutex(void);
/* CTRL+C or CTRL+Break signal handler */
BOOL WINAPI ControlHandler(DWORD dwCtrlType);


/* some variables used just in this file */
static HANDLE			hD2GSMutex = NULL;
static HANDLE			hStopEvent = NULL;
static CLEANUP_RT_ITEM	*pCleanupRT = NULL;


/********************************************************************************
 * Main procedure begins here
 ********************************************************************************/
#ifdef _DEBUG
int main(int argc, char **argv)
#else
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInst, LPSTR lpCmdLine, int nCmdShow)
#endif
{
	DWORD	dwWait;

	/* reset cleanup routine list */
	pCleanupRT = NULL;

	/* init log system first */
	if (!D2GSEventLogInitialize()) return -1;

	/* setup signal capture */
	SetConsoleCtrlHandler(ControlHandler, TRUE);

	/* check if another instance is running */
	if (D2GSCheckRunning()) {
		D2GSEventLog("main", "Seems another server is running");
		DoCleanup();
		return -1;
	}

	/* create a name event, for "d2gssvc" server controler to terminate me */
	hStopEvent = CreateEvent(NULL, TRUE, FALSE, D2GS_STOP_EVENT_NAME);
	if (!hStopEvent) {
		D2GSEventLog("main", "failed create stop event object");
		DoCleanup();
		return -1;
	}

	/* init variables */
	if (!D2GSVarsInitialize()) {
		D2GSEventLog("main", "Failed initialize global variables");
		DoCleanup();
		return -1;
	}

	/* read configurations */
	if (!D2GSReadConfig()) {
		D2GSEventLog("main", "Failed getting configurations from registry");
		DoCleanup();
		return -1;
	}

	/* create timer */
	if (!D2GSTimerInitialize()) {
		D2GSEventLog("main", "Failed Startup Timer");
		DoCleanup();
		return -1;
	}

	/* start up game engine */
	if (!D2GEStartup()) {
		D2GSEventLog("main", "Failed Startup Game Engine");
		DoCleanup();
		return -1;
	}

	/* initialize the net connection */
	if (!D2GSNetInitialize()) {
		D2GSEventLog("main", "Failed Startup Net Connector");
		DoCleanup();
		return -1;
	}

	/* administration console */
	if (!D2GSAdminInitialize()) {
		D2GSEventLog("main", "Failed Startup Administration Console");
		DoCleanup();
		return -1;
	}

	/* main server loop */
	D2GSEventLog("main", "Entering Main Server Loop");
	while (TRUE) {
		dwWait = WaitForSingleObject(hStopEvent, 1000);
		if (dwWait != WAIT_OBJECT_0) continue;
		/* service controler tell me to stop now. "Yes, sir!" */
		D2GSSetD2CSMaxGameNumber(0);
		D2GSActive(FALSE);
		D2GSEventLog("main", "I am going to stop");
		D2GSEndAllGames();
		Sleep(3000);
		break;
	}

	/*DoCleanup();*/
	return 0;

} /* End of main() */


/*********************************************************************
 * Purpose: to add an cleanup routine item to the list
 * Return: TRUE(success) or FALSE(failed)
 *********************************************************************/
int CleanupRoutineInsert(CLEANUP_ROUTINE pRoutine, char *comment)
{
	CLEANUP_RT_ITEM		*pitem;

	if (pRoutine == NULL) return FALSE;
	pitem = (CLEANUP_RT_ITEM *)malloc(sizeof(CLEANUP_RT_ITEM));
	if (!pitem) {
		D2GSEventLog("CleanupRoutineInsert", "Can't alloc memory");
		return FALSE;
	}
	ZeroMemory(pitem, sizeof(CLEANUP_RT_ITEM));

	/* fill the structure */
	if (comment)
		strncpy(pitem->comment, comment, sizeof(pitem->comment) - 1);
	else
		strncpy(pitem->comment, "unknown", sizeof(pitem->comment) - 1);
	pitem->cleanup = pRoutine;
	pitem->next = pCleanupRT;
	pCleanupRT = pitem;

	return TRUE;

} /* End of CleanupRoutineInsert() */


/*********************************************************************
 * Purpose: call the cleanup routine to do real cleanup work
 * Return: TRUE or FALSE
 *********************************************************************/
int DoCleanup(void)
{
	CLEANUP_RT_ITEM		*pitem, *pprev;

	pitem = pCleanupRT;
	while (pitem)
	{
		D2GSEventLog("DoCleanup", "Calling cleanup routine '%s'", pitem->comment);
		pitem->cleanup();
		pprev = pitem;
		pitem = pitem->next;
		free(pprev);
	}
	pCleanupRT = NULL;

	/* at last, cleanup event log system */
	D2GSEventLog("DoCleanup", "Cleanup done.");
	D2GSEventLogCleanup();

	/* Close the mutex */
	if (hD2GSMutex)	CloseHandle(hD2GSMutex);
	if (hStopEvent) CloseHandle(hStopEvent);

#ifdef DEBUG_ON_CONSOLE
	printf("Press Any Key to Continue");
	_getch();
#endif

	return TRUE;

} /* End of DoCleanup() */


/*********************************************************************
 * Purpose: check if other instance is running
 * Return: TRUE(server is running) or FALSE(not running)
 *********************************************************************/
BOOL D2GSCheckRunning(void)
{
	HANDLE	hMutex;

	hD2GSMutex = NULL;
	hMutex = CreateMutex(NULL, TRUE, D2GSERVER_MUTEX_NAME);
	if (hMutex == NULL) {
		return TRUE;
	}
	else if (GetLastError() == ERROR_ALREADY_EXISTS) {
		CloseHandle(hMutex);
		return TRUE;
	}
	else {
		if (CleanupRoutineInsert(CleanupRoutineForServerMutex, "Server Mutex")) {
			hD2GSMutex = hMutex;
			return FALSE;
		}
		else {
			/* insert cleanup routine failed, assume server is running */
			CloseHandle(hMutex);
			return TRUE;
		}
	}

} /* End of D2GServerCheckRun() */


/*********************************************************************
 * Purpose: cleanup routine for release the global server mutex
 * Return: TRUE or FALSE
 *********************************************************************/
int CleanupRoutineForServerMutex(void)
{
	if (!hD2GSMutex) return FALSE;
	return CloseHandle(hD2GSMutex);

} /* End of CleanupRoutineServerMutex() */


/*********************************************************************
 * Purpose: catch CTRL+C or CTRL+Break signal
 * Return: TRUE or FALSE
 *********************************************************************/
BOOL WINAPI ControlHandler(DWORD dwCtrlType)
{
	switch (dwCtrlType)
	{
	case CTRL_BREAK_EVENT:  // use Ctrl+C or Ctrl+Break to simulate
	case CTRL_C_EVENT:      // SERVICE_CONTROL_STOP in debug mode
		D2GSEventLog("ControlHandler", "CTRL_BREAK or CTRL_C event caught");
		DoCleanup();
		ExitProcess(0);
		return TRUE;
		break;
	}
	return FALSE;

} /* End of ControlHandler */


/*********************************************************************
 * Purpose: to close the server mutex
 * Return: none
 *********************************************************************/
void CloseServerMutex(void)
{
	if (hD2GSMutex) CloseHandle(hD2GSMutex);
	hD2GSMutex = NULL;

} /* End of CloseServerMutex() */

