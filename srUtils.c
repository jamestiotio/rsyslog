/**\file srUtils.c
 * \brief General utilties that fit nowhere else.
 *
 * The namespace for this file is "srUtil".
 *
 * \author  Rainer Gerhards <rgerhards@adiscon.com>
 * \date    2003-09-09
 *          Coding begun.
 *
 * Copyright 2003-2008 Rainer Gerhards and Adiscon GmbH.
 *
 * This file is part of rsyslog.
 *
 * Rsyslog is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Rsyslog is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Rsyslog.  If not, see <http://www.gnu.org/licenses/>.
 *
 * A copy of the GPL can be found in the file "COPYING" in this distribution.
 */
#include "config.h"

#include "rsyslog.h"	/* THIS IS A MODIFICATION FOR RSYSLOG! 2004-11-18 rgerards */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <assert.h>
#include <sys/wait.h>
#include <ctype.h>
#include "liblogging-stub.h"	/* THIS IS A MODIFICATION FOR RSYSLOG! 2004-11-18 rgerards */
#define TRUE 1
#define FALSE 0
#include "srUtils.h"
#include "syslogd.h"
#include "obj.h"


/* ################################################################# *
 * private members                                                   *
 * ################################################################# */

/* As this is not a "real" object, there won't be any private
 * members in this file.
 */

/* ################################################################# *
 * public members                                                    *
 * ################################################################# */

rsRetVal srUtilItoA(char *pBuf, int iLenBuf, long iToConv)
{
	int i;
	int bIsNegative;
	char szBuf[64];	/* sufficiently large for my lifespan and those of my children... ;) */

	assert(pBuf != NULL);
	assert(iLenBuf > 1);	/* This is actually an app error and as thus checked for... */

	if(iToConv < 0)
	{
		bIsNegative = TRUE;
		iToConv *= -1;
	}
	else
		bIsNegative = FALSE;

	/* first generate a string with the digits in the reverse direction */
	i = 0;
	do
	{
		szBuf[i++] = iToConv % 10 + '0';
		iToConv /= 10;
	} while(iToConv > 0);	/* warning: do...while()! */
	--i; /* undo last increment - we were pointing at NEXT location */

	/* make sure we are within bounds... */
	if(i + 2 > iLenBuf)	/* +2 because: a) i starts at zero! b) the \0 byte */
		return RS_RET_PROVIDED_BUFFER_TOO_SMALL;

	/* then move it to the right direction... */
	if(bIsNegative == TRUE)
		*pBuf++ = '-';
	while(i >= 0)
		*pBuf++ = szBuf[i--];
	*pBuf = '\0';	/* terminate it!!! */

	return RS_RET_OK;
}

uchar *srUtilStrDup(uchar *pOld, size_t len)
{
	uchar *pNew;

	assert(pOld != NULL);
	
	if((pNew = malloc(len + 1)) != NULL)
		memcpy(pNew, pOld, len + 1);

	return pNew;
}


/* creates a path recursively
 * Return 0 on success, -1 otherwise. On failure, errno
 * hold the last OS error.
 * Param "mode" holds the mode that all non-existing directories
 * are to be created with.
 */
int makeFileParentDirs(uchar *szFile, size_t lenFile, mode_t mode,
		       uid_t uid, gid_t gid, int bFailOnChownFail)
{
        uchar *p;
        uchar *pszWork;
        size_t len;
	int bErr = 0;

	assert(szFile != NULL);
	assert(lenFile > 0);

        len = lenFile + 1; /* add one for '\0'-byte */
	if((pszWork = malloc(sizeof(uchar) * len)) == NULL)
		return -1;
        memcpy(pszWork, szFile, len);
        for(p = pszWork+1 ; *p ; p++)
                if(*p == '/') {
			/* temporarily terminate string, create dir and go on */
                        *p = '\0';
                        if(access((char*)pszWork, F_OK)) {
                                if(mkdir((char*)pszWork, mode) == 0) {
					if(uid != (uid_t) -1 || gid != (gid_t) -1) {
						/* we need to set owner/group */
						if(chown((char*)pszWork, uid, gid) != 0)
							if(bFailOnChownFail)
								bErr = 1;
							/* silently ignore if configured
							 * to do so.
							 */
					}
				} else
					bErr = 1;
				if(bErr) {
					int eSave = errno;
					free(pszWork);
					errno = eSave;
					return -1;
				}
			}
                        *p = '/';
                }
	free(pszWork);
	return 0;
}


/* execute a program with a single argument
 * returns child pid if everything ok, 0 on failure. if
 * it fails, errno is set. if it fails after the fork(), the caller
 * can not be notfied for obvious reasons. if bwait is set to 1,
 * the code waits until the child terminates - that potentially takes
 * a lot of time.
 * implemented 2007-07-20 rgerhards
 */
int execProg(uchar *program, int bWait, uchar *arg)
{
        int pid;
	int sig;
	struct sigaction sigAct;

	dbgprintf("exec program '%s' with param '%s'\n", program, arg);
        pid = fork();
        if (pid < 0) {
                return 0;
        }

        if(pid) {       /* Parent */
		if(bWait)
			if(waitpid(pid, NULL, 0) == -1)
				if(errno != ECHILD) {
					/* we do not use logerror(), because
					 * that might bring us into an endless
					 * loop. At some time, we may
					 * reconsider this behaviour.
					 */
					dbgprintf("could not wait on child after executing '%s'",
					        (char*)program);
				}
                return pid;
	}
        /* Child */
	alarm(0); /* create a clean environment before we exec the real child */

	memset(&sigAct, 0, sizeof(sigAct));
	sigemptyset(&sigAct.sa_mask);
	sigAct.sa_handler = SIG_DFL;

	for(sig = 1 ; sig < NSIG ; ++sig)
		sigaction(sig, &sigAct, NULL);

	execlp((char*)program, (char*) program, (char*)arg, NULL);
	/* In the long term, it's a good idea to implement some enhanced error
	 * checking here. However, it can not easily be done. For starters, we
	 * may run into endless loops if we log to syslog. The next problem is
	 * that output is typically not seen by the user. For the time being,
	 * we use no error reporting, which is quite consitent with the old
	 * system() way of doing things. rgerhards, 2007-07-20
	 */
	perror("exec");
	exit(1); /* not much we can do in this case */
}


/* skip over whitespace in a standard C string. The
 * provided pointer is advanced to the first non-whitespace
 * charater or the \0 byte, if there is none. It is never
 * moved past the \0.
 */
void skipWhiteSpace(uchar **pp)
{
	register uchar *p;

	assert(pp != NULL);
	assert(*pp != NULL);

	p = *pp;
	while(*p && isspace((int) *p))
		++p;
	*pp = p;
}


/* generate a file name from four parts:
 * <directory name>/<name>.<number>
 * If number is negative, it is not used. If any of the strings is
 * NULL, an empty string is used instead. Length must be provided.
 * lNumDigits is the minimum number of digits that lNum should have. This
 * is to pretty-print the file name, e.g. lNum = 3, lNumDigits= 4 will
 * result in "0003" being used inside the file name. Set lNumDigits to 0
 * to use as few space as possible.
 * rgerhards, 2008-01-03
 */
rsRetVal genFileName(uchar **ppName, uchar *pDirName, size_t lenDirName, uchar *pFName,
		     size_t lenFName, long lNum, int lNumDigits)
{
	DEFiRet;
	uchar *pName;
	uchar *pNameWork;
	size_t lenName;
	uchar szBuf[128];	/* buffer for number */
	char szFmtBuf[32];	/* buffer for snprintf format */
	size_t lenBuf;

	if(lNum < 0) {
		szBuf[0] = '\0';
		lenBuf = 0;
	} else {
		if(lNumDigits > 0) {
			snprintf(szFmtBuf, sizeof(szFmtBuf), ".%%0%dld", lNumDigits);
			lenBuf = snprintf((char*)szBuf, sizeof(szBuf), szFmtBuf, lNum);
		} else
			lenBuf = snprintf((char*)szBuf, sizeof(szBuf), ".%ld", lNum);
	}

	lenName = lenDirName + 1 + lenFName + lenBuf + 1; /* last +1 for \0 char! */
	if((pName = malloc(sizeof(uchar) * lenName)) == NULL)
		ABORT_FINALIZE(RS_RET_OUT_OF_MEMORY);
	
	/* got memory, now construct string */
	memcpy(pName, pDirName, lenDirName);
	pNameWork = pName + lenDirName;
	*pNameWork++ = '/';
	memcpy(pNameWork, pFName, lenFName);
	pNameWork += lenFName;
	if(lenBuf > 0) {
		memcpy(pNameWork, szBuf, lenBuf);
		pNameWork += lenBuf;
	}
	*pNameWork = '\0';

	*ppName = pName;

finalize_it:
	RETiRet;
}

/* get the number of digits required to represent a given number. We use an
 * iterative approach as we do not like to draw in the floating point
 * library just for log(). -- rgerhards, 2008-01-10
 */
int getNumberDigits(long lNum)
{
	int iDig;

	if(lNum == 0)
		iDig = 1;
	else
		for(iDig = 0 ; lNum != 0 ; ++iDig)
			lNum /= 10;

	return iDig;
}


/* compute an absolute time timeout suitable for calls to pthread_cond_timedwait()
 * rgerhards, 2008-01-14
 */
rsRetVal
timeoutComp(struct timespec *pt, long iTimeout)
{
	assert(pt != NULL);
	/* compute timeout */
	clock_gettime(CLOCK_REALTIME, pt);
	pt->tv_nsec += (iTimeout % 1000) * 1000000; /* think INTEGER arithmetic! */
	if(pt->tv_nsec > 999999999) { /* overrun? */
		pt->tv_nsec -= 1000000000;
	}
	pt->tv_sec += iTimeout / 1000;
	return RS_RET_OK; /* so far, this is static... */
}


/* This function is kind of the reverse of timeoutComp() - it takes an absolute
 * timeout value and computes how far this is in the future. If the value is already
 * in the past, 0 is returned. The return value is in ms.
 * rgerhards, 2008-01-25
 */
long
timeoutVal(struct timespec *pt)
{
	struct timespec t;
	long iTimeout;

	assert(pt != NULL);
	/* compute timeout */
	clock_gettime(CLOCK_REALTIME, &t);
RUNLOG_VAR("%ld", pt->tv_sec);
RUNLOG_VAR("%ld", t.tv_sec);
RUNLOG_VAR("%ld", pt->tv_nsec);
RUNLOG_VAR("%ld", t.tv_nsec);
	iTimeout = (pt->tv_nsec - t.tv_nsec) / 1000000;
RUNLOG_VAR("%ld", iTimeout);
	iTimeout += (pt->tv_sec - t.tv_sec) * 1000;
RUNLOG_VAR("%ld", iTimeout);

	if(iTimeout < 0)
		iTimeout = 0;

	return iTimeout;
}


/* cancellation cleanup handler - frees provided mutex
 * rgerhards, 2008-01-14
 */
void
mutexCancelCleanup(void *arg)
{
	BEGINfunc
	assert(arg != NULL);
	d_pthread_mutex_unlock((pthread_mutex_t*) arg);
	ENDfunc
}


/*
 * vi:set ai:
 */
