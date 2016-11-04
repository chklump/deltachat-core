/*******************************************************************************
 *
 *                             Messenger Backend
 *     Copyright (C) 2016 Björn Petersen Software Design and Development
 *                   Contact: r10s@b44t.com, http://b44t.com
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see http://www.gnu.org/licenses/ .
 *
 *******************************************************************************
 *
 * File:    mrimap.h
 * Authors: Björn Petersen
 * Purpose: Reading from IMAP servers with no dependencies to mrmailbox_t or to
 *          the database.
 *
 ******************************************************************************/


#ifndef __MRIMAP_H__
#define __MRIMAP_H__
#ifdef __cplusplus
extern "C" {
#endif


/*** library-private **********************************************************/

typedef struct mrloginparam_t mrloginparam_t;


/* send events to the owner of the object, we use the standard MR_EVENT_ here */
#define MR_EVENT_RECEIVE_IMF_    -100 /* data1: ptr, data2: bytes, ret: number of messages created */
#define MR_EVENT_GET_CONFIG_INT_ -200 /* data1: key, data2: default, ret: value  */
#define MR_EVENT_SET_CONFIG_INT_ -210 /* data1: key, data2: value */
typedef uintptr_t (*mrimapcb_t) (mrimap_t*, int event, uintptr_t data1, uintptr_t data2, uintptr_t data3, uintptr_t data4);


typedef struct mrimap_t
{
	mailimap*             m_hEtpan;

	pthread_mutex_t       m_critical;

	char*                 m_imap_server;
	int                   m_imap_port;
	char*                 m_imap_user;
	char*                 m_imap_pw;

	mrimapcb_t            m_cb;
	void*                 m_userData;
} mrimap_t;


mrimap_t* mrimap_new               (mrimapcb_t, void* userData);
void      mrimap_unref             (mrimap_t*);

int       mrimap_is_connected      (mrimap_t*);
int       mrimap_connect           (mrimap_t*, const mrloginparam_t*);
void      mrimap_disconnect        (mrimap_t*);
int       mrimap_fetch             (mrimap_t*);


#ifdef __cplusplus
} /* /extern "C" */
#endif
#endif /* __MRIMAP_H__ */

