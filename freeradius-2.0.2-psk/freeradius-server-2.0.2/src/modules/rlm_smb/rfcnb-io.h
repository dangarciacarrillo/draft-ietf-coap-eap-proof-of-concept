/* UNIX RFCNB (RFC1001/RFC1002) NetBIOS implementation

   Version 1.0
   RFCNB IO Routines Defines

   Copyright (C) Richard Sharpe 1996
   Copyright 2006 The FreeRADIUS server project

*/

/*
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <freeradius-devel/ident.h>
RCSIDH(rfcnb_io_h, "$Id: rfcnb-io.h,v 1.3 2006/11/14 21:22:26 fcusack Exp $")

int RFCNB_Put_Pkt(struct RFCNB_Con *con, struct RFCNB_Pkt *pkt, int len);

int RFCNB_Get_Pkt(struct RFCNB_Con *con, struct RFCNB_Pkt *pkt, int len);
