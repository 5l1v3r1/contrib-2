/*
    EIBD eib bus access and management daemon
    Copyright (C) 2005-2011 Martin Koegler <mkoegler@auto.tuwien.ac.at>

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
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <netinet/tcp.h>
#include "tpuarttcp.h"
#include "layer3.h"

TPUARTTCPLayer2Driver::TPUARTTCPLayer2Driver (const char *dest, int port,
						    L2options *opt)
	: Layer2 (opt)
{
  int reuse = 1;
  int nodelay = 1;
  struct sockaddr_in addr;

  TRACEPRINTF (t, 2, this, "Open");

  pth_sem_init (&in_signal);

  ackallgroup = opt ? (opt->flags & FLAG_B_TPUARTS_ACKGROUP) : 0;
  ackallindividual = opt ? (opt->flags & FLAG_B_TPUARTS_ACKINDIVIDUAL) : 0;
  dischreset = opt ? (opt->flags & FLAG_B_TPUARTS_DISCH_RESET) : 0;

  if (opt)
	opt->flags &=~ (FLAG_B_TPUARTS_ACKGROUP |
	                FLAG_B_TPUARTS_ACKINDIVIDUAL |
					FLAG_B_TPUARTS_DISCH_RESET);

  if (!GetHostIP (t, &addr, dest))
    return;
  addr.sin_port = htons (port);

  fd = socket (AF_INET, SOCK_STREAM, 0);
  if (fd == -1)
    {
      ERRORPRINTF (t, E_ERROR | 52, this, "Opening %s:%d failed: %s", dest,port, strerror(errno));
      return;
    }
  setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof (reuse));

  if (connect (fd, (struct sockaddr *) &addr, sizeof (addr)) == -1)
    {
      ERRORPRINTF (t, E_ERROR | 53, this, "Connect %s:%d: connect: %s", dest,port, strerror(errno));
      close (fd);
      fd = -1;
      return;
    }
  setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof (nodelay));

  Start ();
  TRACEPRINTF (t, 2, this, "Openend");
}

TPUARTTCPLayer2Driver::~TPUARTTCPLayer2Driver ()
{
  TRACEPRINTF (t, 2, this, "Close");
  Stop ();

  while (!inqueue.isempty ())
    delete inqueue.get ();

  if (fd != -1)
    close (fd);

}

bool TPUARTTCPLayer2Driver::init (Layer3 *l3)
{
  if (fd == -1)
    return false;
  if (! addGroupAddress(0))
    return false;
  return Layer2::init (l3);
}

bool
TPUARTTCPLayer2Driver::Send_Queue_Empty ()
{
  return inqueue.isempty ();
}

void
TPUARTTCPLayer2Driver::Send_L_Data (LPDU * l)
{
  TRACEPRINTF (t, 2, this, "Send %s", l->Decode ()());
  inqueue.put (l);
  pth_sem_inc (&in_signal, 1);
}

//Open

bool
TPUARTTCPLayer2Driver::enterBusmonitor ()
{
  if (!Layer2::enterBusmonitor ())
    return false;
  uchar c = 0x05;
  t->TracePacket (2, this, "openBusmonitor", 1, &c);
  if (write (fd, &c, 1) != 1)
    return 0;
  return 1;
}

bool
TPUARTTCPLayer2Driver::leaveBusmonitor ()
{
  if (!Layer2::leaveBusmonitor ())
    return false;
  uchar c = 0x01;
  t->TracePacket (2, this, "leaveBusmonitor", 1, &c);
  if (write (fd, &c, 1) != 1)
    return 0;
  return 1;
}

bool
TPUARTTCPLayer2Driver::Open ()
{
  if (!Layer2::Open ())
    return false;
  uchar c = 0x01;
  t->TracePacket (2, this, "open-reset", 1, &c);
  if (write (fd, &c, 1) != 1)
    return 0;
  return 1;
}

void
TPUARTTCPLayer2Driver::RecvLPDU (const uchar * data, int len)
{
  t->TracePacket (1, this, "RecvLP", len, data);
  if (mode & BUSMODE_MONITOR)
    {
      L_Busmonitor_PDU *l = new L_Busmonitor_PDU (shared_from_this());
      l->pdu.set (data, len);
      l3->recv_L_Data (l);
    }
  if (mode == BUSMODE_UP)
    {
      LPDU *l = LPDU::fromPacket (CArray (data, len), shared_from_this());
      if (l->getType () == L_Data && ((L_Data_PDU *) l)->valid_checksum)
	l3->recv_L_Data (l);
      else
	delete l;
    }
}

void
TPUARTTCPLayer2Driver::Run (pth_sem_t * stop1)
{
  uchar buf[255];
  int i;
  CArray in;
  int to = 0;
  int waitconfirm = 0;
  CArray sendheader;
  int acked = 0;
  int retry = 0;
  int watch = 0;
  pth_event_t stop = pth_event (PTH_EVENT_SEM, stop1);
  pth_event_t input = pth_event (PTH_EVENT_SEM, &in_signal);
  pth_event_t timeout = pth_event (PTH_EVENT_RTIME, pth_time (0, 0));
  pth_event_t watchdog = pth_event (PTH_EVENT_RTIME, pth_time (0, 0));
  pth_event_t sendtimeout = pth_event (PTH_EVENT_RTIME, pth_time (0, 0));
  while (pth_event_status (stop) != PTH_STATUS_OCCURRED)
    {
      if (in () == 0 && !waitconfirm)
	pth_event_concat (stop, input, NULL);
      if (to)
	pth_event_concat (stop, timeout, NULL);
      if (waitconfirm)
	pth_event_concat (stop, sendtimeout, NULL);
      if (watch)
	pth_event_concat (stop, watchdog, NULL);
      i = pth_read_ev (fd, buf, sizeof (buf), stop);
      pth_event_isolate (stop);
      pth_event_isolate (timeout);
      pth_event_isolate (sendtimeout);
      pth_event_isolate (watchdog);
      if (i > 0)
	{
	  t->TracePacket (0, this, "RecvB", i, buf);
	  in.setpart (buf, in (), i);
	}
      while (in () > 0)
	{
	  if (in[0] == 0x8B) // L_DataConfirm positive
	    {
	      if (waitconfirm)
		{
		  waitconfirm = 0;
		  delete inqueue.get ();
		  pth_sem_dec (&in_signal);
		  retry = 0;
		}
	      in.deletepart (0, 1);
	    }
	  else if (in[0] == 0x0B) // L_DataConfirm negative
	    {
	      if (waitconfirm)
		{
		  retry++;
		  waitconfirm = 0;
		  if (retry > 3)
		    {
		      TRACEPRINTF (t, 0, this, "NACK: dropping packet");
		      delete inqueue.get ();
		      pth_sem_dec (&in_signal);
		      retry = 0;
		    }
                  else
		    TRACEPRINTF (t, 0, this, "NACK");
		}
	      in.deletepart (0, 1);
	    }
	  else if ((in[0] & 0x07) == 0x07) // Reset-Indication
	    {
	      TRACEPRINTF (t, 0, this, "RecvWatchdog: %02X", in[0]);
	      watch = 2;
	      pth_event (PTH_EVENT_RTIME | PTH_MODE_REUSE, watchdog,
			 pth_time (10, 0));
	      in.deletepart (0, 1);
	    }
          /*
           * 0xCC acknowledge frame
           * 0x0C NotAcknowledge frame
           * 0xC0 Busy Frame
           */
	  else if (in[0] == 0xCC || in[0] == 0xC0 || in[0] == 0x0C)
	    {
	      RecvLPDU (in.array (), 1);
	      in.deletepart (0, 1);
	    }
	  else if ((in[0] & 0xD0) == 0x90) // Matches KNX control byte L_Data_Standard Frame
	    {
              bool recvecho = false;

/** The "telegram complete" part of the test does not work because the ack arrives at the TPUART
 *  too late to be of any use. Thus we send it early.
 *
 *  This is broken, but there's nothing we can do about it.
 */
	      // if (in () < 6 || in() < (8 + (in[5] & 0x0F)))  // Telegram complete
              if (in () < 6)
		{
		  if (!to)
		    {
		      to = 1;
		      pth_event (PTH_EVENT_RTIME | PTH_MODE_REUSE, timeout,
				 pth_time (0, 300000));
		    }
		  if (pth_event_status (timeout) != PTH_STATUS_OCCURRED)
		    break;
		  TRACEPRINTF (t, 0, this, "Remove1 %02X", in[0]);
		  in.deletepart (0, 1);
		  continue;
		}
              if (waitconfirm)
                {
                  CArray recvheader;
                  recvheader.set(in.array(),6);
                  recvheader[0] &=~ 0x20;
                  if (recvheader == sendheader)
                    {
                      TRACEPRINTF (t, 0, this, "Ignoring this telegram. We sent it.");
                      recvecho = true;
                    }
                }
	      if (!acked && !recvecho)
		{
		  uchar c = 0x10;
		  if ((in[5] & 0x80) == 0)
		    {
		      if (ackallindividual || l3->hasAddress ((in[3] << 8) | in[4], shared_from_this()))
			c |= 0x1;
		    }
		  else
		    {
		      if (ackallgroup || l3->hasGroupAddress ((in[3] << 8) | in[4], shared_from_this()))
		        c |= 0x1;
		    }
		  TRACEPRINTF (t, 0, this, "SendAck %02X", c);
		  pth_write_ev (fd, &c, 1, stop);
		  acked = 1;
		}
	      unsigned len = in[5] & 0x0f;
	      len += 6 + 2;
	      if (in () < len)
		{
		  if (!to)
		    {
		      to = 1;
		      pth_event (PTH_EVENT_RTIME | PTH_MODE_REUSE, timeout,
				 pth_time (0, 300000));
		    }
		  if (pth_event_status (timeout) != PTH_STATUS_OCCURRED)
		    break;
		  TRACEPRINTF (t, 0, this, "Remove2 %02X", in[0]);
		  in.deletepart (0, 1);
		  continue;
		}
              if (!recvecho)
                {
	          acked = 0;
	          RecvLPDU (in.array (), len);
                }
	      in.deletepart (0, len);
	    }
	  else if ((in[0] & 0xD0) == 0x10) //Matches KNX control byte L_Data_Extended Frame
	    {
              bool recvecho = false;
	      if (in () < 7)
		{
		  if (!to)
		    {
		      to = 1;
		      pth_event (PTH_EVENT_RTIME | PTH_MODE_REUSE, timeout,
				 pth_time (0, 300000));
		    }
		  if (pth_event_status (timeout) != PTH_STATUS_OCCURRED)
		    break;
		  TRACEPRINTF (t, 0, this, "Remove1 %02X", in[0]);
		  in.deletepart (0, 1);
		  continue;
		}
              if (waitconfirm)
                {
                  CArray recvheader;
                  recvheader.set(in.array(),6);
                  recvheader[0] &=~ 0x20;
                  if (recvheader == sendheader)
                    {
                      TRACEPRINTF (t, 0, this, "ignoring this telegram. we sent it.");
                      recvecho = true;
                    }
                }
	      if (!acked  && !recvecho)
		{
		  uchar c = 0x10;
		  if ((in[1] & 0x80) == 0)
		    {
		      if (ackallindividual || l3->hasAddress ((in[4] << 8) | in[5], shared_from_this()))
			c |= 0x1;
		    }
		  else
		    {
		      if (ackallgroup || l3->hasGroupAddress ((in[4] << 8) | in[5], shared_from_this()))
			c |= 0x1;
		    }
		  TRACEPRINTF (t, 0, this, "SendAck %02X", c);
		  pth_write_ev (fd, &c, 1, stop);
		  acked = 1;
		}
	      unsigned len = in[6] & 0xff;
	      len += 7 + 2;
	      if (in () < len)
		{
		  if (!to)
		    {
		      to = 1;
		      pth_event (PTH_EVENT_RTIME | PTH_MODE_REUSE, timeout,
				 pth_time (0, 300000));
		    }
		  if (pth_event_status (timeout) != PTH_STATUS_OCCURRED)
		    break;
		  TRACEPRINTF (t, 0, this, "Remove2 %02X", in[0]);
		  in.deletepart (0, 1);
		  continue;
		}
              if (!recvecho)
                {
	          acked = 0;
	          RecvLPDU (in.array (), len);
                }
	      in.deletepart (0, len);
	    }
	  else
	    {
	      acked = 0;
	      TRACEPRINTF (t, 0, this, "Remove %02X", in[0]);
	      in.deletepart (0, 1);
	    }
	  to = 0;
	}
      if (waitconfirm
	  && pth_event_status (sendtimeout) == PTH_STATUS_OCCURRED)
	{
	  retry++;
	  waitconfirm = 0;
	  if (retry >= 3)
	    {
	      TRACEPRINTF (t, 0, this, "Drop Send");
	      delete inqueue.get ();
	      pth_sem_dec (&in_signal);
	    }
	}
      if (watch == 1 && pth_event_status (watchdog) == PTH_STATUS_OCCURRED
	  && mode != BUSMODE_MONITOR)
	{
	  if (dischreset)
	    {
	      pth_usleep (2000);
	      pth_usleep (1000);
	    }

	  uchar c = 0x01;
	  t->TracePacket (2, this, "Watchdog Reset", 1, &c);
	  if (write (fd, &c, 1) != 1)
	    break;
	  watch = 0;
	}
      if (watch == 1 && pth_event_status (watchdog) == PTH_STATUS_OCCURRED
	  && mode == BUSMODE_MONITOR)
	watch = 0;
      if (watch == 2 && pth_event_status (watchdog) == PTH_STATUS_OCCURRED)
	watch = 0;
      if (in () == 0 && !inqueue.isempty () && !waitconfirm)
	{
	  LPDU *l = (LPDU *) inqueue.top ();
	  CArray d = l->ToPacket ();
	  CArray w;
	  unsigned i;

	  sendheader.set(d.array(), 6);
          sendheader[0] &=~ 0x20;
	  w.resize (d () * 2);
	  for (i = 0; i < d (); i++)
	    {
	      w[2 * i] = 0x80 | (i & 0x3f);
	      w[2 * i + 1] = d[i];
	    }
	  w[(d () * 2) - 2] = (w[(d () * 2) - 2] & 0x3f) | 0x40;
	  t->TracePacket (0, this, "Write", w);
	  (void) pth_write_ev (fd, w.array (), w (), stop);
	  waitconfirm = 1;
	  pth_event (PTH_EVENT_RTIME | PTH_MODE_REUSE, sendtimeout,
		     pth_time (0, 600000));
	}
      else if (in () == 0 && !waitconfirm && !watch && mode != BUSMODE_MONITOR && !to)
	{
	  pth_event (PTH_EVENT_RTIME | PTH_MODE_REUSE, watchdog,
		     pth_time (10, 0));
	  watch = 1;
	  uchar c = 0x02;
	  t->TracePacket (2, this, "Watchdog Status", 1, &c);
	  if (write (fd, &c, 1) != 1)
	    break;
	}
    }
  if (pth_event_status (stop) != PTH_STATUS_OCCURRED)
    ERRORPRINTF (t, E_FATAL | 27, this, "exited due to error: %s", strerror(errno));
  pth_event_free (stop, PTH_FREE_THIS);
  pth_event_free (input, PTH_FREE_THIS);
  pth_event_free (timeout, PTH_FREE_THIS);
  pth_event_free (watchdog, PTH_FREE_THIS);
  pth_event_free (sendtimeout, PTH_FREE_THIS);
}
