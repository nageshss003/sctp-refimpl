/*-
 * Copyright (c) 2001-2008, by Cisco Systems, Inc. All rights reserved.
 * Copyright (c) 2008-2011, by Randall Stewart. All rights reserved.
 * Copyright (c) 2008-2011, by Michael Tuexen. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * a) Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * b) Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the distribution.
 *
 * c) Neither the name of Cisco Systems, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/* $KAME: sctp_output.c,v 1.46 2005/03/06 16:04:17 itojun Exp $	 */

#ifdef __FreeBSD__
#include <sys/cdefs.h>
__FBSDID("$FreeBSD: head/sys/netinet/sctp_output.c 224641 2011-08-03 20:21:00Z tuexen $");
#endif

#include <netinet/sctp_os.h>
#ifdef __FreeBSD__
#include <sys/proc.h>
#endif
#include <netinet/sctp_var.h>
#include <netinet/sctp_sysctl.h>
#include <netinet/sctp_header.h>
#include <netinet/sctp_pcb.h>
#include <netinet/sctputil.h>
#include <netinet/sctp_output.h>
#include <netinet/sctp_uio.h>
#include <netinet/sctputil.h>
#include <netinet/sctp_auth.h>
#include <netinet/sctp_timer.h>
#include <netinet/sctp_asconf.h>
#include <netinet/sctp_indata.h>
#include <netinet/sctp_bsd_addr.h>
#include <netinet/sctp_input.h>
#include <netinet/sctp_crc32.h>
#if defined(__Userspace_os_Linux)
#define __FAVOR_BSD    /* (on Ubuntu at least) enables UDP header field names like BSD in RFC 768 */
#endif
#include <netinet/udp.h>
#if defined(__APPLE__)
#include <netinet/in.h>
#endif
#if defined(__FreeBSD__)
#include <machine/in_cksum.h>
#endif

#if defined(__APPLE__)
#define APPLE_FILE_NO 3
#endif

#if defined(APPLE_LION)
#define SCTP_MAX_LINKHDR 16
#endif

#define SCTP_MAX_GAPS_INARRAY 4
struct sack_track {
	uint8_t right_edge;	/* mergable on the right edge */
	uint8_t left_edge;	/* mergable on the left edge */
	uint8_t num_entries;
	uint8_t spare;
	struct sctp_gap_ack_block gaps[SCTP_MAX_GAPS_INARRAY];
};

struct sack_track sack_array[256] = {
	{0, 0, 0, 0,		/* 0x00 */
		{{0, 0},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 1, 0,		/* 0x01 */
		{{0, 0},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 1, 0,		/* 0x02 */
		{{1, 1},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 1, 0,		/* 0x03 */
		{{0, 1},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 1, 0,		/* 0x04 */
		{{2, 2},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x05 */
		{{0, 0},
		{2, 2},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 1, 0,		/* 0x06 */
		{{1, 2},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 1, 0,		/* 0x07 */
		{{0, 2},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 1, 0,		/* 0x08 */
		{{3, 3},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x09 */
		{{0, 0},
		{3, 3},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x0a */
		{{1, 1},
		{3, 3},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x0b */
		{{0, 1},
		{3, 3},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 1, 0,		/* 0x0c */
		{{2, 3},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x0d */
		{{0, 0},
		{2, 3},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 1, 0,		/* 0x0e */
		{{1, 3},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 1, 0,		/* 0x0f */
		{{0, 3},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 1, 0,		/* 0x10 */
		{{4, 4},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x11 */
		{{0, 0},
		{4, 4},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x12 */
		{{1, 1},
		{4, 4},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x13 */
		{{0, 1},
		{4, 4},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x14 */
		{{2, 2},
		{4, 4},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 3, 0,		/* 0x15 */
		{{0, 0},
		{2, 2},
		{4, 4},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x16 */
		{{1, 2},
		{4, 4},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x17 */
		{{0, 2},
		{4, 4},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 1, 0,		/* 0x18 */
		{{3, 4},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x19 */
		{{0, 0},
		{3, 4},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x1a */
		{{1, 1},
		{3, 4},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x1b */
		{{0, 1},
		{3, 4},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 1, 0,		/* 0x1c */
		{{2, 4},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x1d */
		{{0, 0},
		{2, 4},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 1, 0,		/* 0x1e */
		{{1, 4},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 1, 0,		/* 0x1f */
		{{0, 4},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 1, 0,		/* 0x20 */
		{{5, 5},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x21 */
		{{0, 0},
		{5, 5},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x22 */
		{{1, 1},
		{5, 5},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x23 */
		{{0, 1},
		{5, 5},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x24 */
		{{2, 2},
		{5, 5},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 3, 0,		/* 0x25 */
		{{0, 0},
		{2, 2},
		{5, 5},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x26 */
		{{1, 2},
		{5, 5},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x27 */
		{{0, 2},
		{5, 5},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x28 */
		{{3, 3},
		{5, 5},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 3, 0,		/* 0x29 */
		{{0, 0},
		{3, 3},
		{5, 5},
		{0, 0}
		}
	},
	{0, 0, 3, 0,		/* 0x2a */
		{{1, 1},
		{3, 3},
		{5, 5},
		{0, 0}
		}
	},
	{1, 0, 3, 0,		/* 0x2b */
		{{0, 1},
		{3, 3},
		{5, 5},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x2c */
		{{2, 3},
		{5, 5},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 3, 0,		/* 0x2d */
		{{0, 0},
		{2, 3},
		{5, 5},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x2e */
		{{1, 3},
		{5, 5},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x2f */
		{{0, 3},
		{5, 5},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 1, 0,		/* 0x30 */
		{{4, 5},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x31 */
		{{0, 0},
		{4, 5},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x32 */
		{{1, 1},
		{4, 5},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x33 */
		{{0, 1},
		{4, 5},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x34 */
		{{2, 2},
		{4, 5},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 3, 0,		/* 0x35 */
		{{0, 0},
		{2, 2},
		{4, 5},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x36 */
		{{1, 2},
		{4, 5},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x37 */
		{{0, 2},
		{4, 5},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 1, 0,		/* 0x38 */
		{{3, 5},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x39 */
		{{0, 0},
		{3, 5},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x3a */
		{{1, 1},
		{3, 5},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x3b */
		{{0, 1},
		{3, 5},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 1, 0,		/* 0x3c */
		{{2, 5},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x3d */
		{{0, 0},
		{2, 5},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 1, 0,		/* 0x3e */
		{{1, 5},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 1, 0,		/* 0x3f */
		{{0, 5},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 1, 0,		/* 0x40 */
		{{6, 6},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x41 */
		{{0, 0},
		{6, 6},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x42 */
		{{1, 1},
		{6, 6},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x43 */
		{{0, 1},
		{6, 6},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x44 */
		{{2, 2},
		{6, 6},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 3, 0,		/* 0x45 */
		{{0, 0},
		{2, 2},
		{6, 6},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x46 */
		{{1, 2},
		{6, 6},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x47 */
		{{0, 2},
		{6, 6},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x48 */
		{{3, 3},
		{6, 6},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 3, 0,		/* 0x49 */
		{{0, 0},
		{3, 3},
		{6, 6},
		{0, 0}
		}
	},
	{0, 0, 3, 0,		/* 0x4a */
		{{1, 1},
		{3, 3},
		{6, 6},
		{0, 0}
		}
	},
	{1, 0, 3, 0,		/* 0x4b */
		{{0, 1},
		{3, 3},
		{6, 6},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x4c */
		{{2, 3},
		{6, 6},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 3, 0,		/* 0x4d */
		{{0, 0},
		{2, 3},
		{6, 6},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x4e */
		{{1, 3},
		{6, 6},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x4f */
		{{0, 3},
		{6, 6},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x50 */
		{{4, 4},
		{6, 6},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 3, 0,		/* 0x51 */
		{{0, 0},
		{4, 4},
		{6, 6},
		{0, 0}
		}
	},
	{0, 0, 3, 0,		/* 0x52 */
		{{1, 1},
		{4, 4},
		{6, 6},
		{0, 0}
		}
	},
	{1, 0, 3, 0,		/* 0x53 */
		{{0, 1},
		{4, 4},
		{6, 6},
		{0, 0}
		}
	},
	{0, 0, 3, 0,		/* 0x54 */
		{{2, 2},
		{4, 4},
		{6, 6},
		{0, 0}
		}
	},
	{1, 0, 4, 0,		/* 0x55 */
		{{0, 0},
		{2, 2},
		{4, 4},
		{6, 6}
		}
	},
	{0, 0, 3, 0,		/* 0x56 */
		{{1, 2},
		{4, 4},
		{6, 6},
		{0, 0}
		}
	},
	{1, 0, 3, 0,		/* 0x57 */
		{{0, 2},
		{4, 4},
		{6, 6},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x58 */
		{{3, 4},
		{6, 6},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 3, 0,		/* 0x59 */
		{{0, 0},
		{3, 4},
		{6, 6},
		{0, 0}
		}
	},
	{0, 0, 3, 0,		/* 0x5a */
		{{1, 1},
		{3, 4},
		{6, 6},
		{0, 0}
		}
	},
	{1, 0, 3, 0,		/* 0x5b */
		{{0, 1},
		{3, 4},
		{6, 6},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x5c */
		{{2, 4},
		{6, 6},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 3, 0,		/* 0x5d */
		{{0, 0},
		{2, 4},
		{6, 6},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x5e */
		{{1, 4},
		{6, 6},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x5f */
		{{0, 4},
		{6, 6},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 1, 0,		/* 0x60 */
		{{5, 6},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x61 */
		{{0, 0},
		{5, 6},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x62 */
		{{1, 1},
		{5, 6},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x63 */
		{{0, 1},
		{5, 6},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x64 */
		{{2, 2},
		{5, 6},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 3, 0,		/* 0x65 */
		{{0, 0},
		{2, 2},
		{5, 6},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x66 */
		{{1, 2},
		{5, 6},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x67 */
		{{0, 2},
		{5, 6},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x68 */
		{{3, 3},
		{5, 6},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 3, 0,		/* 0x69 */
		{{0, 0},
		{3, 3},
		{5, 6},
		{0, 0}
		}
	},
	{0, 0, 3, 0,		/* 0x6a */
		{{1, 1},
		{3, 3},
		{5, 6},
		{0, 0}
		}
	},
	{1, 0, 3, 0,		/* 0x6b */
		{{0, 1},
		{3, 3},
		{5, 6},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x6c */
		{{2, 3},
		{5, 6},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 3, 0,		/* 0x6d */
		{{0, 0},
		{2, 3},
		{5, 6},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x6e */
		{{1, 3},
		{5, 6},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x6f */
		{{0, 3},
		{5, 6},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 1, 0,		/* 0x70 */
		{{4, 6},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x71 */
		{{0, 0},
		{4, 6},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x72 */
		{{1, 1},
		{4, 6},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x73 */
		{{0, 1},
		{4, 6},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x74 */
		{{2, 2},
		{4, 6},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 3, 0,		/* 0x75 */
		{{0, 0},
		{2, 2},
		{4, 6},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x76 */
		{{1, 2},
		{4, 6},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x77 */
		{{0, 2},
		{4, 6},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 1, 0,		/* 0x78 */
		{{3, 6},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x79 */
		{{0, 0},
		{3, 6},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 2, 0,		/* 0x7a */
		{{1, 1},
		{3, 6},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x7b */
		{{0, 1},
		{3, 6},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 1, 0,		/* 0x7c */
		{{2, 6},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 2, 0,		/* 0x7d */
		{{0, 0},
		{2, 6},
		{0, 0},
		{0, 0}
		}
	},
	{0, 0, 1, 0,		/* 0x7e */
		{{1, 6},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 0, 1, 0,		/* 0x7f */
		{{0, 6},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{0, 1, 1, 0,		/* 0x80 */
		{{7, 7},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 2, 0,		/* 0x81 */
		{{0, 0},
		{7, 7},
		{0, 0},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0x82 */
		{{1, 1},
		{7, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 2, 0,		/* 0x83 */
		{{0, 1},
		{7, 7},
		{0, 0},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0x84 */
		{{2, 2},
		{7, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0x85 */
		{{0, 0},
		{2, 2},
		{7, 7},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0x86 */
		{{1, 2},
		{7, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 2, 0,		/* 0x87 */
		{{0, 2},
		{7, 7},
		{0, 0},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0x88 */
		{{3, 3},
		{7, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0x89 */
		{{0, 0},
		{3, 3},
		{7, 7},
		{0, 0}
		}
	},
	{0, 1, 3, 0,		/* 0x8a */
		{{1, 1},
		{3, 3},
		{7, 7},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0x8b */
		{{0, 1},
		{3, 3},
		{7, 7},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0x8c */
		{{2, 3},
		{7, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0x8d */
		{{0, 0},
		{2, 3},
		{7, 7},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0x8e */
		{{1, 3},
		{7, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 2, 0,		/* 0x8f */
		{{0, 3},
		{7, 7},
		{0, 0},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0x90 */
		{{4, 4},
		{7, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0x91 */
		{{0, 0},
		{4, 4},
		{7, 7},
		{0, 0}
		}
	},
	{0, 1, 3, 0,		/* 0x92 */
		{{1, 1},
		{4, 4},
		{7, 7},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0x93 */
		{{0, 1},
		{4, 4},
		{7, 7},
		{0, 0}
		}
	},
	{0, 1, 3, 0,		/* 0x94 */
		{{2, 2},
		{4, 4},
		{7, 7},
		{0, 0}
		}
	},
	{1, 1, 4, 0,		/* 0x95 */
		{{0, 0},
		{2, 2},
		{4, 4},
		{7, 7}
		}
	},
	{0, 1, 3, 0,		/* 0x96 */
		{{1, 2},
		{4, 4},
		{7, 7},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0x97 */
		{{0, 2},
		{4, 4},
		{7, 7},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0x98 */
		{{3, 4},
		{7, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0x99 */
		{{0, 0},
		{3, 4},
		{7, 7},
		{0, 0}
		}
	},
	{0, 1, 3, 0,		/* 0x9a */
		{{1, 1},
		{3, 4},
		{7, 7},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0x9b */
		{{0, 1},
		{3, 4},
		{7, 7},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0x9c */
		{{2, 4},
		{7, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0x9d */
		{{0, 0},
		{2, 4},
		{7, 7},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0x9e */
		{{1, 4},
		{7, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 2, 0,		/* 0x9f */
		{{0, 4},
		{7, 7},
		{0, 0},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xa0 */
		{{5, 5},
		{7, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xa1 */
		{{0, 0},
		{5, 5},
		{7, 7},
		{0, 0}
		}
	},
	{0, 1, 3, 0,		/* 0xa2 */
		{{1, 1},
		{5, 5},
		{7, 7},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xa3 */
		{{0, 1},
		{5, 5},
		{7, 7},
		{0, 0}
		}
	},
	{0, 1, 3, 0,		/* 0xa4 */
		{{2, 2},
		{5, 5},
		{7, 7},
		{0, 0}
		}
	},
	{1, 1, 4, 0,		/* 0xa5 */
		{{0, 0},
		{2, 2},
		{5, 5},
		{7, 7}
		}
	},
	{0, 1, 3, 0,		/* 0xa6 */
		{{1, 2},
		{5, 5},
		{7, 7},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xa7 */
		{{0, 2},
		{5, 5},
		{7, 7},
		{0, 0}
		}
	},
	{0, 1, 3, 0,		/* 0xa8 */
		{{3, 3},
		{5, 5},
		{7, 7},
		{0, 0}
		}
	},
	{1, 1, 4, 0,		/* 0xa9 */
		{{0, 0},
		{3, 3},
		{5, 5},
		{7, 7}
		}
	},
	{0, 1, 4, 0,		/* 0xaa */
		{{1, 1},
		{3, 3},
		{5, 5},
		{7, 7}
		}
	},
	{1, 1, 4, 0,		/* 0xab */
		{{0, 1},
		{3, 3},
		{5, 5},
		{7, 7}
		}
	},
	{0, 1, 3, 0,		/* 0xac */
		{{2, 3},
		{5, 5},
		{7, 7},
		{0, 0}
		}
	},
	{1, 1, 4, 0,		/* 0xad */
		{{0, 0},
		{2, 3},
		{5, 5},
		{7, 7}
		}
	},
	{0, 1, 3, 0,		/* 0xae */
		{{1, 3},
		{5, 5},
		{7, 7},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xaf */
		{{0, 3},
		{5, 5},
		{7, 7},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xb0 */
		{{4, 5},
		{7, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xb1 */
		{{0, 0},
		{4, 5},
		{7, 7},
		{0, 0}
		}
	},
	{0, 1, 3, 0,		/* 0xb2 */
		{{1, 1},
		{4, 5},
		{7, 7},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xb3 */
		{{0, 1},
		{4, 5},
		{7, 7},
		{0, 0}
		}
	},
	{0, 1, 3, 0,		/* 0xb4 */
		{{2, 2},
		{4, 5},
		{7, 7},
		{0, 0}
		}
	},
	{1, 1, 4, 0,		/* 0xb5 */
		{{0, 0},
		{2, 2},
		{4, 5},
		{7, 7}
		}
	},
	{0, 1, 3, 0,		/* 0xb6 */
		{{1, 2},
		{4, 5},
		{7, 7},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xb7 */
		{{0, 2},
		{4, 5},
		{7, 7},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xb8 */
		{{3, 5},
		{7, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xb9 */
		{{0, 0},
		{3, 5},
		{7, 7},
		{0, 0}
		}
	},
	{0, 1, 3, 0,		/* 0xba */
		{{1, 1},
		{3, 5},
		{7, 7},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xbb */
		{{0, 1},
		{3, 5},
		{7, 7},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xbc */
		{{2, 5},
		{7, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xbd */
		{{0, 0},
		{2, 5},
		{7, 7},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xbe */
		{{1, 5},
		{7, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 2, 0,		/* 0xbf */
		{{0, 5},
		{7, 7},
		{0, 0},
		{0, 0}
		}
	},
	{0, 1, 1, 0,		/* 0xc0 */
		{{6, 7},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 2, 0,		/* 0xc1 */
		{{0, 0},
		{6, 7},
		{0, 0},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xc2 */
		{{1, 1},
		{6, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 2, 0,		/* 0xc3 */
		{{0, 1},
		{6, 7},
		{0, 0},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xc4 */
		{{2, 2},
		{6, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xc5 */
		{{0, 0},
		{2, 2},
		{6, 7},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xc6 */
		{{1, 2},
		{6, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 2, 0,		/* 0xc7 */
		{{0, 2},
		{6, 7},
		{0, 0},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xc8 */
		{{3, 3},
		{6, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xc9 */
		{{0, 0},
		{3, 3},
		{6, 7},
		{0, 0}
		}
	},
	{0, 1, 3, 0,		/* 0xca */
		{{1, 1},
		{3, 3},
		{6, 7},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xcb */
		{{0, 1},
		{3, 3},
		{6, 7},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xcc */
		{{2, 3},
		{6, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xcd */
		{{0, 0},
		{2, 3},
		{6, 7},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xce */
		{{1, 3},
		{6, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 2, 0,		/* 0xcf */
		{{0, 3},
		{6, 7},
		{0, 0},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xd0 */
		{{4, 4},
		{6, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xd1 */
		{{0, 0},
		{4, 4},
		{6, 7},
		{0, 0}
		}
	},
	{0, 1, 3, 0,		/* 0xd2 */
		{{1, 1},
		{4, 4},
		{6, 7},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xd3 */
		{{0, 1},
		{4, 4},
		{6, 7},
		{0, 0}
		}
	},
	{0, 1, 3, 0,		/* 0xd4 */
		{{2, 2},
		{4, 4},
		{6, 7},
		{0, 0}
		}
	},
	{1, 1, 4, 0,		/* 0xd5 */
		{{0, 0},
		{2, 2},
		{4, 4},
		{6, 7}
		}
	},
	{0, 1, 3, 0,		/* 0xd6 */
		{{1, 2},
		{4, 4},
		{6, 7},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xd7 */
		{{0, 2},
		{4, 4},
		{6, 7},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xd8 */
		{{3, 4},
		{6, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xd9 */
		{{0, 0},
		{3, 4},
		{6, 7},
		{0, 0}
		}
	},
	{0, 1, 3, 0,		/* 0xda */
		{{1, 1},
		{3, 4},
		{6, 7},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xdb */
		{{0, 1},
		{3, 4},
		{6, 7},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xdc */
		{{2, 4},
		{6, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xdd */
		{{0, 0},
		{2, 4},
		{6, 7},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xde */
		{{1, 4},
		{6, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 2, 0,		/* 0xdf */
		{{0, 4},
		{6, 7},
		{0, 0},
		{0, 0}
		}
	},
	{0, 1, 1, 0,		/* 0xe0 */
		{{5, 7},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 2, 0,		/* 0xe1 */
		{{0, 0},
		{5, 7},
		{0, 0},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xe2 */
		{{1, 1},
		{5, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 2, 0,		/* 0xe3 */
		{{0, 1},
		{5, 7},
		{0, 0},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xe4 */
		{{2, 2},
		{5, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xe5 */
		{{0, 0},
		{2, 2},
		{5, 7},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xe6 */
		{{1, 2},
		{5, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 2, 0,		/* 0xe7 */
		{{0, 2},
		{5, 7},
		{0, 0},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xe8 */
		{{3, 3},
		{5, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xe9 */
		{{0, 0},
		{3, 3},
		{5, 7},
		{0, 0}
		}
	},
	{0, 1, 3, 0,		/* 0xea */
		{{1, 1},
		{3, 3},
		{5, 7},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xeb */
		{{0, 1},
		{3, 3},
		{5, 7},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xec */
		{{2, 3},
		{5, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xed */
		{{0, 0},
		{2, 3},
		{5, 7},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xee */
		{{1, 3},
		{5, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 2, 0,		/* 0xef */
		{{0, 3},
		{5, 7},
		{0, 0},
		{0, 0}
		}
	},
	{0, 1, 1, 0,		/* 0xf0 */
		{{4, 7},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 2, 0,		/* 0xf1 */
		{{0, 0},
		{4, 7},
		{0, 0},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xf2 */
		{{1, 1},
		{4, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 2, 0,		/* 0xf3 */
		{{0, 1},
		{4, 7},
		{0, 0},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xf4 */
		{{2, 2},
		{4, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 3, 0,		/* 0xf5 */
		{{0, 0},
		{2, 2},
		{4, 7},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xf6 */
		{{1, 2},
		{4, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 2, 0,		/* 0xf7 */
		{{0, 2},
		{4, 7},
		{0, 0},
		{0, 0}
		}
	},
	{0, 1, 1, 0,		/* 0xf8 */
		{{3, 7},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 2, 0,		/* 0xf9 */
		{{0, 0},
		{3, 7},
		{0, 0},
		{0, 0}
		}
	},
	{0, 1, 2, 0,		/* 0xfa */
		{{1, 1},
		{3, 7},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 2, 0,		/* 0xfb */
		{{0, 1},
		{3, 7},
		{0, 0},
		{0, 0}
		}
	},
	{0, 1, 1, 0,		/* 0xfc */
		{{2, 7},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 2, 0,		/* 0xfd */
		{{0, 0},
		{2, 7},
		{0, 0},
		{0, 0}
		}
	},
	{0, 1, 1, 0,		/* 0xfe */
		{{1, 7},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	},
	{1, 1, 1, 0,		/* 0xff */
		{{0, 7},
		{0, 0},
		{0, 0},
		{0, 0}
		}
	}
};


int
sctp_is_address_in_scope(struct sctp_ifa *ifa,
    int ipv4_addr_legal,
    int ipv6_addr_legal,
    int loopback_scope,
    int ipv4_local_scope,
    int local_scope,
    int site_scope,
    int do_update)
{
	if ((loopback_scope == 0) &&
	    (ifa->ifn_p) && SCTP_IFN_IS_IFT_LOOP(ifa->ifn_p)) {
		/*
		 * skip loopback if not in scope *
		 */
		return (0);
	}
	switch (ifa->address.sa.sa_family) {
#ifdef INET
	case AF_INET:
		if (ipv4_addr_legal) {
			struct sockaddr_in *sin;

			sin = (struct sockaddr_in *)&ifa->address.sin;
			if (sin->sin_addr.s_addr == 0) {
				/* not in scope , unspecified */
				return (0);
			}
			if ((ipv4_local_scope == 0) &&
			    (IN4_ISPRIVATE_ADDRESS(&sin->sin_addr))) {
				/* private address not in scope */
				return (0);
			}
		} else {
			return (0);
		}
		break;
#endif
#ifdef INET6
	case AF_INET6:
		if (ipv6_addr_legal) {
			struct sockaddr_in6 *sin6;

#if !defined(__Panda__)
			/* Must update the flags,  bummer, which
			 * means any IFA locks must now be applied HERE <->
			 */
			if (do_update) {
				sctp_gather_internal_ifa_flags(ifa);
			}
#endif
			if (ifa->localifa_flags & SCTP_ADDR_IFA_UNUSEABLE) {
				return (0);
			}
			/* ok to use deprecated addresses? */
			sin6 = (struct sockaddr_in6 *)&ifa->address.sin6;
			if (IN6_IS_ADDR_UNSPECIFIED(&sin6->sin6_addr)) {
				/* skip unspecifed addresses */
				return (0);
			}
			if (		/* (local_scope == 0) && */
			    (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr))) {
				return (0);
			}
			if ((site_scope == 0) &&
			    (IN6_IS_ADDR_SITELOCAL(&sin6->sin6_addr))) {
				return (0);
			}
		} else {
			return (0);
		}
		break;
#endif
	default:
		return (0);
	}
	return (1);
}

static struct mbuf *
sctp_add_addr_to_mbuf(struct mbuf *m, struct sctp_ifa *ifa)
{
	struct sctp_paramhdr *parmh;
	struct mbuf *mret;
	int len;

	switch (ifa->address.sa.sa_family) {
#ifdef INET
	case AF_INET:
		len = sizeof(struct sctp_ipv4addr_param);
		break;
#endif
#ifdef INET6
	case AF_INET6:
		len = sizeof(struct sctp_ipv6addr_param);
		break;
#endif
	default:
		return (m);
	}
	if (M_TRAILINGSPACE(m) >= len) {
		/* easy side we just drop it on the end */
		parmh = (struct sctp_paramhdr *)(SCTP_BUF_AT(m, SCTP_BUF_LEN(m)));
		mret = m;
	} else {
		/* Need more space */
		mret = m;
		while (SCTP_BUF_NEXT(mret) != NULL) {
			mret = SCTP_BUF_NEXT(mret);
		}
		SCTP_BUF_NEXT(mret) = sctp_get_mbuf_for_msg(len, 0, M_DONTWAIT, 1, MT_DATA);
		if (SCTP_BUF_NEXT(mret) == NULL) {
			/* We are hosed, can't add more addresses */
			return (m);
		}
		mret = SCTP_BUF_NEXT(mret);
		parmh = mtod(mret, struct sctp_paramhdr *);
	}
	/* now add the parameter */
	switch (ifa->address.sa.sa_family) {
#ifdef INET
	case AF_INET:
	{
		struct sctp_ipv4addr_param *ipv4p;
		struct sockaddr_in *sin;

		sin = (struct sockaddr_in *)&ifa->address.sin;
		ipv4p = (struct sctp_ipv4addr_param *)parmh;
		parmh->param_type = htons(SCTP_IPV4_ADDRESS);
		parmh->param_length = htons(len);
		ipv4p->addr = sin->sin_addr.s_addr;
		SCTP_BUF_LEN(mret) += len;
		break;
	}
#endif
#ifdef INET6
	case AF_INET6:
	{
		struct sctp_ipv6addr_param *ipv6p;
		struct sockaddr_in6 *sin6;

		sin6 = (struct sockaddr_in6 *)&ifa->address.sin6;
		ipv6p = (struct sctp_ipv6addr_param *)parmh;
		parmh->param_type = htons(SCTP_IPV6_ADDRESS);
		parmh->param_length = htons(len);
		memcpy(ipv6p->addr, &sin6->sin6_addr,
		    sizeof(ipv6p->addr));
#if defined(SCTP_EMBEDDED_V6_SCOPE)
		/* clear embedded scope in the address */
		in6_clearscope((struct in6_addr *)ipv6p->addr);
#endif
		SCTP_BUF_LEN(mret) += len;
		break;
	}
#endif
	default:
		return (m);
	}
	return (mret);
}


struct mbuf *
sctp_add_addresses_to_i_ia(struct sctp_inpcb *inp, struct sctp_tcb *stcb,
                           struct sctp_scoping *scope,
			   struct mbuf *m_at, int cnt_inits_to)
{
	struct sctp_vrf *vrf = NULL;
	int cnt, limit_out=0, total_count;
	uint32_t vrf_id;

	vrf_id = inp->def_vrf_id;
	SCTP_IPI_ADDR_RLOCK();
	vrf = sctp_find_vrf(vrf_id);
	if (vrf == NULL) {
		SCTP_IPI_ADDR_RUNLOCK();
		return(m_at);
	}
	if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) {
		struct sctp_ifa *sctp_ifap;
		struct sctp_ifn *sctp_ifnp;

		cnt = cnt_inits_to;
		if (vrf->total_ifa_count > SCTP_COUNT_LIMIT) {
			limit_out = 1;
			cnt = SCTP_ADDRESS_LIMIT;
			goto skip_count;
		}
		LIST_FOREACH(sctp_ifnp, &vrf->ifnlist, next_ifn) {
			if ((scope->loopback_scope == 0) &&
			    SCTP_IFN_IS_IFT_LOOP(sctp_ifnp)) {
				/*
				 * Skip loopback devices if loopback_scope
				 * not set
				 */
				continue;
			}
			LIST_FOREACH(sctp_ifap, &sctp_ifnp->ifalist, next_ifa) {
				if (sctp_is_addr_restricted(stcb, sctp_ifap)) {
					continue;
				}
				if (sctp_is_address_in_scope(sctp_ifap,
							     scope->ipv4_addr_legal,
							     scope->ipv6_addr_legal,
							     scope->loopback_scope,
							     scope->ipv4_local_scope,
							     scope->local_scope,
							     scope->site_scope, 1) == 0) {
					continue;
				}
				cnt++;
				if (cnt > SCTP_ADDRESS_LIMIT) {
					break;
				}
			}
			if (cnt > SCTP_ADDRESS_LIMIT) {
				break;
			}
		}
	skip_count:
		if (cnt > 1) {
			total_count = 0;
			LIST_FOREACH(sctp_ifnp, &vrf->ifnlist, next_ifn) {
				cnt = 0;
				if ((scope->loopback_scope == 0) &&
				    SCTP_IFN_IS_IFT_LOOP(sctp_ifnp)) {
					/*
					 * Skip loopback devices if
					 * loopback_scope not set
					 */
					continue;
				}
				LIST_FOREACH(sctp_ifap, &sctp_ifnp->ifalist, next_ifa) {
					if (sctp_is_addr_restricted(stcb, sctp_ifap)) {
						continue;
					}
					if (sctp_is_address_in_scope(sctp_ifap,
								     scope->ipv4_addr_legal,
								     scope->ipv6_addr_legal,
								     scope->loopback_scope,
								     scope->ipv4_local_scope,
								     scope->local_scope,
								     scope->site_scope, 0) == 0) {
						continue;
					}
					m_at = sctp_add_addr_to_mbuf(m_at, sctp_ifap);
					if (limit_out) {
						cnt++;
						total_count++;
						if (cnt >= 2) {
							/* two from each address */
							break;
						}
						if (total_count > SCTP_ADDRESS_LIMIT) {
							/* No more addresses */
							break;
						}
					}
				}
			}
		}
	} else {
		struct sctp_laddr *laddr;

		cnt = cnt_inits_to;
		/* First, how many ? */
		LIST_FOREACH(laddr, &inp->sctp_addr_list, sctp_nxt_addr) {
			if (laddr->ifa == NULL) {
				continue;
			}
			if (laddr->ifa->localifa_flags & SCTP_BEING_DELETED)
                                /* Address being deleted by the system, dont
				 * list.
				 */
				continue;
			if (laddr->action == SCTP_DEL_IP_ADDRESS) {
				/* Address being deleted on this ep
				 * don't list.
				 */
				continue;
			}
			if (sctp_is_address_in_scope(laddr->ifa,
						     scope->ipv4_addr_legal,
						     scope->ipv6_addr_legal,
						     scope->loopback_scope,
						     scope->ipv4_local_scope,
						     scope->local_scope,
						     scope->site_scope, 1) == 0) {
				continue;
			}
			cnt++;
		}
		if (cnt > SCTP_ADDRESS_LIMIT) {
			limit_out = 1;
		}
		/*
		 * To get through a NAT we only list addresses if we have
		 * more than one. That way if you just bind a single address
		 * we let the source of the init dictate our address.
		 */
		if (cnt > 1) {
			LIST_FOREACH(laddr, &inp->sctp_addr_list, sctp_nxt_addr) {
				cnt = 0;
				if (laddr->ifa == NULL) {
					continue;
				}
				if (laddr->ifa->localifa_flags & SCTP_BEING_DELETED)
					continue;

				if (sctp_is_address_in_scope(laddr->ifa,
							     scope->ipv4_addr_legal,
							     scope->ipv6_addr_legal,
							     scope->loopback_scope,
							     scope->ipv4_local_scope,
							     scope->local_scope,
							     scope->site_scope, 0) == 0) {
					continue;
				}
				m_at = sctp_add_addr_to_mbuf(m_at, laddr->ifa);
				cnt++;
				if (cnt >= SCTP_ADDRESS_LIMIT) {
					break;
				}
			}
		}
	}
	SCTP_IPI_ADDR_RUNLOCK();
	return (m_at);
}

static struct sctp_ifa *
sctp_is_ifa_addr_preferred(struct sctp_ifa *ifa,
			   uint8_t dest_is_loop,
			   uint8_t dest_is_priv,
			   sa_family_t fam)
{
	uint8_t dest_is_global = 0;
	/* dest_is_priv is true if destination is a private address */
	/* dest_is_loop is true if destination is a loopback addresses */

	/**
	 * Here we determine if its a preferred address. A preferred address
	 * means it is the same scope or higher scope then the destination.
	 * L = loopback, P = private, G = global
	 * -----------------------------------------
	 *    src    |  dest | result
	 *  ----------------------------------------
	 *     L     |    L  |    yes
	 *  -----------------------------------------
	 *     P     |    L  |    yes-v4 no-v6
	 *  -----------------------------------------
	 *     G     |    L  |    yes-v4 no-v6
	 *  -----------------------------------------
	 *     L     |    P  |    no
	 *  -----------------------------------------
	 *     P     |    P  |    yes
	 *  -----------------------------------------
	 *     G     |    P  |    no
	 *   -----------------------------------------
	 *     L     |    G  |    no
	 *   -----------------------------------------
	 *     P     |    G  |    no
	 *    -----------------------------------------
	 *     G     |    G  |    yes
	 *    -----------------------------------------
	 */

	if (ifa->address.sa.sa_family != fam) {
		/* forget mis-matched family */
		return (NULL);
	}
	if ((dest_is_priv == 0) && (dest_is_loop == 0)) {
		dest_is_global = 1;
	}
	SCTPDBG(SCTP_DEBUG_OUTPUT2, "Is destination preferred:");
	SCTPDBG_ADDR(SCTP_DEBUG_OUTPUT2, &ifa->address.sa);
	/* Ok the address may be ok */
#ifdef INET6
	if (fam == AF_INET6) {
		/* ok to use deprecated addresses? no lets not! */
		if (ifa->localifa_flags & SCTP_ADDR_IFA_UNUSEABLE) {
			SCTPDBG(SCTP_DEBUG_OUTPUT3, "NO:1\n");
			return (NULL);
		}
		if (ifa->src_is_priv && !ifa->src_is_loop) {
			if (dest_is_loop) {
				SCTPDBG(SCTP_DEBUG_OUTPUT3, "NO:2\n");
				return (NULL);
			}
		}
		if (ifa->src_is_glob) {
			if (dest_is_loop) {
				SCTPDBG(SCTP_DEBUG_OUTPUT3, "NO:3\n");
				return (NULL);
			}
		}
	}
#endif
	/* Now that we know what is what, implement or table
	 * this could in theory be done slicker (it used to be), but this
	 * is straightforward and easier to validate :-)
	 */
	SCTPDBG(SCTP_DEBUG_OUTPUT3, "src_loop:%d src_priv:%d src_glob:%d\n",
		ifa->src_is_loop, ifa->src_is_priv, ifa->src_is_glob);
	SCTPDBG(SCTP_DEBUG_OUTPUT3, "dest_loop:%d dest_priv:%d dest_glob:%d\n",
		dest_is_loop, dest_is_priv, dest_is_global);

	if ((ifa->src_is_loop) && (dest_is_priv)) {
		SCTPDBG(SCTP_DEBUG_OUTPUT3, "NO:4\n");
		return (NULL);
	}
	if ((ifa->src_is_glob) && (dest_is_priv)) {
		SCTPDBG(SCTP_DEBUG_OUTPUT3, "NO:5\n");
		return (NULL);
	}
	if ((ifa->src_is_loop) && (dest_is_global)) {
		SCTPDBG(SCTP_DEBUG_OUTPUT3, "NO:6\n");
		return (NULL);
	}
	if ((ifa->src_is_priv) && (dest_is_global)) {
		SCTPDBG(SCTP_DEBUG_OUTPUT3, "NO:7\n");
		return (NULL);
	}
	SCTPDBG(SCTP_DEBUG_OUTPUT3, "YES\n");
	/* its a preferred address */
	return (ifa);
}

static struct sctp_ifa *
sctp_is_ifa_addr_acceptable(struct sctp_ifa *ifa,
			    uint8_t dest_is_loop,
			    uint8_t dest_is_priv,
			    sa_family_t fam)
{
	uint8_t dest_is_global=0;

	/**
	 * Here we determine if its a acceptable address. A acceptable
	 * address means it is the same scope or higher scope but we can
	 * allow for NAT which means its ok to have a global dest and a
	 * private src.
	 *
	 * L = loopback, P = private, G = global
	 * -----------------------------------------
	 *  src    |  dest | result
	 * -----------------------------------------
	 *   L     |   L   |    yes
	 *  -----------------------------------------
	 *   P     |   L   |    yes-v4 no-v6
	 *  -----------------------------------------
	 *   G     |   L   |    yes
	 * -----------------------------------------
	 *   L     |   P   |    no
	 * -----------------------------------------
	 *   P     |   P   |    yes
	 * -----------------------------------------
	 *   G     |   P   |    yes - May not work
	 * -----------------------------------------
	 *   L     |   G   |    no
	 * -----------------------------------------
	 *   P     |   G   |    yes - May not work
	 * -----------------------------------------
	 *   G     |   G   |    yes
	 * -----------------------------------------
	 */

	if (ifa->address.sa.sa_family != fam) {
		/* forget non matching family */
		SCTPDBG(SCTP_DEBUG_OUTPUT3, "ifa_fam:%d fam:%d\n",
			ifa->address.sa.sa_family, fam);
		return (NULL);
	}
	/* Ok the address may be ok */
	SCTPDBG_ADDR(SCTP_DEBUG_OUTPUT3, &ifa->address.sa);
	SCTPDBG(SCTP_DEBUG_OUTPUT3, "dst_is_loop:%d dest_is_priv:%d\n",
		dest_is_loop, dest_is_priv);
	if ((dest_is_loop == 0) && (dest_is_priv == 0)) {
		dest_is_global = 1;
	}
#ifdef INET6
	if (fam == AF_INET6) {
		/* ok to use deprecated addresses? */
		if (ifa->localifa_flags & SCTP_ADDR_IFA_UNUSEABLE) {
			return (NULL);
		}
		if (ifa->src_is_priv) {
			/* Special case, linklocal to loop */
			if (dest_is_loop)
				return (NULL);
		}
	}
#endif
	/*
	 * Now that we know what is what, implement our table.
	 * This could in theory be done slicker (it used to be), but this
	 * is straightforward and easier to validate :-)
	 */
	SCTPDBG(SCTP_DEBUG_OUTPUT3, "ifa->src_is_loop:%d dest_is_priv:%d\n", 
		ifa->src_is_loop,
		dest_is_priv);
	if ((ifa->src_is_loop == 1) && (dest_is_priv)) {
		return (NULL);
	}
	SCTPDBG(SCTP_DEBUG_OUTPUT3, "ifa->src_is_loop:%d dest_is_glob:%d\n", 
		ifa->src_is_loop,
		dest_is_global);
	if ((ifa->src_is_loop == 1) && (dest_is_global)) {
		return (NULL);
	}
	SCTPDBG(SCTP_DEBUG_OUTPUT3, "address is acceptable\n");
	/* its an acceptable address */
	return (ifa);
}

int
sctp_is_addr_restricted(struct sctp_tcb *stcb, struct sctp_ifa *ifa)
{
	struct sctp_laddr *laddr;

	if (stcb == NULL) {
		/* There are no restrictions, no TCB :-) */
		return (0);
	}
	LIST_FOREACH(laddr, &stcb->asoc.sctp_restricted_addrs, sctp_nxt_addr) {
		if (laddr->ifa == NULL) {
			SCTPDBG(SCTP_DEBUG_OUTPUT1, "%s: NULL ifa\n",
				__FUNCTION__);
			continue;
		}
		if (laddr->ifa == ifa) {
			/* Yes it is on the list */
			return (1);
		}
	}
	return (0);
}


int
sctp_is_addr_in_ep(struct sctp_inpcb *inp, struct sctp_ifa *ifa)
{
	struct sctp_laddr *laddr;

	if (ifa == NULL)
		return (0);
	LIST_FOREACH(laddr, &inp->sctp_addr_list, sctp_nxt_addr) {
		if (laddr->ifa == NULL) {
			SCTPDBG(SCTP_DEBUG_OUTPUT1, "%s: NULL ifa\n",
				__FUNCTION__);
			continue;
		}
		if ((laddr->ifa == ifa) && laddr->action == 0)
			/* same pointer */
			return (1);
	}
	return (0);
}



static struct sctp_ifa *
sctp_choose_boundspecific_inp(struct sctp_inpcb *inp,
			      sctp_route_t *ro,
			      uint32_t vrf_id,
			      int non_asoc_addr_ok,
			      uint8_t dest_is_priv,
			      uint8_t dest_is_loop,
			      sa_family_t fam)
{
	struct sctp_laddr *laddr, *starting_point;
	void *ifn;
	int resettotop=0;
	struct sctp_ifn *sctp_ifn;
	struct sctp_ifa *sctp_ifa, *sifa;
	struct sctp_vrf *vrf;
	uint32_t ifn_index;

	vrf = sctp_find_vrf(vrf_id);
	if (vrf == NULL)
		return (NULL);

	ifn = SCTP_GET_IFN_VOID_FROM_ROUTE(ro);
	ifn_index = SCTP_GET_IF_INDEX_FROM_ROUTE(ro);
	sctp_ifn = sctp_find_ifn(ifn, ifn_index);
	/*
	 * first question, is the ifn we will emit on in our list, if so, we
	 * want such an address. Note that we first looked for a
	 * preferred address.
	 */
	if (sctp_ifn) {
		/* is a preferred one on the interface we route out? */
		LIST_FOREACH(sctp_ifa, &sctp_ifn->ifalist, next_ifa) {
			if ((sctp_ifa->localifa_flags & SCTP_ADDR_DEFER_USE) &&
			    (non_asoc_addr_ok == 0))
				continue;
			sifa = sctp_is_ifa_addr_preferred(sctp_ifa,
							  dest_is_loop,
							  dest_is_priv, fam);
			if (sifa == NULL)
				continue;
			if (sctp_is_addr_in_ep(inp, sifa)) {
				atomic_add_int(&sifa->refcount, 1);
				return (sifa);
			}
		}
	}
	/*
	 * ok, now we now need to find one on the list of the addresses.
	 * We can't get one on the emitting interface so let's find first
	 * a preferred one. If not that an acceptable one otherwise...
	 * we return NULL.
	 */
	starting_point = inp->next_addr_touse;
 once_again:
	if (inp->next_addr_touse == NULL) {
		inp->next_addr_touse = LIST_FIRST(&inp->sctp_addr_list);
		resettotop = 1;
	}
	for (laddr = inp->next_addr_touse; laddr;
	     laddr = LIST_NEXT(laddr, sctp_nxt_addr)) {
		if (laddr->ifa == NULL) {
			/* address has been removed */
			continue;
		}
		if (laddr->action == SCTP_DEL_IP_ADDRESS) {
			/* address is being deleted */
			continue;
		}
		sifa = sctp_is_ifa_addr_preferred(laddr->ifa, dest_is_loop,
						  dest_is_priv, fam);
		if (sifa == NULL)
			continue;
		atomic_add_int(&sifa->refcount, 1);
		return (sifa);
	}
	if (resettotop == 0) {
		inp->next_addr_touse = NULL;
		goto once_again;
	}

	inp->next_addr_touse = starting_point;
	resettotop = 0;
 once_again_too:
	if (inp->next_addr_touse == NULL) {
		inp->next_addr_touse = LIST_FIRST(&inp->sctp_addr_list);
		resettotop = 1;
	}

	/* ok, what about an acceptable address in the inp */
	for (laddr = inp->next_addr_touse; laddr;
	     laddr = LIST_NEXT(laddr, sctp_nxt_addr)) {
		if (laddr->ifa == NULL) {
			/* address has been removed */
			continue;
		}
		if (laddr->action == SCTP_DEL_IP_ADDRESS) {
			/* address is being deleted */
			continue;
		}
		sifa = sctp_is_ifa_addr_acceptable(laddr->ifa, dest_is_loop,
						   dest_is_priv, fam);
		if (sifa == NULL)
			continue;
		atomic_add_int(&sifa->refcount, 1);
		return (sifa);
	}
	if (resettotop == 0) {
		inp->next_addr_touse = NULL;
		goto once_again_too;
	}

	/*
	 * no address bound can be a source for the destination we are in
	 * trouble
	 */
	return (NULL);
}



static struct sctp_ifa *
sctp_choose_boundspecific_stcb(struct sctp_inpcb *inp,
			       struct sctp_tcb *stcb,
			       struct sctp_nets *net,
			       sctp_route_t *ro,
			       uint32_t vrf_id,
			       uint8_t dest_is_priv,
			       uint8_t dest_is_loop,
			       int non_asoc_addr_ok,
			       sa_family_t fam)
{
	struct sctp_laddr *laddr, *starting_point;
	void *ifn;
	struct sctp_ifn *sctp_ifn;
	struct sctp_ifa *sctp_ifa, *sifa;
	uint8_t start_at_beginning = 0;
	struct sctp_vrf *vrf;
	uint32_t ifn_index;
	/*
	 * first question, is the ifn we will emit on in our list, if so, we
	 * want that one.
	 */
	vrf = sctp_find_vrf(vrf_id);
	if (vrf == NULL)
		return (NULL);

	ifn = SCTP_GET_IFN_VOID_FROM_ROUTE(ro);
	ifn_index = SCTP_GET_IF_INDEX_FROM_ROUTE(ro);
	sctp_ifn = sctp_find_ifn( ifn, ifn_index);

	/*
 	 * first question, is the ifn we will emit on in our list?  If so,
	 * we want that one. First we look for a preferred. Second, we go
	 * for an acceptable.
	 */
	if (sctp_ifn) {
		/* first try for a preferred address on the ep */
		LIST_FOREACH(sctp_ifa, &sctp_ifn->ifalist, next_ifa) {
			if ((sctp_ifa->localifa_flags & SCTP_ADDR_DEFER_USE) && (non_asoc_addr_ok == 0))
				continue;
			if (sctp_is_addr_in_ep(inp, sctp_ifa)) {
				sifa = sctp_is_ifa_addr_preferred(sctp_ifa, dest_is_loop, dest_is_priv, fam);
				if (sifa == NULL)
					continue;
				if (((non_asoc_addr_ok == 0) &&
				     (sctp_is_addr_restricted(stcb, sifa))) ||
				    (non_asoc_addr_ok &&
				     (sctp_is_addr_restricted(stcb, sifa)) &&
				     (!sctp_is_addr_pending(stcb, sifa)))) {
					/* on the no-no list */
					continue;
				}
				atomic_add_int(&sifa->refcount, 1);
				return (sifa);
			}
		}
		/* next try for an acceptable address on the ep */
		LIST_FOREACH(sctp_ifa, &sctp_ifn->ifalist, next_ifa) {
			if ((sctp_ifa->localifa_flags & SCTP_ADDR_DEFER_USE) && (non_asoc_addr_ok == 0))
				continue;
			if (sctp_is_addr_in_ep(inp, sctp_ifa)) {
				sifa= sctp_is_ifa_addr_acceptable(sctp_ifa, dest_is_loop, dest_is_priv,fam);
				if (sifa == NULL)
					continue;
				if (((non_asoc_addr_ok == 0) &&
				     (sctp_is_addr_restricted(stcb, sifa))) ||
				    (non_asoc_addr_ok &&
				     (sctp_is_addr_restricted(stcb, sifa)) &&
				     (!sctp_is_addr_pending(stcb, sifa)))) {
					/* on the no-no list */
					continue;
				}
				atomic_add_int(&sifa->refcount, 1);
				return (sifa);
			}
		}

	}
	/*
	 * if we can't find one like that then we must look at all
	 * addresses bound to pick one at first preferable then
	 * secondly acceptable.
	 */
	starting_point = stcb->asoc.last_used_address;
 sctp_from_the_top:
	if (stcb->asoc.last_used_address == NULL) {
		start_at_beginning = 1;
		stcb->asoc.last_used_address = LIST_FIRST(&inp->sctp_addr_list);
	}
	/* search beginning with the last used address */
	for (laddr = stcb->asoc.last_used_address; laddr;
	     laddr = LIST_NEXT(laddr, sctp_nxt_addr)) {
		if (laddr->ifa == NULL) {
			/* address has been removed */
			continue;
		}
		if (laddr->action == SCTP_DEL_IP_ADDRESS) {
			/* address is being deleted */
			continue;
		}
		sifa = sctp_is_ifa_addr_preferred(laddr->ifa, dest_is_loop, dest_is_priv, fam);
		if (sifa == NULL)
			continue;
		if (((non_asoc_addr_ok == 0) &&
		     (sctp_is_addr_restricted(stcb, sifa))) ||
		    (non_asoc_addr_ok &&
		     (sctp_is_addr_restricted(stcb, sifa)) &&
		     (!sctp_is_addr_pending(stcb, sifa)))) {
			/* on the no-no list */
			continue;
		}
		stcb->asoc.last_used_address = laddr;
		atomic_add_int(&sifa->refcount, 1);
		return (sifa);
	}
	if (start_at_beginning == 0) {
		stcb->asoc.last_used_address = NULL;
		goto sctp_from_the_top;
	}
	/* now try for any higher scope than the destination */
	stcb->asoc.last_used_address = starting_point;
	start_at_beginning = 0;
 sctp_from_the_top2:
	if (stcb->asoc.last_used_address == NULL) {
		start_at_beginning = 1;
		stcb->asoc.last_used_address = LIST_FIRST(&inp->sctp_addr_list);
	}
	/* search beginning with the last used address */
	for (laddr = stcb->asoc.last_used_address; laddr;
	     laddr = LIST_NEXT(laddr, sctp_nxt_addr)) {
		if (laddr->ifa == NULL) {
			/* address has been removed */
			continue;
		}
		if (laddr->action == SCTP_DEL_IP_ADDRESS) {
			/* address is being deleted */
			continue;
		}
		sifa = sctp_is_ifa_addr_acceptable(laddr->ifa, dest_is_loop,
						   dest_is_priv, fam);
		if (sifa == NULL)
			continue;
		if (((non_asoc_addr_ok == 0) &&
		     (sctp_is_addr_restricted(stcb, sifa))) ||
		    (non_asoc_addr_ok &&
		     (sctp_is_addr_restricted(stcb, sifa)) &&
		     (!sctp_is_addr_pending(stcb, sifa)))) {
			/* on the no-no list */
			continue;
		}
		stcb->asoc.last_used_address = laddr;
		atomic_add_int(&sifa->refcount, 1);
		return (sifa);
	}
	if (start_at_beginning == 0) {
		stcb->asoc.last_used_address = NULL;
		goto sctp_from_the_top2;
	}
	return (NULL);
}

static struct sctp_ifa *
sctp_select_nth_preferred_addr_from_ifn_boundall(struct sctp_ifn *ifn,
						 struct sctp_tcb *stcb,
						 int non_asoc_addr_ok,
						 uint8_t dest_is_loop,
						 uint8_t dest_is_priv,
						 int addr_wanted,
						 sa_family_t fam,
						 sctp_route_t *ro
						 )
{
	struct sctp_ifa *ifa, *sifa;
	int num_eligible_addr = 0;
#ifdef INET6
#ifdef SCTP_EMBEDDED_V6_SCOPE
	struct sockaddr_in6 sin6, lsa6;

	if (fam == AF_INET6) {
		memcpy(&sin6, &ro->ro_dst, sizeof(struct sockaddr_in6));
#ifdef SCTP_KAME
		(void)sa6_recoverscope(&sin6);
#else
		(void)in6_recoverscope(&sin6, &sin6.sin6_addr, NULL);
#endif  /* SCTP_KAME */
	}
#endif  /* SCTP_EMBEDDED_V6_SCOPE */
#endif	/* INET6 */
	LIST_FOREACH(ifa, &ifn->ifalist, next_ifa) {
		if ((ifa->localifa_flags & SCTP_ADDR_DEFER_USE) &&
		    (non_asoc_addr_ok == 0))
			continue;
		sifa = sctp_is_ifa_addr_preferred(ifa, dest_is_loop,
						  dest_is_priv, fam);
		if (sifa == NULL)
			continue;
#ifdef INET6
		if (fam == AF_INET6 &&
		    dest_is_loop &&
		    sifa->src_is_loop && sifa->src_is_priv) {
			/* don't allow fe80::1 to be a src on loop ::1, we don't list it
			 * to the peer so we will get an abort.
			 */
			continue;
		}
#ifdef SCTP_EMBEDDED_V6_SCOPE
		if (fam == AF_INET6 &&
		    IN6_IS_ADDR_LINKLOCAL(&sifa->address.sin6.sin6_addr) &&
		    IN6_IS_ADDR_LINKLOCAL(&sin6.sin6_addr)) {
			/* link-local <-> link-local must belong to the same scope. */
			memcpy(&lsa6, &sifa->address.sin6, sizeof(struct sockaddr_in6));
#ifdef SCTP_KAME
			(void)sa6_recoverscope(&lsa6);
#else
			(void)in6_recoverscope(&lsa6, &lsa6.sin6_addr, NULL);
#endif  /* SCTP_KAME */
			if (sin6.sin6_scope_id != lsa6.sin6_scope_id) {
				continue;
			}
		}
#endif  /* SCTP_EMBEDDED_V6_SCOPE */
#endif	/* INET6 */

#if defined(__Userspace__)
                /* __Userspace avoids IPv6 for now... */
#endif
#if defined(__FreeBSD__) || defined(__APPLE__)
		/* Check if the IPv6 address matches to next-hop.
		   In the mobile case, old IPv6 address may be not deleted
		   from the interface. Then, the interface has previous and
		   new addresses.  We should use one corresponding to the
		   next-hop.  (by micchie)
		 */
#ifdef INET6
		if (stcb && fam == AF_INET6 &&
		    sctp_is_mobility_feature_on(stcb->sctp_ep, SCTP_MOBILITY_BASE)) {
			if (sctp_v6src_match_nexthop(&sifa->address.sin6, ro)
			    == 0) {
				continue;
			}
		}
#endif
#ifdef INET
		/* Avoid topologically incorrect IPv4 address */
		if (stcb && fam == AF_INET &&
		    sctp_is_mobility_feature_on(stcb->sctp_ep, SCTP_MOBILITY_BASE)) {
			if (sctp_v4src_match_nexthop(sifa, ro) == 0) {
				continue;
			}
		}
#endif
#endif
		if (stcb) {
			if (sctp_is_address_in_scope(ifa,
			                             stcb->asoc.ipv4_addr_legal,
			                             stcb->asoc.ipv6_addr_legal,
			                             stcb->asoc.loopback_scope,
			                             stcb->asoc.ipv4_local_scope,
			                             stcb->asoc.local_scope,
			                             stcb->asoc.site_scope, 0) == 0) {
				continue;
			}
			if (((non_asoc_addr_ok == 0) &&
			     (sctp_is_addr_restricted(stcb, sifa))) ||
			    (non_asoc_addr_ok &&
			     (sctp_is_addr_restricted(stcb, sifa)) &&
			     (!sctp_is_addr_pending(stcb, sifa)))) {
				/*
				 * It is restricted for some reason..
				 * probably not yet added.
				 */
				continue;
			}
		}
		if (num_eligible_addr >= addr_wanted) {
			return (sifa);
		}
		num_eligible_addr++;
	}
	return (NULL);
}


static int
sctp_count_num_preferred_boundall(struct sctp_ifn *ifn,
				  struct sctp_tcb *stcb,
				  int non_asoc_addr_ok,
				  uint8_t dest_is_loop,
				  uint8_t dest_is_priv,
				  sa_family_t fam)
{
	struct sctp_ifa *ifa, *sifa;
	int num_eligible_addr = 0;

	LIST_FOREACH(ifa, &ifn->ifalist, next_ifa) {
		if ((ifa->localifa_flags & SCTP_ADDR_DEFER_USE) &&
		    (non_asoc_addr_ok == 0)) {
			continue;
		}
		sifa = sctp_is_ifa_addr_preferred(ifa, dest_is_loop,
						  dest_is_priv, fam);
		if (sifa == NULL) {
			continue;
		}
		if (stcb) {
			if (sctp_is_address_in_scope(ifa,
			                             stcb->asoc.ipv4_addr_legal,
			                             stcb->asoc.ipv6_addr_legal,
			                             stcb->asoc.loopback_scope,
			                             stcb->asoc.ipv4_local_scope,
			                             stcb->asoc.local_scope,
			                             stcb->asoc.site_scope, 0) == 0) {
				continue;
			}
			if (((non_asoc_addr_ok == 0) &&
			     (sctp_is_addr_restricted(stcb, sifa))) ||
			    (non_asoc_addr_ok &&
			     (sctp_is_addr_restricted(stcb, sifa)) &&
			     (!sctp_is_addr_pending(stcb, sifa)))) {
				/*
				 * It is restricted for some reason..
				 * probably not yet added.
				 */
				continue;
			}
		}
		num_eligible_addr++;
	}
	return (num_eligible_addr);
}

static struct sctp_ifa *
sctp_choose_boundall(struct sctp_inpcb *inp,
		     struct sctp_tcb *stcb,
		     struct sctp_nets *net,
		     sctp_route_t *ro,
		     uint32_t vrf_id,
		     uint8_t dest_is_priv,
		     uint8_t dest_is_loop,
		     int non_asoc_addr_ok,
		     sa_family_t fam)
{
	int cur_addr_num = 0, num_preferred = 0;
	void *ifn;
	struct sctp_ifn *sctp_ifn, *looked_at=NULL, *emit_ifn;
	struct sctp_ifa *sctp_ifa, *sifa;
	uint32_t ifn_index;
	struct sctp_vrf *vrf;
#ifdef INET
	int retried = 0;
#endif
	/*-
	 * For boundall we can use any address in the association.
	 * If non_asoc_addr_ok is set we can use any address (at least in
	 * theory). So we look for preferred addresses first. If we find one,
	 * we use it. Otherwise we next try to get an address on the
	 * interface, which we should be able to do (unless non_asoc_addr_ok
	 * is false and we are routed out that way). In these cases where we
	 * can't use the address of the interface we go through all the
	 * ifn's looking for an address we can use and fill that in. Punting
	 * means we send back address 0, which will probably cause problems
	 * actually since then IP will fill in the address of the route ifn,
	 * which means we probably already rejected it.. i.e. here comes an
	 * abort :-<.
	 */
	vrf = sctp_find_vrf(vrf_id);
	if (vrf == NULL)
		return (NULL);

	ifn = SCTP_GET_IFN_VOID_FROM_ROUTE(ro);
	ifn_index = SCTP_GET_IF_INDEX_FROM_ROUTE(ro);
	SCTPDBG(SCTP_DEBUG_OUTPUT2,"ifn from route:%p ifn_index:%d\n", ifn, ifn_index);
	emit_ifn = looked_at = sctp_ifn = sctp_find_ifn(ifn, ifn_index);
	if (sctp_ifn == NULL) {
		/* ?? We don't have this guy ?? */
		SCTPDBG(SCTP_DEBUG_OUTPUT2,"No ifn emit interface?\n");
		goto bound_all_plan_b;
	}
	SCTPDBG(SCTP_DEBUG_OUTPUT2,"ifn_index:%d name:%s is emit interface\n",
		ifn_index, sctp_ifn->ifn_name);

	if (net) {
		cur_addr_num = net->indx_of_eligible_next_to_use;
	}
	num_preferred = sctp_count_num_preferred_boundall(sctp_ifn,
							  stcb,
							  non_asoc_addr_ok,
							  dest_is_loop,
							  dest_is_priv, fam);
	SCTPDBG(SCTP_DEBUG_OUTPUT2, "Found %d preferred source addresses for intf:%s\n",
		num_preferred, sctp_ifn->ifn_name);
	if (num_preferred == 0) {
		/*
		 * no eligible addresses, we must use some other interface
		 * address if we can find one.
		 */
		goto bound_all_plan_b;
	}
	/*
	 * Ok we have num_eligible_addr set with how many we can use, this
	 * may vary from call to call due to addresses being deprecated
	 * etc..
	 */
	if (cur_addr_num >= num_preferred) {
		cur_addr_num = 0;
	}
	/*
	 * select the nth address from the list (where cur_addr_num is the
	 * nth) and 0 is the first one, 1 is the second one etc...
	 */
	SCTPDBG(SCTP_DEBUG_OUTPUT2, "cur_addr_num:%d\n", cur_addr_num);

	sctp_ifa = sctp_select_nth_preferred_addr_from_ifn_boundall(sctp_ifn, stcb, non_asoc_addr_ok, dest_is_loop,
								   dest_is_priv, cur_addr_num, fam, ro);

	/* if sctp_ifa is NULL something changed??, fall to plan b. */
	if (sctp_ifa) {
		atomic_add_int(&sctp_ifa->refcount, 1);
		if (net) {
			/* save off where the next one we will want */
			net->indx_of_eligible_next_to_use = cur_addr_num + 1;
		}
		return (sctp_ifa);
	}
	/*
	 * plan_b: Look at all interfaces and find a preferred address. If
	 * no preferred fall through to plan_c.
	 */
 bound_all_plan_b:
	SCTPDBG(SCTP_DEBUG_OUTPUT2, "Trying Plan B\n");
	LIST_FOREACH(sctp_ifn, &vrf->ifnlist, next_ifn) {
		SCTPDBG(SCTP_DEBUG_OUTPUT2, "Examine interface %s\n",
			sctp_ifn->ifn_name);
		if (dest_is_loop == 0 && SCTP_IFN_IS_IFT_LOOP(sctp_ifn)) {
			/* wrong base scope */
			SCTPDBG(SCTP_DEBUG_OUTPUT2, "skip\n");
			continue;
		}
		if ((sctp_ifn == looked_at) && looked_at) {
			/* already looked at this guy */
			SCTPDBG(SCTP_DEBUG_OUTPUT2, "already seen\n");
			continue;
		}
		num_preferred = sctp_count_num_preferred_boundall(sctp_ifn, stcb, non_asoc_addr_ok,
								dest_is_loop, dest_is_priv, fam);
		SCTPDBG(SCTP_DEBUG_OUTPUT2,
			"Found ifn:%p %d preferred source addresses\n",
			ifn, num_preferred);
		if (num_preferred == 0) {
			/* None on this interface. */
			SCTPDBG(SCTP_DEBUG_OUTPUT2, "No prefered -- skipping to next\n");
			continue;
		}
		SCTPDBG(SCTP_DEBUG_OUTPUT2,
			"num preferred:%d on interface:%p cur_addr_num:%d\n",
			num_preferred, sctp_ifn, cur_addr_num);

		/*
		 * Ok we have num_eligible_addr set with how many we can
		 * use, this may vary from call to call due to addresses
		 * being deprecated etc..
		 */
		if (cur_addr_num >= num_preferred) {
			cur_addr_num = 0;
		}
		sifa = sctp_select_nth_preferred_addr_from_ifn_boundall(sctp_ifn, stcb, non_asoc_addr_ok, dest_is_loop,
								       dest_is_priv, cur_addr_num, fam, ro);
		if (sifa == NULL)
			continue;
		if (net) {
			net->indx_of_eligible_next_to_use = cur_addr_num + 1;
			SCTPDBG(SCTP_DEBUG_OUTPUT2, "we selected %d\n",
				cur_addr_num);
			SCTPDBG(SCTP_DEBUG_OUTPUT2, "Source:");
			SCTPDBG_ADDR(SCTP_DEBUG_OUTPUT2, &sifa->address.sa);
			SCTPDBG(SCTP_DEBUG_OUTPUT2, "Dest:");
			SCTPDBG_ADDR(SCTP_DEBUG_OUTPUT2, &net->ro._l_addr.sa);
		}
		atomic_add_int(&sifa->refcount, 1);
		return (sifa);
	}
#ifdef INET
again_with_private_addresses_allowed:
#endif
	/* plan_c: do we have an acceptable address on the emit interface */
	sifa = NULL;
	SCTPDBG(SCTP_DEBUG_OUTPUT2,"Trying Plan C: find acceptable on interface\n");
	if (emit_ifn == NULL) {
		SCTPDBG(SCTP_DEBUG_OUTPUT2,"Jump to Plan D - no emit_ifn\n");
		goto plan_d;
	}
	LIST_FOREACH(sctp_ifa, &emit_ifn->ifalist, next_ifa) {
		SCTPDBG(SCTP_DEBUG_OUTPUT2, "ifa:%p\n", sctp_ifa);
		if ((sctp_ifa->localifa_flags & SCTP_ADDR_DEFER_USE) &&
		    (non_asoc_addr_ok == 0)) {
			SCTPDBG(SCTP_DEBUG_OUTPUT2,"Defer\n");
			continue;
		}
		sifa = sctp_is_ifa_addr_acceptable(sctp_ifa, dest_is_loop,
						   dest_is_priv, fam);
		if (sifa == NULL) {
			SCTPDBG(SCTP_DEBUG_OUTPUT2, "IFA not acceptable\n");
			continue;
		}
		if (stcb) {
			if (sctp_is_address_in_scope(sifa,
			                             stcb->asoc.ipv4_addr_legal,
			                             stcb->asoc.ipv6_addr_legal,
			                             stcb->asoc.loopback_scope,
			                             stcb->asoc.ipv4_local_scope,
			                             stcb->asoc.local_scope,
			                             stcb->asoc.site_scope, 0) == 0) {
				SCTPDBG(SCTP_DEBUG_OUTPUT2, "NOT in scope\n");
				sifa = NULL;
				continue;
			}
			if (((non_asoc_addr_ok == 0) &&
			     (sctp_is_addr_restricted(stcb, sifa))) ||
			    (non_asoc_addr_ok &&
			     (sctp_is_addr_restricted(stcb, sifa)) &&
			     (!sctp_is_addr_pending(stcb, sifa)))) {
				/*
				 * It is restricted for some
				 * reason.. probably not yet added.
				 */
				SCTPDBG(SCTP_DEBUG_OUTPUT2, "Its resticted\n");
				sifa = NULL;
				continue;
			}
		} else {
			printf("Stcb is null - no print\n");
		}
		atomic_add_int(&sifa->refcount, 1);
		goto out;
	}
 plan_d:
	/*
	 * plan_d: We are in trouble. No preferred address on the emit
	 * interface. And not even a preferred address on all interfaces.
	 * Go out and see if we can find an acceptable address somewhere
	 * amongst all interfaces.
	 */
	SCTPDBG(SCTP_DEBUG_OUTPUT2, "Trying Plan D looked_at is %p\n", looked_at);
	LIST_FOREACH(sctp_ifn, &vrf->ifnlist, next_ifn) {
		if (dest_is_loop == 0 && SCTP_IFN_IS_IFT_LOOP(sctp_ifn)) {
			/* wrong base scope */
			continue;
		}
		LIST_FOREACH(sctp_ifa, &sctp_ifn->ifalist, next_ifa) {
			if ((sctp_ifa->localifa_flags & SCTP_ADDR_DEFER_USE) &&
			    (non_asoc_addr_ok == 0))
				continue;
			sifa = sctp_is_ifa_addr_acceptable(sctp_ifa,
							   dest_is_loop,
							   dest_is_priv, fam);
			if (sifa == NULL)
				continue;
			if (stcb) {
				if (sctp_is_address_in_scope(sifa,
				                             stcb->asoc.ipv4_addr_legal,
				                             stcb->asoc.ipv6_addr_legal,
				                             stcb->asoc.loopback_scope,
				                             stcb->asoc.ipv4_local_scope,
				                             stcb->asoc.local_scope,
				                             stcb->asoc.site_scope, 0) == 0) {
					sifa = NULL;
					continue;
				}
				if (((non_asoc_addr_ok == 0) &&
				     (sctp_is_addr_restricted(stcb, sifa))) ||
				    (non_asoc_addr_ok &&
				     (sctp_is_addr_restricted(stcb, sifa)) &&
				     (!sctp_is_addr_pending(stcb, sifa)))) {
					/*
					 * It is restricted for some
					 * reason.. probably not yet added.
					 */
					sifa = NULL;
					continue;
				}
			}
			goto out;
		}
	}
#ifdef INET
	if ((retried == 0) && (stcb->asoc.ipv4_local_scope == 0)) {
		stcb->asoc.ipv4_local_scope = 1;
		retried = 1;
		goto again_with_private_addresses_allowed;
	} else if (retried == 1) {
		stcb->asoc.ipv4_local_scope = 0;
	}
#endif
out:
#ifdef INET
	if (sifa) {
		if (retried == 1) {
			LIST_FOREACH(sctp_ifn, &vrf->ifnlist, next_ifn) {
				if (dest_is_loop == 0 && SCTP_IFN_IS_IFT_LOOP(sctp_ifn)) {
					/* wrong base scope */
					continue;
				}
				LIST_FOREACH(sctp_ifa, &sctp_ifn->ifalist, next_ifa) {
					struct sctp_ifa *tmp_sifa;

					if ((sctp_ifa->localifa_flags & SCTP_ADDR_DEFER_USE) &&
					    (non_asoc_addr_ok == 0))
						continue;
					tmp_sifa = sctp_is_ifa_addr_acceptable(sctp_ifa,
					                                       dest_is_loop,
					                                       dest_is_priv, fam);
					if (tmp_sifa == NULL) {
						continue;
					}
					if (tmp_sifa == sifa) {
						continue;
					}
					if (stcb) {
						if (sctp_is_address_in_scope(tmp_sifa,
						                             stcb->asoc.ipv4_addr_legal,
						                             stcb->asoc.ipv6_addr_legal,
						                             stcb->asoc.loopback_scope,
						                             stcb->asoc.ipv4_local_scope,
						                             stcb->asoc.local_scope,
						                             stcb->asoc.site_scope, 0) == 0) {
							continue;
						}
						if (((non_asoc_addr_ok == 0) &&
						     (sctp_is_addr_restricted(stcb, tmp_sifa))) ||
						    (non_asoc_addr_ok &&
						     (sctp_is_addr_restricted(stcb, tmp_sifa)) &&
						     (!sctp_is_addr_pending(stcb, tmp_sifa)))) {
							/*
							 * It is restricted for some
							 * reason.. probably not yet added.
							 */
							continue;
						}
					}
					if ((tmp_sifa->address.sin.sin_family == AF_INET) &&
					    (IN4_ISPRIVATE_ADDRESS(&(tmp_sifa->address.sin.sin_addr)))) {
						sctp_add_local_addr_restricted(stcb, tmp_sifa);
					}
				}
			}
		}
		atomic_add_int(&sifa->refcount, 1);
	}
#endif
	return (sifa);
}



/* tcb may be NULL */
struct sctp_ifa *
sctp_source_address_selection(struct sctp_inpcb *inp,
			      struct sctp_tcb *stcb,
			      sctp_route_t *ro,
			      struct sctp_nets *net,
			      int non_asoc_addr_ok, uint32_t vrf_id)
{
	struct sctp_ifa *answer;
	uint8_t dest_is_priv, dest_is_loop;
	sa_family_t fam;
#ifdef INET
	struct sockaddr_in *to = (struct sockaddr_in *)&ro->ro_dst;
#endif
#ifdef INET6
	struct sockaddr_in6 *to6 = (struct sockaddr_in6 *)&ro->ro_dst;
#endif

	/**
	 * Rules: - Find the route if needed, cache if I can. - Look at
	 * interface address in route, Is it in the bound list. If so we
	 * have the best source. - If not we must rotate amongst the
	 * addresses.
	 *
	 * Cavets and issues
	 *
	 * Do we need to pay attention to scope. We can have a private address
	 * or a global address we are sourcing or sending to. So if we draw
	 * it out
	 * zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz
	 * For V4
	 * ------------------------------------------
	 *      source     *      dest  *  result
	 * -----------------------------------------
	 * <a>  Private    *    Global  *  NAT
	 * -----------------------------------------
	 * <b>  Private    *    Private *  No problem
	 * -----------------------------------------
	 * <c>  Global     *    Private *  Huh, How will this work?
	 * -----------------------------------------
	 * <d>  Global     *    Global  *  No Problem
	 *------------------------------------------
	 * zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz
	 * For V6
	 *------------------------------------------
	 *      source     *      dest  *  result
	 * -----------------------------------------
	 * <a>  Linklocal  *    Global  *
	 * -----------------------------------------
	 * <b>  Linklocal  * Linklocal  *  No problem
	 * -----------------------------------------
	 * <c>  Global     * Linklocal  *  Huh, How will this work?
	 * -----------------------------------------
	 * <d>  Global     *    Global  *  No Problem
	 *------------------------------------------
	 * zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz
	 *
	 * And then we add to that what happens if there are multiple addresses
	 * assigned to an interface. Remember the ifa on a ifn is a linked
	 * list of addresses. So one interface can have more than one IP
	 * address. What happens if we have both a private and a global
	 * address? Do we then use context of destination to sort out which
	 * one is best? And what about NAT's sending P->G may get you a NAT
	 * translation, or should you select the G thats on the interface in
	 * preference.
	 *
	 * Decisions:
	 *
	 * - count the number of addresses on the interface.
	 * - if it is one, no problem except case <c>.
	 *   For <a> we will assume a NAT out there.
	 * - if there are more than one, then we need to worry about scope P
	 *   or G. We should prefer G -> G and P -> P if possible.
	 *   Then as a secondary fall back to mixed types G->P being a last
	 *   ditch one.
	 * - The above all works for bound all, but bound specific we need to
	 *   use the same concept but instead only consider the bound
	 *   addresses. If the bound set is NOT assigned to the interface then
	 *   we must use rotation amongst the bound addresses..
	 */
	if (ro->ro_rt == NULL) {
		/*
		 * Need a route to cache.
		 */
		SCTP_RTALLOC(ro, vrf_id);
	}
	if (ro->ro_rt == NULL) {
		return (NULL);
	}
	fam = ro->ro_dst.sa_family;
	dest_is_priv = dest_is_loop = 0;
	/* Setup our scopes for the destination */
	switch (fam) {
#ifdef INET
	case AF_INET:
		/* Scope based on outbound address */
		if (IN4_ISLOOPBACK_ADDRESS(&to->sin_addr)) {
			dest_is_loop = 1;
			if (net != NULL) {
				/* mark it as local */
				net->addr_is_local = 1;
			}
		} else if ((IN4_ISPRIVATE_ADDRESS(&to->sin_addr))) {
			dest_is_priv = 1;
		}
		break;
#endif
#ifdef INET6
	case AF_INET6:
		/* Scope based on outbound address */
		if (IN6_IS_ADDR_LOOPBACK(&to6->sin6_addr) ||
		    SCTP_ROUTE_IS_REAL_LOOP(ro)) {
			/*
			 * If the address is a loopback address, which
			 * consists of "::1" OR "fe80::1%lo0", we are loopback
			 * scope. But we don't use dest_is_priv (link local
			 * addresses).
			 */
			dest_is_loop = 1;
			if (net != NULL) {
				/* mark it as local */
				net->addr_is_local = 1;
			}
		} else if (IN6_IS_ADDR_LINKLOCAL(&to6->sin6_addr)) {
			dest_is_priv = 1;
		}
		break;
#endif
	}
	SCTPDBG(SCTP_DEBUG_OUTPUT2, "Select source addr for:");
	SCTPDBG_ADDR(SCTP_DEBUG_OUTPUT2, (struct sockaddr *)&ro->ro_dst);
	SCTP_IPI_ADDR_RLOCK();
	if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUNDALL) {
		/*
		 * Bound all case
		 */
		answer = sctp_choose_boundall(inp, stcb, net, ro, vrf_id,
					      dest_is_priv, dest_is_loop,
					      non_asoc_addr_ok, fam);
		SCTP_IPI_ADDR_RUNLOCK();
		return (answer);
	}
	/*
	 * Subset bound case
	 */
	if (stcb) {
		answer = sctp_choose_boundspecific_stcb(inp, stcb, net, ro,
							vrf_id,	dest_is_priv,
							dest_is_loop,
							non_asoc_addr_ok, fam);
	} else {
		answer = sctp_choose_boundspecific_inp(inp, ro, vrf_id,
						       non_asoc_addr_ok,
						       dest_is_priv,
						       dest_is_loop, fam);
	}
	SCTP_IPI_ADDR_RUNLOCK();
	return (answer);
}

static int
sctp_find_cmsg(int c_type, void *data, struct mbuf *control, size_t cpsize)
{
	struct cmsghdr cmh;
	int tlen, at, found;
	struct sctp_sndinfo sndinfo;
	struct sctp_prinfo prinfo;
	struct sctp_authinfo authinfo;

	tlen = SCTP_BUF_LEN(control);
	at = 0;
	found = 0;
	/*
	 * Independent of how many mbufs, find the c_type inside the control
	 * structure and copy out the data.
	 */
	while (at < tlen) {
		if ((tlen - at) < (int)CMSG_ALIGN(sizeof(cmh))) {
			/* There is not enough room for one more. */
			return (found);
		}
		m_copydata(control, at, sizeof(cmh), (caddr_t)&cmh);
		if (cmh.cmsg_len < CMSG_ALIGN(sizeof(struct cmsghdr))) {
			/* We dont't have a complete CMSG header. */
			return (found);
		}
		if (((int)cmh.cmsg_len + at) > tlen) {
			/* We don't have the complete CMSG. */
			return (found);
		}
		if ((cmh.cmsg_level == IPPROTO_SCTP) &&
		    ((c_type == cmh.cmsg_type) ||
		     ((c_type == SCTP_SNDRCV) &&
		      ((cmh.cmsg_type == SCTP_SNDINFO) ||
		       (cmh.cmsg_type == SCTP_PRINFO) ||
		       (cmh.cmsg_type == SCTP_AUTHINFO))))) {
			if (c_type == cmh.cmsg_type) {
				if ((size_t)(cmh.cmsg_len - CMSG_ALIGN(sizeof(struct cmsghdr))) < cpsize) {
					return (found);
				}
				/* It is exactly what we want. Copy it out. */
				m_copydata(control, at + CMSG_ALIGN(sizeof(struct cmsghdr)), cpsize, (caddr_t)data);
				return (1);
			} else {
				struct sctp_sndrcvinfo *sndrcvinfo;

				sndrcvinfo = (struct sctp_sndrcvinfo *)data;
				if (found == 0) {
					if (cpsize < sizeof(struct sctp_sndrcvinfo)) {
						return (found);
					}
					memset(sndrcvinfo, 0, sizeof(struct sctp_sndrcvinfo));
				}
				switch (cmh.cmsg_type) {
				case SCTP_SNDINFO:
					if ((size_t)(cmh.cmsg_len - CMSG_ALIGN(sizeof(struct cmsghdr))) < sizeof(struct sctp_sndinfo)) {
						return (found);
					}					
					m_copydata(control, at + CMSG_ALIGN(sizeof(struct cmsghdr)), sizeof(struct sctp_sndinfo), (caddr_t)&sndinfo);
                			sndrcvinfo->sinfo_stream = sndinfo.snd_sid;
			                sndrcvinfo->sinfo_flags = sndinfo.snd_flags;
			                sndrcvinfo->sinfo_ppid = sndinfo.snd_ppid;
			                sndrcvinfo->sinfo_context = sndinfo.snd_context;
			                sndrcvinfo->sinfo_assoc_id = sndinfo.snd_assoc_id;
 					break;
				case SCTP_PRINFO:
					if ((size_t)(cmh.cmsg_len - CMSG_ALIGN(sizeof(struct cmsghdr))) < sizeof(struct sctp_prinfo)) {
						return (found);
					}					
					m_copydata(control, at + CMSG_ALIGN(sizeof(struct cmsghdr)), sizeof(struct sctp_prinfo), (caddr_t)&prinfo);
					sndrcvinfo->sinfo_timetolive = prinfo.pr_value;
					sndrcvinfo->sinfo_flags |= prinfo.pr_policy;
					break;
				case SCTP_AUTHINFO:
					if ((size_t)(cmh.cmsg_len - CMSG_ALIGN(sizeof(struct cmsghdr))) < sizeof(struct sctp_authinfo)) {
						return (found);
					}					
					m_copydata(control, at + CMSG_ALIGN(sizeof(struct cmsghdr)), sizeof(struct sctp_authinfo), (caddr_t)&authinfo);
					sndrcvinfo->sinfo_keynumber_valid = 1;
					sndrcvinfo->sinfo_keynumber = authinfo.auth_keyid;
					break;
				default:
					return (found);
				}				
				found = 1;
			}
		}
		at += CMSG_ALIGN(cmh.cmsg_len);
	}
	return (found);
}

static int
sctp_process_cmsgs_for_init(struct sctp_tcb *stcb, struct mbuf *control, int *error)
{
	struct cmsghdr cmh;
	int tlen, at;
	struct sctp_initmsg initmsg;
#ifdef INET
	struct sockaddr_in sin;
#endif
#ifdef INET6
	struct sockaddr_in6 sin6;
#endif

	tlen = SCTP_BUF_LEN(control);
	at = 0;
	while (at < tlen) {
		if ((tlen - at) < (int)CMSG_ALIGN(sizeof(cmh))) {
			/* There is not enough room for one more. */
			*error = EINVAL;
			return (1);
		}
		m_copydata(control, at, sizeof(cmh), (caddr_t)&cmh);
		if (cmh.cmsg_len < CMSG_ALIGN(sizeof(struct cmsghdr))) {
			/* We dont't have a complete CMSG header. */
			*error = EINVAL;
			return (1);
		}
		if (((int)cmh.cmsg_len + at) > tlen) {
			/* We don't have the complete CMSG. */
			*error = EINVAL;
			return (1);
		}
		if (cmh.cmsg_level == IPPROTO_SCTP) {
			switch (cmh.cmsg_type) {
			case SCTP_INIT:
				if ((size_t)(cmh.cmsg_len - CMSG_ALIGN(sizeof(struct cmsghdr))) < sizeof(struct sctp_initmsg)) {
					*error = EINVAL;
					return (1);
				}					
				m_copydata(control, at + CMSG_ALIGN(sizeof(struct cmsghdr)), sizeof(struct sctp_initmsg), (caddr_t)&initmsg);
				if (initmsg.sinit_max_attempts)
					stcb->asoc.max_init_times = initmsg.sinit_max_attempts;
				if (initmsg.sinit_num_ostreams)
					stcb->asoc.pre_open_streams = initmsg.sinit_num_ostreams;
				if (initmsg.sinit_max_instreams)
					stcb->asoc.max_inbound_streams = initmsg.sinit_max_instreams;
				if (initmsg.sinit_max_init_timeo)
					stcb->asoc.initial_init_rto_max = initmsg.sinit_max_init_timeo;
				if (stcb->asoc.streamoutcnt < stcb->asoc.pre_open_streams) {
					struct sctp_stream_out *tmp_str;
					unsigned int i;

					/* Default is NOT correct */
					SCTPDBG(SCTP_DEBUG_OUTPUT1, "Ok, default:%d pre_open:%d\n",
						stcb->asoc.streamoutcnt, stcb->asoc.pre_open_streams);
					SCTP_TCB_UNLOCK(stcb);
					SCTP_MALLOC(tmp_str,
					            struct sctp_stream_out *,
					            (stcb->asoc.pre_open_streams * sizeof(struct sctp_stream_out)),
					            SCTP_M_STRMO);
					SCTP_TCB_LOCK(stcb);
					if (tmp_str != NULL) {
						SCTP_FREE(stcb->asoc.strmout, SCTP_M_STRMO);
						stcb->asoc.strmout = tmp_str;
						stcb->asoc.strm_realoutsize = stcb->asoc.streamoutcnt = stcb->asoc.pre_open_streams;
					} else {
						stcb->asoc.pre_open_streams = stcb->asoc.streamoutcnt;
					}
					for (i = 0; i < stcb->asoc.streamoutcnt; i++) {
						stcb->asoc.strmout[i].next_sequence_sent = 0;
						TAILQ_INIT(&stcb->asoc.strmout[i].outqueue);
						stcb->asoc.strmout[i].stream_no = i;
						stcb->asoc.strmout[i].last_msg_incomplete = 0;
						stcb->asoc.ss_functions.sctp_ss_init_stream(&stcb->asoc.strmout[i], NULL);
					}
				}
				break;
#ifdef INET
			case SCTP_DSTADDRV4:
				if ((size_t)(cmh.cmsg_len - CMSG_ALIGN(sizeof(struct cmsghdr))) < sizeof(struct in_addr)) {
					*error = EINVAL;
					return (1);
				}
				memset(&sin, 0, sizeof(struct sockaddr_in));
				sin.sin_family = AF_INET;
#if !defined(__Windows__) && !defined(__Userspace_os_Linux)
				sin.sin_len = sizeof(struct sockaddr_in);
#endif
				sin.sin_port = stcb->rport;
				m_copydata(control, at + CMSG_ALIGN(sizeof(struct cmsghdr)), sizeof(struct in_addr), (caddr_t)&sin.sin_addr);
				if ((sin.sin_addr.s_addr == INADDR_ANY) ||
				    (sin.sin_addr.s_addr == INADDR_BROADCAST) ||
				    IN_MULTICAST(ntohl(sin.sin_addr.s_addr))) {
					*error = EINVAL;
					return (-1);
				}
				if (sctp_add_remote_addr(stcb, (struct sockaddr *)&sin, NULL,
				                         SCTP_DONOT_SETSCOPE, SCTP_ADDR_IS_CONFIRMED)) {
					*error = ENOBUFS;
					return (1);
				}
				break;
#endif
#ifdef INET6
			case SCTP_DSTADDRV6:
				if ((size_t)(cmh.cmsg_len - CMSG_ALIGN(sizeof(struct cmsghdr))) < sizeof(struct in6_addr)) {
					*error = EINVAL;
					return (1);
				}
				memset(&sin6, 0, sizeof(struct sockaddr_in6));
				sin6.sin6_family = AF_INET6;
#if !defined(__Windows__) && !defined(__Userspace_os_Linux)
				sin6.sin6_len = sizeof(struct sockaddr_in6);
#endif
				sin6.sin6_port = stcb->rport;
				m_copydata(control, at + CMSG_ALIGN(sizeof(struct cmsghdr)), sizeof(struct in6_addr), (caddr_t)&sin6.sin6_addr);
				if (IN6_IS_ADDR_UNSPECIFIED(&sin6.sin6_addr) ||
				    IN6_IS_ADDR_MULTICAST(&sin6.sin6_addr)) {
					*error = EINVAL;
					return (-1);
				}
#ifdef INET
				if (IN6_IS_ADDR_V4MAPPED(&sin6.sin6_addr)) {
					in6_sin6_2_sin(&sin, &sin6);
					if ((sin.sin_addr.s_addr == INADDR_ANY) ||
					    (sin.sin_addr.s_addr == INADDR_BROADCAST) ||
					    IN_MULTICAST(ntohl(sin.sin_addr.s_addr))) {
						*error = EINVAL;
						return (-1);
					}
					if (sctp_add_remote_addr(stcb, (struct sockaddr *)&sin, NULL,
					                         SCTP_DONOT_SETSCOPE, SCTP_ADDR_IS_CONFIRMED)) {
						*error = ENOBUFS;
						return (1);
					}
				} else
#endif
					if (sctp_add_remote_addr(stcb, (struct sockaddr *)&sin6, NULL,
					                         SCTP_DONOT_SETSCOPE, SCTP_ADDR_IS_CONFIRMED)) {
						*error = ENOBUFS;
						return (1);
					}
				break;
#endif
			default:
				break;
			}
		}
		at += CMSG_ALIGN(cmh.cmsg_len);
	}
	return (0);
}

static struct sctp_tcb *
sctp_findassociation_cmsgs(struct sctp_inpcb **inp_p,
                           in_port_t port,
                           struct mbuf *control,
                           struct sctp_nets **net_p,
                           int *error)
{
	struct cmsghdr cmh;
	int tlen, at;
	struct sctp_tcb *stcb;
	struct sockaddr *addr;
#ifdef INET
	struct sockaddr_in sin;
#endif
#ifdef INET6
	struct sockaddr_in6 sin6;
#endif

	tlen = SCTP_BUF_LEN(control);
	at = 0;
	while (at < tlen) {
		if ((tlen - at) < (int)CMSG_ALIGN(sizeof(cmh))) {
			/* There is not enough room for one more. */
			*error = EINVAL;
			return (NULL);
		}
		m_copydata(control, at, sizeof(cmh), (caddr_t)&cmh);
		if (cmh.cmsg_len < CMSG_ALIGN(sizeof(struct cmsghdr))) {
			/* We dont't have a complete CMSG header. */
			*error = EINVAL;
			return (NULL);
		}
		if (((int)cmh.cmsg_len + at) > tlen) {
			/* We don't have the complete CMSG. */
			*error = EINVAL;
			return (NULL);
		}
		if (cmh.cmsg_level == IPPROTO_SCTP) {
			switch (cmh.cmsg_type) {
#ifdef INET
			case SCTP_DSTADDRV4:
				if ((size_t)(cmh.cmsg_len - CMSG_ALIGN(sizeof(struct cmsghdr))) < sizeof(struct in_addr)) {
					*error = EINVAL;
					return (NULL);
				}
				memset(&sin, 0, sizeof(struct sockaddr_in));
				sin.sin_family = AF_INET;
#if !defined(__Windows__) && !defined(__Userspace_os_Linux)
				sin.sin_len = sizeof(struct sockaddr_in);
#endif
				sin.sin_port = port;
				m_copydata(control, at + CMSG_ALIGN(sizeof(struct cmsghdr)), sizeof(struct in_addr), (caddr_t)&sin.sin_addr);
				addr = (struct sockaddr *)&sin;
				break;
#endif
#ifdef INET6
			case SCTP_DSTADDRV6:
				if ((size_t)(cmh.cmsg_len - CMSG_ALIGN(sizeof(struct cmsghdr))) < sizeof(struct in6_addr)) {
					*error = EINVAL;
					return (NULL);
				}
				memset(&sin6, 0, sizeof(struct sockaddr_in6));
				sin6.sin6_family = AF_INET6;
#if !defined(__Windows__) && !defined(__Userspace_os_Linux)
				sin6.sin6_len = sizeof(struct sockaddr_in6);
#endif
				sin6.sin6_port = port;
				m_copydata(control, at + CMSG_ALIGN(sizeof(struct cmsghdr)), sizeof(struct in6_addr), (caddr_t)&sin6.sin6_addr);
#ifdef INET
				if (IN6_IS_ADDR_V4MAPPED(&sin6.sin6_addr)) {
					in6_sin6_2_sin(&sin, &sin6);
					addr = (struct sockaddr *)&sin;
				} else
#endif
					addr = (struct sockaddr *)&sin6;
				break;
#endif
			default:
				addr = NULL;
				break;
			}
			if (addr) {
				stcb = sctp_findassociation_ep_addr(inp_p, addr, net_p, NULL, NULL);
				if (stcb != NULL) {
					return (stcb);
				}
			}
		}
		at += CMSG_ALIGN(cmh.cmsg_len);
	}
	return (NULL);
}

static struct mbuf *
sctp_add_cookie(struct sctp_inpcb *inp, struct mbuf *init, int init_offset,
    struct mbuf *initack, int initack_offset, struct sctp_state_cookie *stc_in, uint8_t **signature)
{
	struct mbuf *copy_init, *copy_initack, *m_at, *sig, *mret;
	struct sctp_state_cookie *stc;
	struct sctp_paramhdr *ph;
	uint8_t *foo;
	int sig_offset;
	uint16_t cookie_sz;

	mret = NULL;
	mret = sctp_get_mbuf_for_msg((sizeof(struct sctp_state_cookie) +
				      sizeof(struct sctp_paramhdr)), 0,
				     M_DONTWAIT, 1, MT_DATA);
	if (mret == NULL) {
		return (NULL);
	}
	copy_init = SCTP_M_COPYM(init, init_offset, M_COPYALL, M_DONTWAIT);
	if (copy_init == NULL) {
		sctp_m_freem(mret);
		return (NULL);
	}
#ifdef SCTP_MBUF_LOGGING
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MBUF_LOGGING_ENABLE) {
		struct mbuf *mat;
		mat = copy_init;
		while (mat) {
			if (SCTP_BUF_IS_EXTENDED(mat)) {
				sctp_log_mb(mat, SCTP_MBUF_ICOPY);
			}
			mat = SCTP_BUF_NEXT(mat);
		}
	}
#endif
	copy_initack = SCTP_M_COPYM(initack, initack_offset, M_COPYALL,
	    M_DONTWAIT);
	if (copy_initack == NULL) {
		sctp_m_freem(mret);
		sctp_m_freem(copy_init);
		return (NULL);
	}
#ifdef SCTP_MBUF_LOGGING
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MBUF_LOGGING_ENABLE) {
		struct mbuf *mat;
		mat = copy_initack;
		while (mat) {
			if (SCTP_BUF_IS_EXTENDED(mat)) {
				sctp_log_mb(mat, SCTP_MBUF_ICOPY);
			}
			mat = SCTP_BUF_NEXT(mat);
		}
	}
#endif
	/* easy side we just drop it on the end */
	ph = mtod(mret, struct sctp_paramhdr *);
	SCTP_BUF_LEN(mret) = sizeof(struct sctp_state_cookie) +
	    sizeof(struct sctp_paramhdr);
	stc = (struct sctp_state_cookie *)((caddr_t)ph +
	    sizeof(struct sctp_paramhdr));
	ph->param_type = htons(SCTP_STATE_COOKIE);
	ph->param_length = 0;	/* fill in at the end */
	/* Fill in the stc cookie data */
	memcpy(stc, stc_in, sizeof(struct sctp_state_cookie));

	/* tack the INIT and then the INIT-ACK onto the chain */
	cookie_sz = 0;
	m_at = mret;
	for (m_at = mret; m_at; m_at = SCTP_BUF_NEXT(m_at)) {
		cookie_sz += SCTP_BUF_LEN(m_at);
		if (SCTP_BUF_NEXT(m_at) == NULL) {
			SCTP_BUF_NEXT(m_at) = copy_init;
			break;
		}
	}

	for (m_at = copy_init; m_at; m_at = SCTP_BUF_NEXT(m_at)) {
		cookie_sz += SCTP_BUF_LEN(m_at);
		if (SCTP_BUF_NEXT(m_at) == NULL) {
			SCTP_BUF_NEXT(m_at) = copy_initack;
			break;
		}
	}

	for (m_at = copy_initack; m_at; m_at = SCTP_BUF_NEXT(m_at)) {
		cookie_sz += SCTP_BUF_LEN(m_at);
		if (SCTP_BUF_NEXT(m_at) == NULL) {
			break;
		}
	}
	sig = sctp_get_mbuf_for_msg(SCTP_SECRET_SIZE, 0, M_DONTWAIT, 1, MT_DATA);
	if (sig == NULL) {
		/* no space, so free the entire chain */
		sctp_m_freem(mret);
		return (NULL);
	}
	SCTP_BUF_LEN(sig) = 0;
	SCTP_BUF_NEXT(m_at) = sig;
	sig_offset = 0;
	foo = (uint8_t *) (mtod(sig, caddr_t)+sig_offset);
	memset(foo, 0, SCTP_SIGNATURE_SIZE);
	*signature = foo;
	SCTP_BUF_LEN(sig) += SCTP_SIGNATURE_SIZE;
	cookie_sz += SCTP_SIGNATURE_SIZE;
	ph->param_length = htons(cookie_sz);
	return (mret);
}


static uint8_t
sctp_get_ect(struct sctp_tcb *stcb,
             struct sctp_tmit_chunk *chk)
{
	if ((stcb != NULL) && (stcb->asoc.ecn_allowed == 1)) {
		return (SCTP_ECT0_BIT);
	} else {
		return (0);
	}
}

static void
sctp_handle_no_route(struct sctp_tcb *stcb,
                     struct sctp_nets *net,
                     int so_locked)
{
	SCTPDBG(SCTP_DEBUG_OUTPUT1, "dropped packet - no valid source addr\n");

	if (net) {
		SCTPDBG(SCTP_DEBUG_OUTPUT1, "Destination was ");
		SCTPDBG_ADDR(SCTP_DEBUG_OUTPUT1, &net->ro._l_addr.sa);
		if (net->dest_state & SCTP_ADDR_CONFIRMED) {
			if ((net->dest_state & SCTP_ADDR_REACHABLE) && stcb) {
				SCTPDBG(SCTP_DEBUG_OUTPUT1, "no route takes interface %p down\n", net);
				sctp_ulp_notify(SCTP_NOTIFY_INTERFACE_DOWN,
			                        stcb,
			                        SCTP_FAILED_THRESHOLD,
			                        (void *)net,
			                        so_locked);
				net->dest_state &= ~SCTP_ADDR_REACHABLE;
				net->dest_state &= ~SCTP_ADDR_PF;
			}
		}
		if (stcb) {
			if (net == stcb->asoc.primary_destination) {
				/* need a new primary */
				struct sctp_nets *alt;

				alt = sctp_find_alternate_net(stcb, net, 0);
				if (alt != net) {
					if (stcb->asoc.alternate) {
						sctp_free_remote_addr(stcb->asoc.alternate);
					}
					stcb->asoc.alternate = alt;
					atomic_add_int(&stcb->asoc.alternate->ref_count, 1);
					if (net->ro._s_addr) {
						sctp_free_ifa(net->ro._s_addr);
						net->ro._s_addr = NULL;
					}
					net->src_addr_selected = 0;
				}
			}
		}
	}
}

static int
sctp_lowlevel_chunk_output(struct sctp_inpcb *inp,
    struct sctp_tcb *stcb,	/* may be NULL */
    struct sctp_nets *net,
    struct sockaddr *to,
    struct mbuf *m,
    uint32_t auth_offset,
    struct sctp_auth_chunk *auth,
    uint16_t auth_keyid,
    int nofragment_flag,
    int ecn_ok,
    struct sctp_tmit_chunk *chk,
    int out_of_asoc_ok,
    uint16_t src_port,
    uint16_t dest_port,
    uint32_t v_tag,
    uint16_t port,
    int so_locked,
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
    SCTP_UNUSED
#endif
    union sctp_sockstore *over_addr,
    struct mbuf *init
    )
/* nofragment_flag to tell if IP_DF should be set (IPv4 only) */
{
	/**
	 * Given a mbuf chain (via SCTP_BUF_NEXT()) that holds a packet header
	 * WITH an SCTPHDR but no IP header, endpoint inp and sa structure:
	 * - fill in the HMAC digest of any AUTH chunk in the packet.
	 * - calculate and fill in the SCTP checksum.
	 * - prepend an IP address header.
	 * - if boundall use INADDR_ANY.
	 * - if boundspecific do source address selection.
	 * - set fragmentation option for ipV4.
	 * - On return from IP output, check/adjust mtu size of output
	 *   interface and smallest_mtu size as well.
	 */
	/* Will need ifdefs around this */
#ifdef __Panda__
	pakhandle_type o_pak;
#else
	struct mbuf *o_pak;
#endif
	struct mbuf *newm;
	struct sctphdr *sctphdr;
	int packet_length;
	int ret;
	uint32_t vrf_id;
	sctp_route_t *ro = NULL;
	struct udphdr *udp = NULL;
#if defined (__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
	struct socket *so = NULL;
#endif

#if defined (__APPLE__)
	if (so_locked) {
		sctp_lock_assert(SCTP_INP_SO(inp));
		SCTP_TCB_LOCK_ASSERT(stcb);
	} else {
		sctp_unlock_assert(SCTP_INP_SO(inp));
	}
#endif
	if ((net) && (net->dest_state & SCTP_ADDR_OUT_OF_SCOPE)) {
		SCTP_LTRACE_ERR_RET_PKT(m, inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, EFAULT);
		sctp_m_freem(m);
		return (EFAULT);
	}
	if (stcb) {
		vrf_id = stcb->asoc.vrf_id;
	} else {
		vrf_id = inp->def_vrf_id;
	}

	/* fill in the HMAC digest for any AUTH chunk in the packet */
	if ((auth != NULL) && (stcb != NULL)) {
		sctp_fill_hmac_digest_m(m, auth_offset, auth, stcb, auth_keyid);
	}

	switch (to->sa_family) {
#ifdef INET
	case AF_INET:
	{
		struct ip *ip = NULL;
		sctp_route_t iproute;
		uint8_t tos_value;
		int len;
		
		len = sizeof(struct ip) + sizeof(struct sctphdr);
		if (port) {
			len += sizeof(struct udphdr);
		}
		newm = sctp_get_mbuf_for_msg(len, 1, M_DONTWAIT, 1, MT_DATA);
		if (newm == NULL) {
			sctp_m_freem(m);
			SCTP_LTRACE_ERR_RET(inp, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, ENOMEM);
			return (ENOMEM);
		}
		SCTP_ALIGN_TO_END(newm, len);
		SCTP_BUF_LEN(newm) = len;
		SCTP_BUF_NEXT(newm) = m;
		m = newm;
#if defined(__FreeBSD__)
		if (net != NULL) {
#ifdef INVARIANTS
			if (net->flowidset == 0) {
				panic("Flow ID not set");
			}
#endif
			m->m_pkthdr.flowid = net->flowid;
			m->m_flags |= M_FLOWID;
		} else {
			if ((init != NULL) && (init->m_flags & M_FLOWID)) {
				m->m_pkthdr.flowid = init->m_pkthdr.flowid;
				m->m_flags |= M_FLOWID;
			}
		}
#endif
		packet_length = sctp_calculate_len(m);
		ip = mtod(m, struct ip *);
		ip->ip_v = IPVERSION;
		ip->ip_hl = (sizeof(struct ip) >> 2);
		if (net) {
			tos_value = net->dscp;
		} else {
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Panda__) || defined(__Windows__) || defined(__Userspace__)
			tos_value = inp->ip_inp.inp.inp_ip_tos;
#else
			tos_value = inp->inp_ip_tos;
#endif
		}
                if ((nofragment_flag) && (port == 0)) {
#if defined(__FreeBSD__)
			ip->ip_off = IP_DF;
#elif defined(WITH_CONVERT_IP_OFF) || defined(__APPLE__) || defined(__Userspace__)
			ip->ip_off = IP_DF;
#else
			ip->ip_off = htons(IP_DF);
#endif
		} else
			ip->ip_off = 0;

#if defined(__FreeBSD__)
		/* FreeBSD has a function for ip_id's */
		ip->ip_id = ip_newid();
#elif defined(RANDOM_IP_ID)
		/* Apple has RANDOM_IP_ID switch */
		ip->ip_id = htons(ip_randomid());
#elif defined(__Userspace__)
                ip->ip_id = htons(SCTP_IP_ID(inp)++);
#else
		ip->ip_id = SCTP_IP_ID(inp)++;
#endif

#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Panda__) || defined(__Windows__) || defined(__Userspace__)
		ip->ip_ttl = inp->ip_inp.inp.inp_ip_ttl;
#else
		ip->ip_ttl = inp->inp_ip_ttl;
#endif
		ip->ip_len = packet_length;
		ip->ip_tos = tos_value & 0xfc;
		if (ecn_ok) {
			ip->ip_tos |= sctp_get_ect(stcb, chk);
		}
		if (port) {
			ip->ip_p = IPPROTO_UDP;
		} else {
			ip->ip_p = IPPROTO_SCTP;
		}
		ip->ip_sum = 0;
		if (net == NULL) {
			ro = &iproute;
			memset(&iproute, 0, sizeof(iproute));
#if !(defined(__Windows__) ||  defined(__Userspace_os_Linux)) /*__Userspace__ */
			memcpy(&ro->ro_dst, to, to->sa_len);
#else
			memcpy(&ro->ro_dst, to, sizeof(struct sockaddr_in));
#endif
		} else {
			ro = (sctp_route_t *)&net->ro;
		}
		/* Now the address selection part */
		ip->ip_dst.s_addr = ((struct sockaddr_in *)to)->sin_addr.s_addr;

		/* call the routine to select the src address */
		if (net && out_of_asoc_ok == 0) {
			if (net->ro._s_addr && (net->ro._s_addr->localifa_flags & (SCTP_BEING_DELETED|SCTP_ADDR_IFA_UNUSEABLE))) {
				sctp_free_ifa(net->ro._s_addr);
				net->ro._s_addr = NULL;
				net->src_addr_selected = 0;
				if (ro->ro_rt) {
					RTFREE(ro->ro_rt);
					ro->ro_rt = NULL;
				}
			}
			if (net->src_addr_selected == 0) {
				/* Cache the source address */
				net->ro._s_addr = sctp_source_address_selection(inp,stcb,
										ro, net, 0,
										vrf_id);
				net->src_addr_selected = 1;
			}
			if (net->ro._s_addr == NULL) {
				/* No route to host */
				net->src_addr_selected = 0;
				sctp_handle_no_route(stcb, net, so_locked);
				SCTP_LTRACE_ERR_RET_PKT(m, inp, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, EHOSTUNREACH);
				sctp_m_freem(m);
				return (EHOSTUNREACH);
			}
			ip->ip_src = net->ro._s_addr->address.sin.sin_addr;
		} else {
			if (over_addr == NULL) {
				struct sctp_ifa *_lsrc;

				_lsrc = sctp_source_address_selection(inp, stcb, ro,
				                                      net,
				                                      out_of_asoc_ok,
				                                      vrf_id);
				if (_lsrc == NULL) {
					sctp_handle_no_route(stcb, net, so_locked);
					SCTP_LTRACE_ERR_RET_PKT(m, inp, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, EHOSTUNREACH);
					sctp_m_freem(m);
					return (EHOSTUNREACH);
				}
				ip->ip_src = _lsrc->address.sin.sin_addr;
				sctp_free_ifa(_lsrc);
			} else {
				ip->ip_src = over_addr->sin.sin_addr;
				SCTP_RTALLOC(ro, vrf_id);
			}
		}
		if (port) {
			udp = (struct udphdr *)((caddr_t)ip + sizeof(struct ip));
			udp->uh_sport = htons(SCTP_BASE_SYSCTL(sctp_udp_tunneling_port));
			udp->uh_dport = port;
			udp->uh_ulen = htons(packet_length - sizeof(struct ip));	
#if !defined(__Windows__) && !defined(__Userspace__)
			udp->uh_sum = in_pseudo(ip->ip_src.s_addr, ip->ip_dst.s_addr, udp->uh_ulen + htons(IPPROTO_UDP));
#else
			udp->uh_sum = 0;
#endif
			sctphdr = (struct sctphdr *)((caddr_t)udp + sizeof(struct udphdr));
		} else {
			sctphdr = (struct sctphdr *)((caddr_t)ip + sizeof(struct ip));
		}

		sctphdr->src_port = src_port;
		sctphdr->dest_port = dest_port;
		sctphdr->v_tag = v_tag;
		sctphdr->checksum = 0;

		/*
		 * If source address selection fails and we find no route
		 * then the ip_output should fail as well with a
		 * NO_ROUTE_TO_HOST type error. We probably should catch
		 * that somewhere and abort the association right away
		 * (assuming this is an INIT being sent).
		 */
		if (ro->ro_rt == NULL) {
			/*
			 * src addr selection failed to find a route (or
			 * valid source addr), so we can't get there from
			 * here (yet)!
			 */
			sctp_handle_no_route(stcb, net, so_locked);
			SCTP_LTRACE_ERR_RET_PKT(m, inp, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, EHOSTUNREACH);
			sctp_m_freem(m);
			return (EHOSTUNREACH);
		}
		if (ro != &iproute) {
			memcpy(&iproute, ro, sizeof(*ro));
		}
		SCTPDBG(SCTP_DEBUG_OUTPUT3, "Calling ipv4 output routine from low level src addr:%x\n",
			(uint32_t) (ntohl(ip->ip_src.s_addr)));
		SCTPDBG(SCTP_DEBUG_OUTPUT3, "Destination is %x\n",
			(uint32_t)(ntohl(ip->ip_dst.s_addr)));
		SCTPDBG(SCTP_DEBUG_OUTPUT3, "RTP route is %p through\n",
			ro->ro_rt);

		if (SCTP_GET_HEADER_FOR_OUTPUT(o_pak)) {
			/* failed to prepend data, give up */
			SCTP_LTRACE_ERR_RET_PKT(m, inp, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, ENOMEM);
			sctp_m_freem(m);
			return (ENOMEM);
		}
#ifdef  SCTP_PACKET_LOGGING
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LAST_PACKET_TRACING)
			sctp_packet_log(m, packet_length);
#endif
		SCTP_ATTACH_CHAIN(o_pak, m, packet_length);
		if (port) {
#if defined(SCTP_WITH_NO_CSUM)
			SCTP_STAT_INCR(sctps_sendnocrc);
#else
			if (!(SCTP_BASE_SYSCTL(sctp_no_csum_on_loopback) &&
			     (stcb) &&
			     (stcb->asoc.loopback_scope))) {
				sctphdr->checksum = sctp_calculate_cksum(m, sizeof(struct ip) + sizeof(struct udphdr));
				SCTP_STAT_INCR(sctps_sendswcrc);
			} else {
				SCTP_STAT_INCR(sctps_sendnocrc);
			}
#endif
			SCTP_ENABLE_UDP_CSUM(o_pak);
		} else {
#if defined(SCTP_WITH_NO_CSUM)
			SCTP_STAT_INCR(sctps_sendnocrc);
#else
#if defined(__FreeBSD__) && __FreeBSD_version >= 800000
			m->m_pkthdr.csum_flags = CSUM_SCTP;
			m->m_pkthdr.csum_data = 0;
			SCTP_STAT_INCR(sctps_sendhwcrc);
#else
			if (!(SCTP_BASE_SYSCTL(sctp_no_csum_on_loopback) &&
			     (stcb) &&
			     (stcb->asoc.loopback_scope))) {
				sctphdr->checksum = sctp_calculate_cksum(m, sizeof(struct ip));
				SCTP_STAT_INCR(sctps_sendswcrc);
			} else {
				SCTP_STAT_INCR(sctps_sendnocrc);
			}
#endif
#endif
		}
		/* send it out.  table id is taken from stcb */
#if defined (__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		if ((SCTP_BASE_SYSCTL(sctp_output_unlocked)) && (so_locked)) {
			so = SCTP_INP_SO(inp);
			SCTP_SOCKET_UNLOCK(so, 0);
		}
#endif		
		SCTP_IP_OUTPUT(ret, o_pak, ro, stcb, vrf_id);
#if defined (__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		if ((SCTP_BASE_SYSCTL(sctp_output_unlocked)) && (so_locked)) {
			atomic_add_int(&stcb->asoc.refcnt, 1);
			SCTP_TCB_UNLOCK(stcb);
			SCTP_SOCKET_LOCK(so, 0);
			SCTP_TCB_LOCK(stcb);
			atomic_subtract_int(&stcb->asoc.refcnt, 1);
		}
#endif		
		SCTP_STAT_INCR(sctps_sendpackets);
		SCTP_STAT_INCR_COUNTER64(sctps_outpackets);
		if (ret)
			SCTP_STAT_INCR(sctps_senderrors);

		SCTPDBG(SCTP_DEBUG_OUTPUT3, "IP output returns %d\n", ret);
		if (net == NULL) {
			/* free tempy routes */
			if (ro->ro_rt) {
				RTFREE(ro->ro_rt);
				ro->ro_rt = NULL;
			}
		} else {
			/* PMTU check versus smallest asoc MTU goes here */
			if ((ro->ro_rt != NULL) &&
			    (net->ro._s_addr)) {
				uint32_t mtu;
				mtu = SCTP_GATHER_MTU_FROM_ROUTE(net->ro._s_addr, &net->ro._l_addr.sa, ro->ro_rt);
				if (net->port) {
					mtu -= sizeof(struct udphdr);
				}
				if (mtu && (stcb->asoc.smallest_mtu > mtu)) {
					sctp_mtu_size_reset(inp, &stcb->asoc, mtu);
					net->mtu = mtu;
				}
			} else if (ro->ro_rt == NULL) {
				/* route was freed */
				if (net->ro._s_addr &&
				    net->src_addr_selected) {
					sctp_free_ifa(net->ro._s_addr);
					net->ro._s_addr = NULL;
				}
				net->src_addr_selected = 0;
			}
		}
		return (ret);
	}
#endif
#ifdef INET6
	case AF_INET6:
	{
		uint32_t flowlabel;
		struct ip6_hdr *ip6h;
#if defined(__Userspace__)
		sctp_route_t ip6route;
#else
		struct route_in6 ip6route;
#endif
#if defined(__Panda__) || defined(__Userspace__)
		void *ifp;
#else
		struct ifnet *ifp;
#endif
		u_char flowTop;
		uint16_t flowBottom;
		u_char tosBottom, tosTop;
		struct sockaddr_in6 *sin6, tmp, *lsa6, lsa6_tmp;
		int prev_scope = 0;
#ifdef SCTP_EMBEDDED_V6_SCOPE
		struct sockaddr_in6 lsa6_storage;
		int error;
#endif
		u_short prev_port = 0;
		int len;

		if (net != NULL) {
			flowlabel = net->flowlabel;
		} else {
			flowlabel = ((struct in6pcb *)inp)->in6p_flowinfo;
		}

		len = sizeof(struct ip6_hdr) + sizeof(struct sctphdr);
		if (port) {
			len += sizeof(struct udphdr);
		}
		newm = sctp_get_mbuf_for_msg(len, 1, M_DONTWAIT, 1, MT_DATA);
		if (newm == NULL) {
			sctp_m_freem(m);
			SCTP_LTRACE_ERR_RET(inp, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, ENOMEM);
			return (ENOMEM);
		}
		SCTP_ALIGN_TO_END(newm, len);
		SCTP_BUF_LEN(newm) = len;
		SCTP_BUF_NEXT(newm) = m;
		m = newm;
#if defined(__FreeBSD__)
		if (net != NULL) {
#ifdef INVARIANTS
			if (net->flowidset == 0) {
				panic("Flow ID not set");
			}
#endif
			m->m_pkthdr.flowid = net->flowid;
			m->m_flags |= M_FLOWID;
		} else {
			if ((init != NULL) && (init->m_flags & M_FLOWID)) {
				m->m_pkthdr.flowid = init->m_pkthdr.flowid;
				m->m_flags |= M_FLOWID;
			}
		}
#endif
		packet_length = sctp_calculate_len(m);

		ip6h = mtod(m, struct ip6_hdr *);
		/*
		 * We assume here that inp_flow is in host byte order within
		 * the TCB!
		 */
		flowBottom = flowlabel & 0x0000ffff;
		flowTop = ((flowlabel & 0x000f0000) >> 16);
		tosTop = (((flowlabel & 0xf0) >> 4) | IPV6_VERSION);
		/* protect *sin6 from overwrite */
		sin6 = (struct sockaddr_in6 *)to;
		tmp = *sin6;
		sin6 = &tmp;

#ifdef SCTP_EMBEDDED_V6_SCOPE
		/* KAME hack: embed scopeid */
#if defined(SCTP_BASE_FREEBSD) || defined(__APPLE__)
#if defined(APPLE_LION)
		if (in6_embedscope(&sin6->sin6_addr, sin6, NULL, NULL, NULL) != 0)
#else
		if (in6_embedscope(&sin6->sin6_addr, sin6, NULL, NULL) != 0)
#endif
#elif defined(SCTP_KAME)
		if (sa6_embedscope(sin6, MODULE_GLOBAL(ip6_use_defzone)) != 0)
#else
		if (in6_embedscope(&sin6->sin6_addr, sin6) != 0)
#endif
		{
			SCTP_LTRACE_ERR_RET_PKT(m, inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, EINVAL);
			return (EINVAL);
		}
#endif /* SCTP_EMBEDDED_V6_SCOPE */
		if (net == NULL) {
			memset(&ip6route, 0, sizeof(ip6route));
			ro = (sctp_route_t *)&ip6route;
#if !(defined(__Windows__) ||  defined(__Userspace_os_Linux)) /*__Userspace__ */
			memcpy(&ro->ro_dst, sin6, sin6->sin6_len);
#else
			memcpy(&ro->ro_dst, sin6, sizeof(struct sockaddr_in6));
#endif
		} else {
			ro = (sctp_route_t *)&net->ro;
		}
		tosBottom = (((struct in6pcb *)inp)->in6p_flowinfo & 0x0c);
		if (ecn_ok) {
			tosBottom |= sctp_get_ect(stcb, chk);
		}
		tosBottom <<= 4;
		ip6h->ip6_flow = htonl(((tosTop << 24) | ((tosBottom | flowTop) << 16) | flowBottom));
		if (port) {
			ip6h->ip6_nxt = IPPROTO_UDP;
		} else {
			ip6h->ip6_nxt = IPPROTO_SCTP;
		}
		ip6h->ip6_plen = (packet_length - sizeof(struct ip6_hdr));
		ip6h->ip6_dst = sin6->sin6_addr;

		/*
		 * Add SRC address selection here: we can only reuse to a
		 * limited degree the kame src-addr-sel, since we can try
		 * their selection but it may not be bound.
		 */
		bzero(&lsa6_tmp, sizeof(lsa6_tmp));
		lsa6_tmp.sin6_family = AF_INET6;
#if !(defined(__Windows__) ||  defined(__Userspace_os_Linux)) /*__Userspace__ */
		lsa6_tmp.sin6_len = sizeof(lsa6_tmp);
#endif
		lsa6 = &lsa6_tmp;
		if (net && out_of_asoc_ok == 0) {
			if (net->ro._s_addr && (net->ro._s_addr->localifa_flags & (SCTP_BEING_DELETED|SCTP_ADDR_IFA_UNUSEABLE))) {
				sctp_free_ifa(net->ro._s_addr);
				net->ro._s_addr = NULL;
				net->src_addr_selected = 0;
				if (ro->ro_rt) {
					RTFREE(ro->ro_rt);
					ro->ro_rt = NULL;
				}
			}
			if (net->src_addr_selected == 0) {
#ifdef SCTP_EMBEDDED_V6_SCOPE
				sin6 = (struct sockaddr_in6 *)&net->ro._l_addr;
				/* KAME hack: embed scopeid */
#if defined(SCTP_BASE_FREEBSD) || defined(__APPLE__)
#if defined(APPLE_LION)
				if (in6_embedscope(&sin6->sin6_addr, sin6, NULL, NULL, NULL) != 0)
#else
				if (in6_embedscope(&sin6->sin6_addr, sin6, NULL, NULL) != 0)
#endif
#elif defined(SCTP_KAME)
				if (sa6_embedscope(sin6, MODULE_GLOBAL(ip6_use_defzone)) != 0)
#else
				if (in6_embedscope(&sin6->sin6_addr, sin6) != 0)
#endif
				{
					SCTP_LTRACE_ERR_RET_PKT(m, inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, EINVAL);
					return (EINVAL);
				}
#endif /* SCTP_EMBEDDED_V6_SCOPE */
				/* Cache the source address */
				net->ro._s_addr = sctp_source_address_selection(inp,
										stcb,
										ro,
										net,
										0,
										vrf_id);
#ifdef SCTP_EMBEDDED_V6_SCOPE
#ifdef SCTP_KAME
				(void)sa6_recoverscope(sin6);
#else
				(void)in6_recoverscope(sin6, &sin6->sin6_addr, NULL);
#endif	/* SCTP_KAME */
#endif	/* SCTP_EMBEDDED_V6_SCOPE */
				net->src_addr_selected = 1;
			}
			if (net->ro._s_addr == NULL) {
				SCTPDBG(SCTP_DEBUG_OUTPUT3, "V6:No route to host\n");
				net->src_addr_selected = 0;
				sctp_handle_no_route(stcb, net, so_locked);
				SCTP_LTRACE_ERR_RET_PKT(m, inp, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, EHOSTUNREACH);
				sctp_m_freem(m);
				return (EHOSTUNREACH);
			}
			lsa6->sin6_addr = net->ro._s_addr->address.sin6.sin6_addr;
		} else {
#ifdef SCTP_EMBEDDED_V6_SCOPE
			sin6 = (struct sockaddr_in6 *)&ro->ro_dst;
			/* KAME hack: embed scopeid */
#if defined(SCTP_BASE_FREEBSD) || defined(__APPLE__)
#if defined(APPLE_LION)
			if (in6_embedscope(&sin6->sin6_addr, sin6, NULL, NULL, NULL) != 0)
#else
			if (in6_embedscope(&sin6->sin6_addr, sin6, NULL, NULL) != 0)
#endif
#elif defined(SCTP_KAME)
			if (sa6_embedscope(sin6, MODULE_GLOBAL(ip6_use_defzone)) != 0)
#else
			if (in6_embedscope(&sin6->sin6_addr, sin6) != 0)
#endif
			  {
				SCTP_LTRACE_ERR_RET_PKT(m, inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, EINVAL);
				return (EINVAL);
			  }
#endif /* SCTP_EMBEDDED_V6_SCOPE */
			if (over_addr == NULL) {
				struct sctp_ifa *_lsrc;

				_lsrc = sctp_source_address_selection(inp, stcb, ro,
				                                      net,
				                                      out_of_asoc_ok,
				                                      vrf_id);
				if (_lsrc == NULL) {
					sctp_handle_no_route(stcb, net, so_locked);
					SCTP_LTRACE_ERR_RET_PKT(m, inp, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, EHOSTUNREACH);
					sctp_m_freem(m);
					return (EHOSTUNREACH);
				}
				lsa6->sin6_addr = _lsrc->address.sin6.sin6_addr;
				sctp_free_ifa(_lsrc);
			} else {
				lsa6->sin6_addr = over_addr->sin6.sin6_addr;
				SCTP_RTALLOC(ro, vrf_id);
			}
#ifdef SCTP_EMBEDDED_V6_SCOPE
#ifdef SCTP_KAME
			(void)sa6_recoverscope(sin6);
#else
			(void)in6_recoverscope(sin6, &sin6->sin6_addr, NULL);
#endif	/* SCTP_KAME */
#endif	/* SCTP_EMBEDDED_V6_SCOPE */
		}
		lsa6->sin6_port = inp->sctp_lport;

		if (ro->ro_rt == NULL) {
			/*
			 * src addr selection failed to find a route (or
			 * valid source addr), so we can't get there from
			 * here!
			 */
			sctp_handle_no_route(stcb, net, so_locked);
			SCTP_LTRACE_ERR_RET_PKT(m, inp, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, EHOSTUNREACH);
			sctp_m_freem(m);
			return (EHOSTUNREACH);
		}
#ifndef SCOPEDROUTING
#ifdef SCTP_EMBEDDED_V6_SCOPE
		/*
		 * XXX: sa6 may not have a valid sin6_scope_id in the
		 * non-SCOPEDROUTING case.
		 */
		bzero(&lsa6_storage, sizeof(lsa6_storage));
		lsa6_storage.sin6_family = AF_INET6;
#if !defined(__Windows__)
		lsa6_storage.sin6_len = sizeof(lsa6_storage);
#endif
#ifdef SCTP_KAME
		lsa6_storage.sin6_addr = lsa6->sin6_addr;
		if ((error = sa6_recoverscope(&lsa6_storage)) != 0) {
#else
		if ((error = in6_recoverscope(&lsa6_storage, &lsa6->sin6_addr,
		    NULL)) != 0) {
#endif				/* SCTP_KAME */
			SCTPDBG(SCTP_DEBUG_OUTPUT3, "recover scope fails error %d\n", error);
			sctp_m_freem(m);
			return (error);
		}
		/* XXX */
		lsa6_storage.sin6_addr = lsa6->sin6_addr;
		lsa6_storage.sin6_port = inp->sctp_lport;
		lsa6 = &lsa6_storage;
#endif /* SCTP_EMBEDDED_V6_SCOPE */
#endif /* SCOPEDROUTING */
		ip6h->ip6_src = lsa6->sin6_addr;

		if (port) {
			udp = (struct udphdr *)((caddr_t)ip6h + sizeof(struct ip6_hdr));
			udp->uh_sport = htons(SCTP_BASE_SYSCTL(sctp_udp_tunneling_port));
			udp->uh_dport = port;
			udp->uh_ulen = htons(packet_length - sizeof(struct ip6_hdr));
			udp->uh_sum = 0;
			sctphdr = (struct sctphdr *)((caddr_t)udp + sizeof(struct udphdr));
		} else {
			sctphdr = (struct sctphdr *)((caddr_t)ip6h + sizeof(struct ip6_hdr));
		}

		sctphdr->src_port = src_port;
		sctphdr->dest_port = dest_port;
		sctphdr->v_tag = v_tag;
		sctphdr->checksum = 0;

		/*
		 * We set the hop limit now since there is a good chance
		 * that our ro pointer is now filled
		 */
		ip6h->ip6_hlim = SCTP_GET_HLIM(inp, ro);
		ifp = SCTP_GET_IFN_VOID_FROM_ROUTE(ro);

#ifdef SCTP_DEBUG
		/* Copy to be sure something bad is not happening */
		sin6->sin6_addr = ip6h->ip6_dst;
		lsa6->sin6_addr = ip6h->ip6_src;
#endif

		SCTPDBG(SCTP_DEBUG_OUTPUT3, "Calling ipv6 output routine from low level\n");
		SCTPDBG(SCTP_DEBUG_OUTPUT3, "src: ");
		SCTPDBG_ADDR(SCTP_DEBUG_OUTPUT3, (struct sockaddr *)lsa6);
		SCTPDBG(SCTP_DEBUG_OUTPUT3, "dst: ");
		SCTPDBG_ADDR(SCTP_DEBUG_OUTPUT3, (struct sockaddr *)sin6);
		if (net) {
			sin6 = (struct sockaddr_in6 *)&net->ro._l_addr;
			/* preserve the port and scope for link local send */
			prev_scope = sin6->sin6_scope_id;
			prev_port = sin6->sin6_port;
		}

		if (SCTP_GET_HEADER_FOR_OUTPUT(o_pak)) {
			/* failed to prepend data, give up */
			sctp_m_freem(m);
			SCTP_LTRACE_ERR_RET(inp, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, ENOMEM);
			return (ENOMEM);
		}
#ifdef  SCTP_PACKET_LOGGING
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LAST_PACKET_TRACING)
			sctp_packet_log(m, packet_length);
#endif
		SCTP_ATTACH_CHAIN(o_pak, m, packet_length);
		if (port) {
#if defined(SCTP_WITH_NO_CSUM)
			SCTP_STAT_INCR(sctps_sendnocrc);
#else
			if (!(SCTP_BASE_SYSCTL(sctp_no_csum_on_loopback) &&
			     (stcb) &&
			     (stcb->asoc.loopback_scope))) {
				sctphdr->checksum = sctp_calculate_cksum(m, sizeof(struct ip6_hdr) + sizeof(struct udphdr));
				SCTP_STAT_INCR(sctps_sendswcrc);
			} else {
				SCTP_STAT_INCR(sctps_sendnocrc);
			}
#endif
#if defined(__Windows__)
			udp->uh_sum = 0;
#elif !defined(__Userspace__)
			if ((udp->uh_sum = in6_cksum(o_pak, IPPROTO_UDP, sizeof(struct ip6_hdr), packet_length - sizeof(struct ip6_hdr))) == 0) {
				udp->uh_sum = 0xffff;
			}
#endif
		} else {
#if defined(SCTP_WITH_NO_CSUM)
			SCTP_STAT_INCR(sctps_sendnocrc);
#else
#if defined(__FreeBSD__) && __FreeBSD_version >= 800000
			m->m_pkthdr.csum_flags = CSUM_SCTP;
			m->m_pkthdr.csum_data = 0;
			SCTP_STAT_INCR(sctps_sendhwcrc);
#else
			if (!(SCTP_BASE_SYSCTL(sctp_no_csum_on_loopback) &&
			     (stcb) &&
			     (stcb->asoc.loopback_scope))) {
				sctphdr->checksum = sctp_calculate_cksum(m, sizeof(struct ip6_hdr));
				SCTP_STAT_INCR(sctps_sendswcrc);
			} else {
				SCTP_STAT_INCR(sctps_sendnocrc);
			}
#endif
#endif
		}
		/* send it out. table id is taken from stcb */
#if defined (__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		if ((SCTP_BASE_SYSCTL(sctp_output_unlocked)) && (so_locked)) {
			so = SCTP_INP_SO(inp);
			SCTP_SOCKET_UNLOCK(so, 0);
		}
#endif		
		SCTP_IP6_OUTPUT(ret, o_pak, (struct route_in6 *)ro, &ifp, stcb, vrf_id);
#if defined (__APPLE__) || defined(SCTP_SO_LOCK_TESTING)
		if ((SCTP_BASE_SYSCTL(sctp_output_unlocked)) && (so_locked)) {
			atomic_add_int(&stcb->asoc.refcnt, 1);
			SCTP_TCB_UNLOCK(stcb);
			SCTP_SOCKET_LOCK(so, 0);
			SCTP_TCB_LOCK(stcb);
			atomic_subtract_int(&stcb->asoc.refcnt, 1);
		}
#endif		
		if (net) {
			/* for link local this must be done */
			sin6->sin6_scope_id = prev_scope;
			sin6->sin6_port = prev_port;
		}
		SCTPDBG(SCTP_DEBUG_OUTPUT3, "return from send is %d\n", ret);
		SCTP_STAT_INCR(sctps_sendpackets);
		SCTP_STAT_INCR_COUNTER64(sctps_outpackets);
		if (ret) {
			SCTP_STAT_INCR(sctps_senderrors);
		}
		if (net == NULL) {
			/* Now if we had a temp route free it */
			if (ro->ro_rt) {
				RTFREE(ro->ro_rt);
			}
		} else {
			/* PMTU check versus smallest asoc MTU goes here */
			if (ro->ro_rt == NULL) {
				/* Route was freed */
				if (net->ro._s_addr &&
				    net->src_addr_selected) {
					sctp_free_ifa(net->ro._s_addr);
					net->ro._s_addr = NULL;
				}
				net->src_addr_selected = 0;
			}
			if ((ro->ro_rt != NULL) &&
			    (net->ro._s_addr)) {
				uint32_t mtu;
				mtu = SCTP_GATHER_MTU_FROM_ROUTE(net->ro._s_addr, &net->ro._l_addr.sa, ro->ro_rt);
				if (mtu &&
				    (stcb->asoc.smallest_mtu > mtu)) {
					sctp_mtu_size_reset(inp, &stcb->asoc, mtu);
					net->mtu = mtu;
					if (net->port) {
						net->mtu -= sizeof(struct udphdr);
					}
				}
			}
#if !defined(__Panda__) && !defined(__Userspace__)
			else if (ifp) {
#if defined(__APPLE__)
#if !defined(APPLE_LEOPARD) && !defined(APPLE_SNOWLEOPARD) && !defined(APPLE_LION)
#define ND_IFINFO(ifp) (&nd_ifinfo[ifp->if_index])
#endif
#elif defined(__Windows__)
#define ND_IFINFO(ifp)	(ifp)
#define linkmtu		if_mtu
#endif				/* SCTP_BASE_FREEBSD */
				if (ND_IFINFO(ifp)->linkmtu &&
				    (stcb->asoc.smallest_mtu > ND_IFINFO(ifp)->linkmtu)) {
					sctp_mtu_size_reset(inp,
					    &stcb->asoc,
					    ND_IFINFO(ifp)->linkmtu);
				}
			}
#endif
		}
		return (ret);
	}
#endif
	default:
		SCTPDBG(SCTP_DEBUG_OUTPUT1, "Unknown protocol (TSNH) type %d\n",
		        ((struct sockaddr *)to)->sa_family);
		sctp_m_freem(m);
		SCTP_LTRACE_ERR_RET_PKT(m, inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, EFAULT);
		return (EFAULT);
	}
}


void
sctp_send_initiate(struct sctp_inpcb *inp, struct sctp_tcb *stcb, int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
    SCTP_UNUSED
#endif
    )
{
	struct mbuf *m, *m_at, *mp_last;
	struct sctp_nets *net;
	struct sctp_init_chunk *init;
	struct sctp_supported_addr_param *sup_addr;
	struct sctp_adaptation_layer_indication *ali;
	struct sctp_ecn_supported_param *ecn;
	struct sctp_prsctp_supported_param *prsctp;
	struct sctp_supported_chunk_types_param *pr_supported;
	int cnt_inits_to = 0;
	int padval, ret;
	int num_ext;
	int p_len;

#if defined(__APPLE__)
	if (so_locked) {
		sctp_lock_assert(SCTP_INP_SO(inp));
	} else {
		sctp_unlock_assert(SCTP_INP_SO(inp));
	}
#endif
	/* INIT's always go to the primary (and usually ONLY address) */
	mp_last = NULL;
	net = stcb->asoc.primary_destination;
	if (net == NULL) {
		net = TAILQ_FIRST(&stcb->asoc.nets);
		if (net == NULL) {
			/* TSNH */
			return;
		}
		/* we confirm any address we send an INIT to */
		net->dest_state &= ~SCTP_ADDR_UNCONFIRMED;
		(void)sctp_set_primary_addr(stcb, NULL, net);
	} else {
		/* we confirm any address we send an INIT to */
		net->dest_state &= ~SCTP_ADDR_UNCONFIRMED;
	}
	SCTPDBG(SCTP_DEBUG_OUTPUT4, "Sending INIT\n");
#ifdef INET6
	if (((struct sockaddr *)&(net->ro._l_addr))->sa_family == AF_INET6) {
		/*
		 * special hook, if we are sending to link local it will not
		 * show up in our private address count.
		 */
		struct sockaddr_in6 *sin6l;

		sin6l = &net->ro._l_addr.sin6;
		if (IN6_IS_ADDR_LINKLOCAL(&sin6l->sin6_addr))
			cnt_inits_to = 1;
	}
#endif
	if (SCTP_OS_TIMER_PENDING(&net->rxt_timer.timer)) {
		/* This case should not happen */
		SCTPDBG(SCTP_DEBUG_OUTPUT4, "Sending INIT - failed timer?\n");
		return;
	}
	/* start the INIT timer */
	sctp_timer_start(SCTP_TIMER_TYPE_INIT, inp, stcb, net);

	m = sctp_get_mbuf_for_msg(MCLBYTES, 1, M_DONTWAIT, 1, MT_DATA);
	if (m == NULL) {
		/* No memory, INIT timer will re-attempt. */
		SCTPDBG(SCTP_DEBUG_OUTPUT4, "Sending INIT - mbuf?\n");
		return;
	}
	SCTP_BUF_LEN(m) = sizeof(struct sctp_init_chunk);
	/*
	 * assume peer supports asconf in order to be able to queue
	 * local address changes while an INIT is in flight and before
	 * the assoc is established.
	 */
	stcb->asoc.peer_supports_asconf = 1;
	/* Now lets put the SCTP header in place */
	init = mtod(m, struct sctp_init_chunk *);
	/* now the chunk header */
	init->ch.chunk_type = SCTP_INITIATION;
	init->ch.chunk_flags = 0;
	/* fill in later from mbuf we build */
	init->ch.chunk_length = 0;
	/* place in my tag */
	init->init.initiate_tag = htonl(stcb->asoc.my_vtag);
	/* set up some of the credits. */
	init->init.a_rwnd = htonl(max(inp->sctp_socket?SCTP_SB_LIMIT_RCV(inp->sctp_socket):0,
	                              SCTP_MINIMAL_RWND));

	init->init.num_outbound_streams = htons(stcb->asoc.pre_open_streams);
	init->init.num_inbound_streams = htons(stcb->asoc.max_inbound_streams);
	init->init.initial_tsn = htonl(stcb->asoc.init_seq_number);
	/* now the address restriction */
	/* XXX Should we take the address family of the socket into account? */
	sup_addr = (struct sctp_supported_addr_param *)((caddr_t)init +
	    sizeof(*init));
	sup_addr->ph.param_type = htons(SCTP_SUPPORTED_ADDRTYPE);
#ifdef INET6
#ifdef INET
	/* we support 2 types: IPv4/IPv6 */
	sup_addr->ph.param_length = htons(sizeof(*sup_addr) + sizeof(uint16_t));
	sup_addr->addr_type[0] = htons(SCTP_IPV4_ADDRESS);
	sup_addr->addr_type[1] = htons(SCTP_IPV6_ADDRESS);
#else
	/* we support 1 type: IPv6 */
	sup_addr->ph.param_length = htons(sizeof(*sup_addr) + sizeof(uint8_t));
	sup_addr->addr_type[0] = htons(SCTP_IPV6_ADDRESS);
	sup_addr->addr_type[1] = htons(0); /* this is the padding */
#endif
#else
	/* we support 1 type: IPv4 */
	sup_addr->ph.param_length = htons(sizeof(*sup_addr) + sizeof(uint8_t));
	sup_addr->addr_type[0] = htons(SCTP_IPV4_ADDRESS);
	sup_addr->addr_type[1] = htons(0); /* this is the padding */
#endif
	SCTP_BUF_LEN(m) += sizeof(*sup_addr) + sizeof(uint16_t);
	/* adaptation layer indication parameter */
	ali = (struct sctp_adaptation_layer_indication *)((caddr_t)sup_addr + sizeof(*sup_addr) + sizeof(uint16_t));
	ali->ph.param_type = htons(SCTP_ULP_ADAPTATION);
	ali->ph.param_length = htons(sizeof(*ali));
	ali->indication = ntohl(inp->sctp_ep.adaptation_layer_indicator);
	SCTP_BUF_LEN(m) += sizeof(*ali);
	ecn = (struct sctp_ecn_supported_param *)((caddr_t)ali + sizeof(*ali));

	if (SCTP_BASE_SYSCTL(sctp_inits_include_nat_friendly)) {
		/* Add NAT friendly parameter */
		struct sctp_paramhdr *ph;

		ph = (struct sctp_paramhdr *)(mtod(m, caddr_t) + SCTP_BUF_LEN(m));
		ph->param_type = htons(SCTP_HAS_NAT_SUPPORT);
		ph->param_length = htons(sizeof(struct sctp_paramhdr));
		SCTP_BUF_LEN(m) += sizeof(struct sctp_paramhdr);
		ecn = (struct sctp_ecn_supported_param *)((caddr_t)ph + sizeof(*ph));
	}

	
	/* now any cookie time extensions */
	if (stcb->asoc.cookie_preserve_req) {
		struct sctp_cookie_perserve_param *cookie_preserve;

		cookie_preserve = (struct sctp_cookie_perserve_param *)(ecn);
		cookie_preserve->ph.param_type = htons(SCTP_COOKIE_PRESERVE);
		cookie_preserve->ph.param_length = htons(
		    sizeof(*cookie_preserve));
		cookie_preserve->time = htonl(stcb->asoc.cookie_preserve_req);
		SCTP_BUF_LEN(m) += sizeof(*cookie_preserve);
		ecn = (struct sctp_ecn_supported_param *)(
		    (caddr_t)cookie_preserve + sizeof(*cookie_preserve));
		stcb->asoc.cookie_preserve_req = 0;
	}
	/* ECN parameter */
	if (stcb->asoc.ecn_allowed == 1) {
		ecn->ph.param_type = htons(SCTP_ECN_CAPABLE);
		ecn->ph.param_length = htons(sizeof(*ecn));
		SCTP_BUF_LEN(m) += sizeof(*ecn);
		prsctp = (struct sctp_prsctp_supported_param *)((caddr_t)ecn +
		    sizeof(*ecn));
	} else {
		prsctp = (struct sctp_prsctp_supported_param *)((caddr_t)ecn);
	}
	/* And now tell the peer we do pr-sctp */
	prsctp->ph.param_type = htons(SCTP_PRSCTP_SUPPORTED);
	prsctp->ph.param_length = htons(sizeof(*prsctp));
	SCTP_BUF_LEN(m) += sizeof(*prsctp);

	/* And now tell the peer we do all the extensions */
	pr_supported = (struct sctp_supported_chunk_types_param *)
	    ((caddr_t)prsctp + sizeof(*prsctp));
	pr_supported->ph.param_type = htons(SCTP_SUPPORTED_CHUNK_EXT);
	num_ext = 0;
	pr_supported->chunk_types[num_ext++] = SCTP_ASCONF;
	pr_supported->chunk_types[num_ext++] = SCTP_ASCONF_ACK;
	pr_supported->chunk_types[num_ext++] = SCTP_FORWARD_CUM_TSN;
	pr_supported->chunk_types[num_ext++] = SCTP_PACKET_DROPPED;
	pr_supported->chunk_types[num_ext++] = SCTP_STREAM_RESET;
	if (!SCTP_BASE_SYSCTL(sctp_auth_disable)) {
		pr_supported->chunk_types[num_ext++] = SCTP_AUTHENTICATION;
	}
	if (stcb->asoc.sctp_nr_sack_on_off == 1) {
		pr_supported->chunk_types[num_ext++] = SCTP_NR_SELECTIVE_ACK;
	}
	p_len = sizeof(*pr_supported) + num_ext;
	pr_supported->ph.param_length = htons(p_len);
	bzero((caddr_t)pr_supported + p_len, SCTP_SIZE32(p_len) - p_len);
	SCTP_BUF_LEN(m) += SCTP_SIZE32(p_len);


	/* add authentication parameters */
	if (!SCTP_BASE_SYSCTL(sctp_auth_disable)) {
		struct sctp_auth_random *randp;
		struct sctp_auth_hmac_algo *hmacs;
		struct sctp_auth_chunk_list *chunks;

		/* attach RANDOM parameter, if available */
		if (stcb->asoc.authinfo.random != NULL) {
			randp = (struct sctp_auth_random *)(mtod(m, caddr_t) + SCTP_BUF_LEN(m));
			p_len = sizeof(*randp) + stcb->asoc.authinfo.random_len;
			/* random key already contains the header */
			bcopy(stcb->asoc.authinfo.random->key, randp, p_len);
			/* zero out any padding required */
			bzero((caddr_t)randp + p_len, SCTP_SIZE32(p_len) - p_len);
			SCTP_BUF_LEN(m) += SCTP_SIZE32(p_len);
		}
		/* add HMAC_ALGO parameter */
		hmacs = (struct sctp_auth_hmac_algo *)(mtod(m, caddr_t)+ SCTP_BUF_LEN(m));
		p_len = sctp_serialize_hmaclist(stcb->asoc.local_hmacs,
		    (uint8_t *) hmacs->hmac_ids);
		if (p_len > 0) {
			p_len += sizeof(*hmacs);
			hmacs->ph.param_type = htons(SCTP_HMAC_LIST);
			hmacs->ph.param_length = htons(p_len);
			/* zero out any padding required */
			bzero((caddr_t)hmacs + p_len, SCTP_SIZE32(p_len) - p_len);
			SCTP_BUF_LEN(m) += SCTP_SIZE32(p_len);
		}
		/* add CHUNKS parameter */
		chunks = (struct sctp_auth_chunk_list *)(mtod(m, caddr_t)+ SCTP_BUF_LEN(m));
		p_len = sctp_serialize_auth_chunks(stcb->asoc.local_auth_chunks,
		    chunks->chunk_types);
		if (p_len > 0) {
			p_len += sizeof(*chunks);
			chunks->ph.param_type = htons(SCTP_CHUNK_LIST);
			chunks->ph.param_length = htons(p_len);
			/* zero out any padding required */
			bzero((caddr_t)chunks + p_len, SCTP_SIZE32(p_len) - p_len);
			SCTP_BUF_LEN(m) += SCTP_SIZE32(p_len);
		}
	}
	m_at = m;
	/* now the addresses */
	{
		struct sctp_scoping scp;
		/* To optimize this we could put the scoping stuff
		 * into a structure and remove the individual uint8's from
		 * the assoc structure. Then we could just sifa in the
		 * address within the stcb.. but for now this is a quick
		 * hack to get the address stuff teased apart.
		 */
 		scp.ipv4_addr_legal = stcb->asoc.ipv4_addr_legal;
		scp.ipv6_addr_legal = stcb->asoc.ipv6_addr_legal;
		scp.loopback_scope = stcb->asoc.loopback_scope;
		scp.ipv4_local_scope = stcb->asoc.ipv4_local_scope;
		scp.local_scope = stcb->asoc.local_scope;
		scp.site_scope = stcb->asoc.site_scope;

		m_at = sctp_add_addresses_to_i_ia(inp, stcb, &scp, m_at, cnt_inits_to);
	}

	/* calulate the size and update pkt header and chunk header */
	p_len = 0;
	for (m_at = m; m_at; m_at = SCTP_BUF_NEXT(m_at)) {
		if (SCTP_BUF_NEXT(m_at) == NULL)
			mp_last = m_at;
		p_len += SCTP_BUF_LEN(m_at);
	}
	init->ch.chunk_length = htons(p_len);
	/*
	 * We sifa 0 here to NOT set IP_DF if its IPv4, we ignore the return
	 * here since the timer will drive a retranmission.
	 */

	/* I don't expect this to execute but we will be safe here */
	padval = p_len % 4;
	if ((padval) && (mp_last)) {
		/*
		 * The compiler worries that mp_last may not be set even
		 * though I think it is impossible :-> however we add mp_last
		 * here just in case.
		 */
		ret = sctp_add_pad_tombuf(mp_last, (4 - padval));
		if (ret) {
			/* Houston we have a problem, no space */
			sctp_m_freem(m);
			return;
		}
		p_len += padval;
	}
	SCTPDBG(SCTP_DEBUG_OUTPUT4, "Sending INIT - calls lowlevel_output\n");
	ret = sctp_lowlevel_chunk_output(inp, stcb, net,
	                                 (struct sockaddr *)&net->ro._l_addr,
	                                 m, 0, NULL, 0, 0, 0, NULL, 0,
	                                 inp->sctp_lport, stcb->rport, htonl(0),
	                                 net->port, so_locked, NULL, NULL);
	SCTPDBG(SCTP_DEBUG_OUTPUT4, "lowlevel_output - %d\n", ret);
	SCTP_STAT_INCR_COUNTER64(sctps_outcontrolchunks);
	(void)SCTP_GETTIME_TIMEVAL(&net->last_sent_time);
}

struct mbuf *
sctp_arethere_unrecognized_parameters(struct mbuf *in_initpkt,
	int param_offset, int *abort_processing, struct sctp_chunkhdr *cp, int *nat_friendly)
{
	/*
	 * Given a mbuf containing an INIT or INIT-ACK with the param_offset
	 * being equal to the beginning of the params i.e. (iphlen +
	 * sizeof(struct sctp_init_msg) parse through the parameters to the
	 * end of the mbuf verifying that all parameters are known.
	 *
	 * For unknown parameters build and return a mbuf with
	 * UNRECOGNIZED_PARAMETER errors. If the flags indicate to stop
	 * processing this chunk stop, and set *abort_processing to 1.
	 *
	 * By having param_offset be pre-set to where parameters begin it is
	 * hoped that this routine may be reused in the future by new
	 * features.
	 */
	struct sctp_paramhdr *phdr, params;

	struct mbuf *mat, *op_err;
	char tempbuf[SCTP_PARAM_BUFFER_SIZE];
	int at, limit, pad_needed;
	uint16_t ptype, plen, padded_size;
	int err_at;

	*abort_processing = 0;
	mat = in_initpkt;
	err_at = 0;
	limit = ntohs(cp->chunk_length) - sizeof(struct sctp_init_chunk);
	at = param_offset;
	op_err = NULL;
	SCTPDBG(SCTP_DEBUG_OUTPUT1, "Check for unrecognized param's\n");
	phdr = sctp_get_next_param(mat, at, &params, sizeof(params));
	while ((phdr != NULL) && ((size_t)limit >= sizeof(struct sctp_paramhdr))) {
		ptype = ntohs(phdr->param_type);
		plen = ntohs(phdr->param_length);
		if ((plen > limit) || (plen < sizeof(struct sctp_paramhdr)))  {
			/* wacked parameter */
			SCTPDBG(SCTP_DEBUG_OUTPUT1, "Invalid size - error %d\n", plen);
			goto invalid_size;
		}
		limit -= SCTP_SIZE32(plen);
		/*-
		 * All parameters for all chunks that we know/understand are
		 * listed here. We process them other places and make
		 * appropriate stop actions per the upper bits. However this
		 * is the generic routine processor's can call to get back
		 * an operr.. to either incorporate (init-ack) or send.
		 */
		padded_size = SCTP_SIZE32(plen);
		switch (ptype) {
			/* Param's with variable size */
		case SCTP_HEARTBEAT_INFO:
		case SCTP_STATE_COOKIE:
		case SCTP_UNRECOG_PARAM:
		case SCTP_ERROR_CAUSE_IND:
			/* ok skip fwd */
			at += padded_size;
			break;
			/* Param's with variable size within a range */
		case SCTP_CHUNK_LIST:
		case SCTP_SUPPORTED_CHUNK_EXT:
			if (padded_size > (sizeof(struct sctp_supported_chunk_types_param) + (sizeof(uint8_t) * SCTP_MAX_SUPPORTED_EXT)))  {
				SCTPDBG(SCTP_DEBUG_OUTPUT1, "Invalid size - error chklist %d\n", plen);
				goto invalid_size;
			}
			at += padded_size;
			break;
		case SCTP_SUPPORTED_ADDRTYPE:
			if (padded_size > SCTP_MAX_ADDR_PARAMS_SIZE) {
				SCTPDBG(SCTP_DEBUG_OUTPUT1, "Invalid size - error supaddrtype %d\n", plen);
				goto invalid_size;
			}
			at += padded_size;
			break;
		case SCTP_RANDOM:
			if (padded_size > (sizeof(struct sctp_auth_random) + SCTP_RANDOM_MAX_SIZE)) {
				SCTPDBG(SCTP_DEBUG_OUTPUT1, "Invalid size - error random %d\n", plen);
				goto invalid_size;
			}
			at += padded_size;
			break;
		case SCTP_SET_PRIM_ADDR:
		case SCTP_DEL_IP_ADDRESS:
		case SCTP_ADD_IP_ADDRESS:
			if ((padded_size != sizeof(struct sctp_asconf_addrv4_param)) &&
			    (padded_size != sizeof(struct sctp_asconf_addr_param))) {
				SCTPDBG(SCTP_DEBUG_OUTPUT1, "Invalid size - error setprim %d\n", plen);
				goto invalid_size;
			}
			at += padded_size;
			break;
			/* Param's with a fixed size */
		case SCTP_IPV4_ADDRESS:
			if (padded_size != sizeof(struct sctp_ipv4addr_param)) {
				SCTPDBG(SCTP_DEBUG_OUTPUT1, "Invalid size - error ipv4 addr %d\n", plen);
				goto invalid_size;
			}
			at += padded_size;
			break;
		case SCTP_IPV6_ADDRESS:
			if (padded_size != sizeof(struct sctp_ipv6addr_param)) {
				SCTPDBG(SCTP_DEBUG_OUTPUT1, "Invalid size - error ipv6 addr %d\n", plen);
				goto invalid_size;
			}
			at += padded_size;
			break;
		case SCTP_COOKIE_PRESERVE:
			if (padded_size != sizeof(struct sctp_cookie_perserve_param)) {
				SCTPDBG(SCTP_DEBUG_OUTPUT1, "Invalid size - error cookie-preserve %d\n", plen);
				goto invalid_size;
			}
			at += padded_size;
			break;
		case SCTP_HAS_NAT_SUPPORT:
		  *nat_friendly = 1;
		  /* fall through */
		case SCTP_PRSCTP_SUPPORTED:

			if (padded_size != sizeof(struct sctp_paramhdr)) {
				SCTPDBG(SCTP_DEBUG_OUTPUT1, "Invalid size - error prsctp/nat support %d\n", plen);
				goto invalid_size;
			}
			at += padded_size;
			break;
		case SCTP_ECN_CAPABLE:
			if (padded_size != sizeof(struct sctp_ecn_supported_param)) {
				SCTPDBG(SCTP_DEBUG_OUTPUT1, "Invalid size - error ecn %d\n", plen);
				goto invalid_size;
			}
			at += padded_size;
			break;
		case SCTP_ULP_ADAPTATION:
			if (padded_size != sizeof(struct sctp_adaptation_layer_indication)) {
				SCTPDBG(SCTP_DEBUG_OUTPUT1, "Invalid size - error adapatation %d\n", plen);
				goto invalid_size;
			}
			at += padded_size;
			break;
		case SCTP_SUCCESS_REPORT:
			if (padded_size != sizeof(struct sctp_asconf_paramhdr)) {
				SCTPDBG(SCTP_DEBUG_OUTPUT1, "Invalid size - error success %d\n", plen);
				goto invalid_size;
			}
			at += padded_size;
			break;
		case SCTP_HOSTNAME_ADDRESS:
		{
			/* We can NOT handle HOST NAME addresses!! */
			int l_len;
			SCTPDBG(SCTP_DEBUG_OUTPUT1, "Can't handle hostname addresses.. abort processing\n");
			*abort_processing = 1;
			if (op_err == NULL) {
				/* Ok need to try to get a mbuf */
#ifdef INET6
				l_len = sizeof(struct ip6_hdr) + sizeof(struct sctphdr) + sizeof(struct sctp_chunkhdr);
#else
				l_len = sizeof(struct ip) + sizeof(struct sctphdr) + sizeof(struct sctp_chunkhdr);
#endif
				l_len += plen;
				l_len += sizeof(struct sctp_paramhdr);
				op_err = sctp_get_mbuf_for_msg(l_len, 0, M_DONTWAIT, 1, MT_DATA);
				if (op_err) {
					SCTP_BUF_LEN(op_err) = 0;
					/*
					 * pre-reserve space for ip and sctp
					 * header  and chunk hdr
					 */
#ifdef INET6
					SCTP_BUF_RESV_UF(op_err, sizeof(struct ip6_hdr));
#else
					SCTP_BUF_RESV_UF(op_err, sizeof(struct ip));
#endif
					SCTP_BUF_RESV_UF(op_err, sizeof(struct sctphdr));
					SCTP_BUF_RESV_UF(op_err, sizeof(struct sctp_chunkhdr));
				}
			}
			if (op_err) {
				/* If we have space */
				struct sctp_paramhdr s;

				if (err_at % 4) {
					uint32_t cpthis = 0;

					pad_needed = 4 - (err_at % 4);
					m_copyback(op_err, err_at, pad_needed, (caddr_t)&cpthis);
					err_at += pad_needed;
				}
				s.param_type = htons(SCTP_CAUSE_UNRESOLVABLE_ADDR);
				s.param_length = htons(sizeof(s) + plen);
				m_copyback(op_err, err_at, sizeof(s), (caddr_t)&s);
				err_at += sizeof(s);
				phdr = sctp_get_next_param(mat, at, (struct sctp_paramhdr *)tempbuf, min(sizeof(tempbuf),plen));
				if (phdr == NULL) {
					sctp_m_freem(op_err);
					/*
					 * we are out of memory but we still
					 * need to have a look at what to do
					 * (the system is in trouble
					 * though).
					 */
					return (NULL);
				}
				m_copyback(op_err, err_at, plen, (caddr_t)phdr);
				err_at += plen;
			}
			return (op_err);
			break;
		}
		default:
			/*
			 * we do not recognize the parameter figure out what
			 * we do.
			 */
			SCTPDBG(SCTP_DEBUG_OUTPUT1, "Hit default param %x\n", ptype);
			if ((ptype & 0x4000) == 0x4000) {
				/* Report bit is set?? */
				SCTPDBG(SCTP_DEBUG_OUTPUT1, "report op err\n");
				if (op_err == NULL) {
					int l_len;
					/* Ok need to try to get an mbuf */
#ifdef INET6
					l_len = sizeof(struct ip6_hdr) + sizeof(struct sctphdr) + sizeof(struct sctp_chunkhdr);
#else
					l_len = sizeof(struct ip) + sizeof(struct sctphdr) + sizeof(struct sctp_chunkhdr);
#endif
					l_len += plen;
					l_len += sizeof(struct sctp_paramhdr);
					op_err = sctp_get_mbuf_for_msg(l_len, 0, M_DONTWAIT, 1, MT_DATA);
					if (op_err) {
						SCTP_BUF_LEN(op_err) = 0;
#ifdef INET6
						SCTP_BUF_RESV_UF(op_err, sizeof(struct ip6_hdr));
#else
						SCTP_BUF_RESV_UF(op_err, sizeof(struct ip));
#endif
						SCTP_BUF_RESV_UF(op_err, sizeof(struct sctphdr));
						SCTP_BUF_RESV_UF(op_err, sizeof(struct sctp_chunkhdr));
					}
				}
				if (op_err) {
					/* If we have space */
					struct sctp_paramhdr s;

					if (err_at % 4) {
						uint32_t cpthis = 0;

						pad_needed = 4 - (err_at % 4);
						m_copyback(op_err, err_at, pad_needed, (caddr_t)&cpthis);
						err_at += pad_needed;
					}
					s.param_type = htons(SCTP_UNRECOG_PARAM);
					s.param_length = htons(sizeof(s) + plen);
					m_copyback(op_err, err_at, sizeof(s), (caddr_t)&s);
					err_at += sizeof(s);
					if (plen > sizeof(tempbuf)) {
						plen = sizeof(tempbuf);
					}
					phdr = sctp_get_next_param(mat, at, (struct sctp_paramhdr *)tempbuf, min(sizeof(tempbuf),plen));
					if (phdr == NULL) {
						sctp_m_freem(op_err);
						/*
						 * we are out of memory but
						 * we still need to have a
						 * look at what to do (the
						 * system is in trouble
						 * though).
						 */
						op_err = NULL;
						goto more_processing;
					}
					m_copyback(op_err, err_at, plen, (caddr_t)phdr);
					err_at += plen;
				}
			}
		more_processing:
			if ((ptype & 0x8000) == 0x0000) {
				SCTPDBG(SCTP_DEBUG_OUTPUT1, "stop proc\n");
				return (op_err);
			} else {
				/* skip this chunk and continue processing */
				SCTPDBG(SCTP_DEBUG_OUTPUT1, "move on\n");
				at += SCTP_SIZE32(plen);
			}
			break;

		}
		phdr = sctp_get_next_param(mat, at, &params, sizeof(params));
	}
	return (op_err);
 invalid_size:
	SCTPDBG(SCTP_DEBUG_OUTPUT1, "abort flag set\n");
	*abort_processing = 1;
	if ((op_err == NULL) && phdr) {
		int l_len;
#ifdef INET6
		l_len = sizeof(struct ip6_hdr) + sizeof(struct sctphdr) + sizeof(struct sctp_chunkhdr);
#else
		l_len = sizeof(struct ip) + sizeof(struct sctphdr) + sizeof(struct sctp_chunkhdr);
#endif
		l_len += (2 * sizeof(struct sctp_paramhdr));
		op_err = sctp_get_mbuf_for_msg(l_len, 0, M_DONTWAIT, 1, MT_DATA);
        if (op_err) {
    		SCTP_BUF_LEN(op_err) = 0;
#ifdef INET6
	    	SCTP_BUF_RESV_UF(op_err, sizeof(struct ip6_hdr));
#else
	    	SCTP_BUF_RESV_UF(op_err, sizeof(struct ip));
#endif
		SCTP_BUF_RESV_UF(op_err, sizeof(struct sctphdr));
    		SCTP_BUF_RESV_UF(op_err, sizeof(struct sctp_chunkhdr));
        }
	}
	if ((op_err) && phdr) {
		struct sctp_paramhdr s;

		if (err_at % 4) {
			uint32_t cpthis = 0;

			pad_needed = 4 - (err_at % 4);
			m_copyback(op_err, err_at, pad_needed, (caddr_t)&cpthis);
			err_at += pad_needed;
		}
		s.param_type = htons(SCTP_CAUSE_PROTOCOL_VIOLATION);
		s.param_length = htons(sizeof(s) + sizeof(struct sctp_paramhdr));
		m_copyback(op_err, err_at, sizeof(s), (caddr_t)&s);
		err_at += sizeof(s);
		/* Only copy back the p-hdr that caused the issue */
		m_copyback(op_err, err_at, sizeof(struct sctp_paramhdr), (caddr_t)phdr);
	}
	return (op_err);
}

static int
sctp_are_there_new_addresses(struct sctp_association *asoc,
    struct mbuf *in_initpkt, int iphlen, int offset)
{
	/*
	 * Given a INIT packet, look through the packet to verify that there
	 * are NO new addresses. As we go through the parameters add reports
	 * of any un-understood parameters that require an error.  Also we
	 * must return (1) to drop the packet if we see a un-understood
	 * parameter that tells us to drop the chunk.
	 */
	struct sockaddr *sa_touse;
	struct sockaddr *sa;
	struct sctp_paramhdr *phdr, params;
	uint16_t ptype, plen;
	uint8_t fnd;
	struct sctp_nets *net;
	struct ip *iph;
#ifdef INET
	struct sockaddr_in sin4, *sa4;
#endif
#ifdef INET6
	struct sockaddr_in6 sin6, *sa6;
	struct ip6_hdr *ip6h;
#endif

#ifdef INET
	memset(&sin4, 0, sizeof(sin4));
	sin4.sin_family = AF_INET;
#if !defined(__Windows__) && !defined(__Userspace_os_Linux)
	sin4.sin_len = sizeof(sin4);
#endif
#endif
#ifdef INET6
	memset(&sin6, 0, sizeof(sin6));
	sin6.sin6_family = AF_INET6;
#if !defined(__Windows__) && !defined(__Userspace_os_Linux)
	sin6.sin6_len = sizeof(sin6);
#endif
#endif
	sa_touse = NULL;
	/* First what about the src address of the pkt ? */
	iph = mtod(in_initpkt, struct ip *);
	switch (iph->ip_v) {
#ifdef INET
	case IPVERSION:
		/* source addr is IPv4 */
		sin4.sin_addr = iph->ip_src;
		sa_touse = (struct sockaddr *)&sin4;
		break;
#endif
#ifdef INET6
	case IPV6_VERSION >> 4:
		/* source addr is IPv6 */
		ip6h = mtod(in_initpkt, struct ip6_hdr *);
		sin6.sin6_addr = ip6h->ip6_src;
		sa_touse = (struct sockaddr *)&sin6;
		break;
#endif
	default:
		return (1);
	}

	fnd = 0;
	TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
		sa = (struct sockaddr *)&net->ro._l_addr;
		if (sa->sa_family == sa_touse->sa_family) {
#ifdef INET
			if (sa->sa_family == AF_INET) {
				sa4 = (struct sockaddr_in *)sa;
				if (sa4->sin_addr.s_addr == sin4.sin_addr.s_addr) {
					fnd = 1;
					break;
				}
			}
#endif
#ifdef INET6
			if (sa->sa_family == AF_INET6) {
				sa6 = (struct sockaddr_in6 *)sa;
				if (SCTP6_ARE_ADDR_EQUAL(sa6, &sin6)) {
					fnd = 1;
					break;
				}
			}
#endif
		}
	}
	if (fnd == 0) {
		/* New address added! no need to look futher. */
		return (1);
	}
	/* Ok so far lets munge through the rest of the packet */
	offset += sizeof(struct sctp_init_chunk);
	phdr = sctp_get_next_param(in_initpkt, offset, &params, sizeof(params));
	while (phdr) {
		sa_touse = NULL;
		ptype = ntohs(phdr->param_type);
		plen = ntohs(phdr->param_length);
		switch (ptype) {
#ifdef INET
		case SCTP_IPV4_ADDRESS:
		{
			struct sctp_ipv4addr_param *p4, p4_buf;

			phdr = sctp_get_next_param(in_initpkt, offset,
			    (struct sctp_paramhdr *)&p4_buf, sizeof(p4_buf));
			if (plen != sizeof(struct sctp_ipv4addr_param) ||
			    phdr == NULL) {
				return (1);
			}
			p4 = (struct sctp_ipv4addr_param *)phdr;
			sin4.sin_addr.s_addr = p4->addr;
			sa_touse = (struct sockaddr *)&sin4;
		}
#endif
#ifdef INET6
		case SCTP_IPV6_ADDRESS:
		{
			struct sctp_ipv6addr_param *p6, p6_buf;

			phdr = sctp_get_next_param(in_initpkt, offset,
			    (struct sctp_paramhdr *)&p6_buf, sizeof(p6_buf));
			if (plen != sizeof(struct sctp_ipv6addr_param) ||
			    phdr == NULL) {
				return (1);
			}
			p6 = (struct sctp_ipv6addr_param *)phdr;
			memcpy((caddr_t)&sin6.sin6_addr, p6->addr,
			    sizeof(p6->addr));
			sa_touse = (struct sockaddr *)&sin6;
		}
#endif
		default:
			sa_touse = NULL;
		}
		if (sa_touse) {
			/* ok, sa_touse points to one to check */
			fnd = 0;
			TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
				sa = (struct sockaddr *)&net->ro._l_addr;
				if (sa->sa_family != sa_touse->sa_family) {
					continue;
				}
#ifdef INET
				if (sa->sa_family == AF_INET) {
					sa4 = (struct sockaddr_in *)sa;
					if (sa4->sin_addr.s_addr ==
					    sin4.sin_addr.s_addr) {
						fnd = 1;
						break;
					}
				}
#endif
#ifdef INET6
				if (sa->sa_family == AF_INET6) {
					sa6 = (struct sockaddr_in6 *)sa;
					if (SCTP6_ARE_ADDR_EQUAL(
					    sa6, &sin6)) {
						fnd = 1;
						break;
					}
				}
#endif
			}
			if (!fnd) {
				/* New addr added! no need to look further */
				return (1);
			}
		}
		offset += SCTP_SIZE32(plen);
		phdr = sctp_get_next_param(in_initpkt, offset, &params, sizeof(params));
	}
	return (0);
}

/*
 * Given a MBUF chain that was sent into us containing an INIT. Build a
 * INIT-ACK with COOKIE and send back. We assume that the in_initpkt has done
 * a pullup to include IPv6/4header, SCTP header and initial part of INIT
 * message (i.e. the struct sctp_init_msg).
 */
void
sctp_send_initiate_ack(struct sctp_inpcb *inp, struct sctp_tcb *stcb,
    struct mbuf *init_pkt, int iphlen, int offset, struct sctphdr *sh,
    struct sctp_init_chunk *init_chk, uint32_t vrf_id, uint16_t port, int hold_inp_lock)
{
	struct sctp_association *asoc;
	struct mbuf *m, *m_at, *m_tmp, *m_cookie, *op_err, *mp_last;
	struct sctp_init_ack_chunk *initack;
	struct sctp_adaptation_layer_indication *ali;
	struct sctp_ecn_supported_param *ecn;
	struct sctp_prsctp_supported_param *prsctp;
	struct sctp_supported_chunk_types_param *pr_supported;
	union sctp_sockstore store, store1, *over_addr;
#ifdef INET
	struct sockaddr_in *sin, *to_sin;
#endif
#ifdef INET6
	struct sockaddr_in6 *sin6, *to_sin6;
#endif
	struct ip *iph;
#ifdef INET6
	struct ip6_hdr *ip6;
#endif
	struct sockaddr *to;
	struct sctp_state_cookie stc;
	struct sctp_nets *net = NULL;
	uint8_t *signature=NULL;
	int cnt_inits_to = 0;
	uint16_t his_limit, i_want;
	int abort_flag, padval;
	int num_ext;
	int p_len;
	int nat_friendly=0;
	struct socket *so;

	if (stcb)
		asoc = &stcb->asoc;
	else
		asoc = NULL;
	mp_last = NULL;
	if ((asoc != NULL) &&
	    (SCTP_GET_STATE(asoc) != SCTP_STATE_COOKIE_WAIT) &&
	    (sctp_are_there_new_addresses(asoc, init_pkt, iphlen, offset))) {
		/* new addresses, out of here in non-cookie-wait states */
		/*
		 * Send a ABORT, we don't add the new address error clause
		 * though we even set the T bit and copy in the 0 tag.. this
		 * looks no different than if no listener was present.
		 */
		sctp_send_abort(init_pkt, iphlen, sh, 0, NULL, vrf_id, port);
		return;
	}
	abort_flag = 0;
	op_err = sctp_arethere_unrecognized_parameters(init_pkt,
						       (offset + sizeof(struct sctp_init_chunk)),
						       &abort_flag, (struct sctp_chunkhdr *)init_chk, &nat_friendly);
	if (abort_flag) {
	do_a_abort:
		sctp_send_abort(init_pkt, iphlen, sh,
				init_chk->init.initiate_tag, op_err, vrf_id, port);
		return;
	}
	m = sctp_get_mbuf_for_msg(MCLBYTES, 0, M_DONTWAIT, 1, MT_DATA);
	if (m == NULL) {
		/* No memory, INIT timer will re-attempt. */
		if (op_err)
			sctp_m_freem(op_err);
		return;
	}
	SCTP_BUF_LEN(m) = sizeof(struct sctp_init_chunk);

	/* the time I built cookie */
	(void)SCTP_GETTIME_TIMEVAL(&stc.time_entered);

	/* populate any tie tags */
	if (asoc != NULL) {
		/* unlock before tag selections */
		stc.tie_tag_my_vtag = asoc->my_vtag_nonce;
		stc.tie_tag_peer_vtag = asoc->peer_vtag_nonce;
		stc.cookie_life = asoc->cookie_life;
		net = asoc->primary_destination;
	} else {
		stc.tie_tag_my_vtag = 0;
		stc.tie_tag_peer_vtag = 0;
		/* life I will award this cookie */
		stc.cookie_life = inp->sctp_ep.def_cookie_life;
	}

	/* copy in the ports for later check */
	stc.myport = sh->dest_port;
	stc.peerport = sh->src_port;

	/*
	 * If we wanted to honor cookie life extentions, we would add to
	 * stc.cookie_life. For now we should NOT honor any extension
	 */
	stc.site_scope = stc.local_scope = stc.loopback_scope = 0;
	if (inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) {
		struct inpcb *in_inp;

		/* Its a V6 socket */
		in_inp = (struct inpcb *)inp;
		stc.ipv6_addr_legal = 1;
		/* Now look at the binding flag to see if V4 will be legal */
		if (SCTP_IPV6_V6ONLY(in_inp) == 0) {
			stc.ipv4_addr_legal = 1;
		} else {
			/* V4 addresses are NOT legal on the association */
			stc.ipv4_addr_legal = 0;
		}
	} else {
		/* Its a V4 socket, no - V6 */
		stc.ipv4_addr_legal = 1;
		stc.ipv6_addr_legal = 0;
	}

#ifdef SCTP_DONT_DO_PRIVADDR_SCOPE
	stc.ipv4_scope = 1;
#else
	stc.ipv4_scope = 0;
#endif
	/* now for scope setup */
	memset((caddr_t)&store, 0, sizeof(store));
	memset((caddr_t)&store1, 0, sizeof(store1));
#ifdef INET
	sin = &store.sin;
	to_sin = &store1.sin;
#endif
#ifdef INET6
	sin6 = &store.sin6;
	to_sin6 = &store1.sin6;
#endif
	iph = mtod(init_pkt, struct ip *);
	/* establish the to_addr's */
	switch (iph->ip_v) {
#ifdef INET
	case IPVERSION:
		  to_sin->sin_port = sh->dest_port;
		  to_sin->sin_family = AF_INET;
#if !defined(__Windows__) && !defined(__Userspace_os_Linux)
		  to_sin->sin_len = sizeof(struct sockaddr_in);
#endif
		  to_sin->sin_addr = iph->ip_dst;
		  break;
#endif
#ifdef INET6
	case IPV6_VERSION >> 4:
		  ip6 = mtod(init_pkt, struct ip6_hdr *);
		  to_sin6->sin6_addr = ip6->ip6_dst;
		  to_sin6->sin6_scope_id = 0;
		  to_sin6->sin6_port = sh->dest_port;
		  to_sin6->sin6_family = AF_INET6;
#if !defined(__Windows__) && !defined(__Userspace_os_Linux)
		  to_sin6->sin6_len = sizeof(struct sockaddr_in6);
#endif
		  break;
#endif
	default:
		goto do_a_abort;
		break;
	};

	if (net == NULL) {
		to = (struct sockaddr *)&store;
		switch (iph->ip_v) {
#ifdef INET
		case IPVERSION:
		{
			sin->sin_family = AF_INET;
#if !defined(__Windows__) && !defined(__Userspace_os_Linux)
			sin->sin_len = sizeof(struct sockaddr_in);
#endif
			sin->sin_port = sh->src_port;
			sin->sin_addr = iph->ip_src;
			/* lookup address */
			stc.address[0] = sin->sin_addr.s_addr;
			stc.address[1] = 0;
			stc.address[2] = 0;
			stc.address[3] = 0;
			stc.addr_type = SCTP_IPV4_ADDRESS;
			/* local from address */
			stc.laddress[0] = to_sin->sin_addr.s_addr;
			stc.laddress[1] = 0;
			stc.laddress[2] = 0;
			stc.laddress[3] = 0;
			stc.laddr_type = SCTP_IPV4_ADDRESS;
			/* scope_id is only for v6 */
			stc.scope_id = 0;
#ifndef SCTP_DONT_DO_PRIVADDR_SCOPE
			if (IN4_ISPRIVATE_ADDRESS(&sin->sin_addr)) {
				stc.ipv4_scope = 1;
			}
#else
			stc.ipv4_scope = 1;
#endif				/* SCTP_DONT_DO_PRIVADDR_SCOPE */
			/* Must use the address in this case */
			if (sctp_is_address_on_local_host((struct sockaddr *)sin, vrf_id)) {
				stc.loopback_scope = 1;
				stc.ipv4_scope = 1;
				stc.site_scope = 1;
				stc.local_scope = 0;
			}
			break;
		}
#endif
#ifdef INET6
		case IPV6_VERSION >> 4:
		{
			ip6 = mtod(init_pkt, struct ip6_hdr *);
			sin6->sin6_family = AF_INET6;
#if !defined(__Windows__) && !defined(__Userspace_os_Linux)
			sin6->sin6_len = sizeof(struct sockaddr_in6);
#endif
			sin6->sin6_port = sh->src_port;
			sin6->sin6_addr = ip6->ip6_src;
			/* lookup address */
			memcpy(&stc.address, &sin6->sin6_addr,
			       sizeof(struct in6_addr));
			sin6->sin6_scope_id = 0;
			stc.addr_type = SCTP_IPV6_ADDRESS;
			stc.scope_id = 0;
			if (sctp_is_address_on_local_host((struct sockaddr *)sin6, vrf_id)) {
#ifdef SCTP_EMBEDDED_V6_SCOPE
#ifndef SCTP_KAME
				(void)in6_recoverscope(sin6, &ip6->ip6_src,
						       SCTP_BUF_RECVIF(init_pkt));
#else
				/* FIX ME: does this have scope from rcvif? */
				(void)sa6_recoverscope(sin6);
#endif /* not SCTP_KAME */
				stc.scope_id = sin6->sin6_scope_id;
#if defined(SCTP_BASE_FREEBSD) || defined(__APPLE__)
#if defined(APPLE_LION)
				in6_embedscope(&sin6->sin6_addr, sin6, NULL,
					       NULL, NULL);
#else
				in6_embedscope(&sin6->sin6_addr, sin6, NULL,
					       NULL);
#endif
#elif defined(SCTP_KAME)
				sa6_embedscope(sin6, MODULE_GLOBAL(ip6_use_defzone));
#else
				in6_embedscope(&sin6->sin6_addr, sin6);
#endif
#else
				stc.scope_id = sin6->sin6_scope_id;
#endif /* SCTP_EMBEDDED_V6_SCOPE */
				stc.loopback_scope = 1;
				stc.local_scope = 0;
				stc.site_scope = 1;
				stc.ipv4_scope = 1;
			} else if (IN6_IS_ADDR_LINKLOCAL(&sin6->sin6_addr)) {
				/*
				 * If the new destination is a LINK_LOCAL we
				 * must have common both site and local
				 * scope. Don't set local scope though since
				 * we must depend on the source to be added
				 * implicitly. We cannot assure just because
				 * we share one link that all links are
				 * common.
				 */
				stc.local_scope = 0;
				stc.site_scope = 1;
				stc.ipv4_scope = 1;
				/*
				 * we start counting for the private address
				 * stuff at 1. since the link local we
				 * source from won't show up in our scoped
				 * count.
				 */
				cnt_inits_to = 1;
				/* pull out the scope_id from incoming pkt */
#ifdef SCTP_EMBEDDED_V6_SCOPE
#ifndef SCTP_KAME
				(void)in6_recoverscope(sin6, &ip6->ip6_src,
						       SCTP_BUF_RECVIF(init_pkt));
#else
				/* FIX ME: does this have scope from rcvif? */
				(void)sa6_recoverscope(sin6);
#endif /* not SCTP_KAME */
				stc.scope_id = sin6->sin6_scope_id;
#if defined(SCTP_BASE_FREEBSD) || defined(__APPLE__)
#if defined(APPLE_LION)
				in6_embedscope(&sin6->sin6_addr, sin6, NULL,
					       NULL, NULL);
#else
				in6_embedscope(&sin6->sin6_addr, sin6, NULL,
					       NULL);
#endif
#elif defined(SCTP_KAME)
				sa6_embedscope(sin6, MODULE_GLOBAL(ip6_use_defzone));
#else
				in6_embedscope(&sin6->sin6_addr, sin6);
#endif
#else
				stc.scope_id = sin6->sin6_scope_id;
#endif /* SCTP_EMBEDDED_V6_SCOPE */
			} else if (IN6_IS_ADDR_SITELOCAL(&sin6->sin6_addr)) {
				/*
				 * If the new destination is SITE_LOCAL then
				 * we must have site scope in common.
				 */
				stc.site_scope = 1;
			}
			memcpy(&stc.laddress, &to_sin6->sin6_addr, sizeof(struct in6_addr));
			stc.laddr_type = SCTP_IPV6_ADDRESS;
			break;
		}
#endif
		default:
			/* TSNH */
		    goto do_a_abort;
			break;
		}
	} else {
		/* set the scope per the existing tcb */

#ifdef INET6
		struct sctp_nets *lnet;
#endif

		stc.loopback_scope = asoc->loopback_scope;
		stc.ipv4_scope = asoc->ipv4_local_scope;
		stc.site_scope = asoc->site_scope;
		stc.local_scope = asoc->local_scope;
#ifdef INET6
		/* Why do we not consider IPv4 LL addresses? */
		TAILQ_FOREACH(lnet, &asoc->nets, sctp_next) {
			if (lnet->ro._l_addr.sin6.sin6_family == AF_INET6) {
				if (IN6_IS_ADDR_LINKLOCAL(&lnet->ro._l_addr.sin6.sin6_addr)) {
					/*
					 * if we have a LL address, start
					 * counting at 1.
					 */
					cnt_inits_to = 1;
				}
			}
		}
#endif
		/* use the net pointer */
		to = (struct sockaddr *)&net->ro._l_addr;
		switch (to->sa_family) {
#ifdef INET
		case AF_INET:
			sin = (struct sockaddr_in *)to;
			stc.address[0] = sin->sin_addr.s_addr;
			stc.address[1] = 0;
			stc.address[2] = 0;
			stc.address[3] = 0;
			stc.addr_type = SCTP_IPV4_ADDRESS;
			if (net->src_addr_selected == 0) {
				/*
				 * strange case here, the INIT should have
				 * did the selection.
				 */
				net->ro._s_addr = sctp_source_address_selection(inp,
										stcb, (sctp_route_t *)&net->ro,
										net, 0, vrf_id);
				if (net->ro._s_addr == NULL)
					return;

				net->src_addr_selected = 1;

			}
			stc.laddress[0] = net->ro._s_addr->address.sin.sin_addr.s_addr;
			stc.laddress[1] = 0;
			stc.laddress[2] = 0;
			stc.laddress[3] = 0;
			stc.laddr_type = SCTP_IPV4_ADDRESS;
			break;
#endif
#ifdef INET6
		case AF_INET6:
			sin6 = (struct sockaddr_in6 *)to;
			memcpy(&stc.address, &sin6->sin6_addr,
			       sizeof(struct in6_addr));
			stc.addr_type = SCTP_IPV6_ADDRESS;
			if (net->src_addr_selected == 0) {
				/*
				 * strange case here, the INIT should have
				 * did the selection.
				 */
				net->ro._s_addr = sctp_source_address_selection(inp,
										stcb, (sctp_route_t *)&net->ro,
										net, 0, vrf_id);
				if (net->ro._s_addr == NULL)
					return;

				net->src_addr_selected = 1;
			}
			memcpy(&stc.laddress, &net->ro._s_addr->address.sin6.sin6_addr,
			       sizeof(struct in6_addr));
			stc.laddr_type = SCTP_IPV6_ADDRESS;
			break;
#endif
		}
	}
	/* Now lets put the SCTP header in place */
	initack = mtod(m, struct sctp_init_ack_chunk *);
	/* Save it off for quick ref */
	stc.peers_vtag = init_chk->init.initiate_tag;
	/* who are we */
	memcpy(stc.identification, SCTP_VERSION_STRING,
	       min(strlen(SCTP_VERSION_STRING), sizeof(stc.identification)));
	/* now the chunk header */
	initack->ch.chunk_type = SCTP_INITIATION_ACK;
	initack->ch.chunk_flags = 0;
	/* fill in later from mbuf we build */
	initack->ch.chunk_length = 0;
	/* place in my tag */
	if ((asoc != NULL) &&
	    ((SCTP_GET_STATE(asoc) == SCTP_STATE_COOKIE_WAIT) ||
	     (SCTP_GET_STATE(asoc) == SCTP_STATE_INUSE) ||
	     (SCTP_GET_STATE(asoc) == SCTP_STATE_COOKIE_ECHOED))) {
		/* re-use the v-tags and init-seq here */
		initack->init.initiate_tag = htonl(asoc->my_vtag);
		initack->init.initial_tsn = htonl(asoc->init_seq_number);
	} else {
		uint32_t vtag, itsn;
		if (hold_inp_lock) {
			SCTP_INP_INCR_REF(inp);
			SCTP_INP_RUNLOCK(inp);
		}
		if (asoc) {
			atomic_add_int(&asoc->refcnt, 1);
			SCTP_TCB_UNLOCK(stcb);
		new_tag:
			vtag = sctp_select_a_tag(inp, inp->sctp_lport, sh->src_port, 1);
			if ((asoc->peer_supports_nat)  && (vtag == asoc->my_vtag)) {
			  /* Got a duplicate vtag on some guy behind a nat
			   * make sure we don't use it.
			   */
			  goto new_tag;
			}
			initack->init.initiate_tag = htonl(vtag);
			/* get a TSN to use too */
			itsn = sctp_select_initial_TSN(&inp->sctp_ep);
			initack->init.initial_tsn = htonl(itsn);
			SCTP_TCB_LOCK(stcb);
			atomic_add_int(&asoc->refcnt, -1);
		} else {
			vtag = sctp_select_a_tag(inp, inp->sctp_lport, sh->src_port, 1);
			initack->init.initiate_tag = htonl(vtag);
			/* get a TSN to use too */
			initack->init.initial_tsn = htonl(sctp_select_initial_TSN(&inp->sctp_ep));
		}
		if (hold_inp_lock) {
			SCTP_INP_RLOCK(inp);
			SCTP_INP_DECR_REF(inp);
		}
	}
	/* save away my tag to */
	stc.my_vtag = initack->init.initiate_tag;

	/* set up some of the credits. */
	so = inp->sctp_socket;
	if (so == NULL) {
		/* memory problem */
		sctp_m_freem(m);
		return;
	} else {
		initack->init.a_rwnd = htonl(max(SCTP_SB_LIMIT_RCV(so), SCTP_MINIMAL_RWND));
	}
	/* set what I want */
	his_limit = ntohs(init_chk->init.num_inbound_streams);
	/* choose what I want */
	if (asoc != NULL) {
		if (asoc->streamoutcnt > inp->sctp_ep.pre_open_stream_count) {
			i_want = asoc->streamoutcnt;
		} else {
			i_want = inp->sctp_ep.pre_open_stream_count;
		}
	} else {
		i_want = inp->sctp_ep.pre_open_stream_count;
	}
	if (his_limit < i_want) {
		/* I Want more :< */
		initack->init.num_outbound_streams = init_chk->init.num_inbound_streams;
	} else {
		/* I can have what I want :> */
		initack->init.num_outbound_streams = htons(i_want);
	}
	/* tell him his limt. */
	initack->init.num_inbound_streams =
		htons(inp->sctp_ep.max_open_streams_intome);

	/* adaptation layer indication parameter */
	ali = (struct sctp_adaptation_layer_indication *)((caddr_t)initack + sizeof(*initack));
	ali->ph.param_type = htons(SCTP_ULP_ADAPTATION);
	ali->ph.param_length = htons(sizeof(*ali));
	ali->indication = ntohl(inp->sctp_ep.adaptation_layer_indicator);
	SCTP_BUF_LEN(m) += sizeof(*ali);
	ecn = (struct sctp_ecn_supported_param *)((caddr_t)ali + sizeof(*ali));

	/* ECN parameter */
	if (((asoc != NULL) && (asoc->ecn_allowed == 1)) ||
	    (inp->sctp_ecn_enable == 1)) {
		ecn->ph.param_type = htons(SCTP_ECN_CAPABLE);
		ecn->ph.param_length = htons(sizeof(*ecn));
		SCTP_BUF_LEN(m) += sizeof(*ecn);

		prsctp = (struct sctp_prsctp_supported_param *)((caddr_t)ecn +
								sizeof(*ecn));
	} else {
		prsctp = (struct sctp_prsctp_supported_param *)((caddr_t)ecn);
	}
	/* And now tell the peer we do  pr-sctp */
	prsctp->ph.param_type = htons(SCTP_PRSCTP_SUPPORTED);
	prsctp->ph.param_length = htons(sizeof(*prsctp));
	SCTP_BUF_LEN(m) += sizeof(*prsctp);
	if (nat_friendly) {
		/* Add NAT friendly parameter */
		struct sctp_paramhdr *ph;

		ph = (struct sctp_paramhdr *)(mtod(m, caddr_t) + SCTP_BUF_LEN(m));
		ph->param_type = htons(SCTP_HAS_NAT_SUPPORT);
		ph->param_length = htons(sizeof(struct sctp_paramhdr));
		SCTP_BUF_LEN(m) += sizeof(struct sctp_paramhdr);
	}
	/* And now tell the peer we do all the extensions */
	pr_supported = (struct sctp_supported_chunk_types_param *)(mtod(m, caddr_t) + SCTP_BUF_LEN(m));
	pr_supported->ph.param_type = htons(SCTP_SUPPORTED_CHUNK_EXT);
	num_ext = 0;
	pr_supported->chunk_types[num_ext++] = SCTP_ASCONF;
	pr_supported->chunk_types[num_ext++] = SCTP_ASCONF_ACK;
	pr_supported->chunk_types[num_ext++] = SCTP_FORWARD_CUM_TSN;
	pr_supported->chunk_types[num_ext++] = SCTP_PACKET_DROPPED;
	pr_supported->chunk_types[num_ext++] = SCTP_STREAM_RESET;
	if (!SCTP_BASE_SYSCTL(sctp_auth_disable))
		pr_supported->chunk_types[num_ext++] = SCTP_AUTHENTICATION;
	if (SCTP_BASE_SYSCTL(sctp_nr_sack_on_off))
		pr_supported->chunk_types[num_ext++] = SCTP_NR_SELECTIVE_ACK;
	p_len = sizeof(*pr_supported) + num_ext;
	pr_supported->ph.param_length = htons(p_len);
	bzero((caddr_t)pr_supported + p_len, SCTP_SIZE32(p_len) - p_len);
	SCTP_BUF_LEN(m) += SCTP_SIZE32(p_len);

	/* add authentication parameters */
	if (!SCTP_BASE_SYSCTL(sctp_auth_disable)) {
		struct sctp_auth_random *randp;
		struct sctp_auth_hmac_algo *hmacs;
		struct sctp_auth_chunk_list *chunks;
		uint16_t random_len;

		/* generate and add RANDOM parameter */
		random_len = SCTP_AUTH_RANDOM_SIZE_DEFAULT;
		randp = (struct sctp_auth_random *)(mtod(m, caddr_t) + SCTP_BUF_LEN(m));
		randp->ph.param_type = htons(SCTP_RANDOM);
		p_len = sizeof(*randp) + random_len;
		randp->ph.param_length = htons(p_len);
		SCTP_READ_RANDOM(randp->random_data, random_len);
		/* zero out any padding required */
		bzero((caddr_t)randp + p_len, SCTP_SIZE32(p_len) - p_len);
		SCTP_BUF_LEN(m) += SCTP_SIZE32(p_len);

		/* add HMAC_ALGO parameter */
		hmacs = (struct sctp_auth_hmac_algo *)(mtod(m, caddr_t)+ SCTP_BUF_LEN(m));
		p_len = sctp_serialize_hmaclist(inp->sctp_ep.local_hmacs,
						(uint8_t *) hmacs->hmac_ids);
		if (p_len > 0) {
			p_len += sizeof(*hmacs);
			hmacs->ph.param_type = htons(SCTP_HMAC_LIST);
			hmacs->ph.param_length = htons(p_len);
			/* zero out any padding required */
			bzero((caddr_t)hmacs + p_len, SCTP_SIZE32(p_len) - p_len);
			SCTP_BUF_LEN(m) += SCTP_SIZE32(p_len);
		}
		/* add CHUNKS parameter */
		chunks = (struct sctp_auth_chunk_list *)(mtod(m, caddr_t)+ SCTP_BUF_LEN(m));
		p_len = sctp_serialize_auth_chunks(inp->sctp_ep.local_auth_chunks,
						   chunks->chunk_types);
		if (p_len > 0) {
			p_len += sizeof(*chunks);
			chunks->ph.param_type = htons(SCTP_CHUNK_LIST);
			chunks->ph.param_length = htons(p_len);
			/* zero out any padding required */
			bzero((caddr_t)chunks + p_len, SCTP_SIZE32(p_len) - p_len);
			SCTP_BUF_LEN(m) += SCTP_SIZE32(p_len);
		}
	}
	m_at = m;
	/* now the addresses */
	{
		struct sctp_scoping scp;
		/* To optimize this we could put the scoping stuff
		 * into a structure and remove the individual uint8's from
		 * the stc structure. Then we could just sifa in the
		 * address within the stc.. but for now this is a quick
		 * hack to get the address stuff teased apart.
		 */
 		scp.ipv4_addr_legal = stc.ipv4_addr_legal;
		scp.ipv6_addr_legal = stc.ipv6_addr_legal;
		scp.loopback_scope = stc.loopback_scope;
		scp.ipv4_local_scope = stc.ipv4_scope;
		scp.local_scope = stc.local_scope;
		scp.site_scope = stc.site_scope;
		m_at = sctp_add_addresses_to_i_ia(inp, stcb, &scp, m_at, cnt_inits_to);
	}

	/* tack on the operational error if present */
	if (op_err) {
		struct mbuf *ol;
		int llen;
		llen = 0;
		ol = op_err;
		while(ol) {
			llen += SCTP_BUF_LEN(ol);
			ol = SCTP_BUF_NEXT(ol);
		}
		if (llen % 4) {
			/* must add a pad to the param */
			uint32_t cpthis = 0;
			int padlen;

			padlen = 4 - (llen % 4);
			m_copyback(op_err, llen, padlen, (caddr_t)&cpthis);
		}
		while (SCTP_BUF_NEXT(m_at) != NULL) {
			m_at = SCTP_BUF_NEXT(m_at);
		}
		SCTP_BUF_NEXT(m_at) = op_err;
		while (SCTP_BUF_NEXT(m_at) != NULL) {
			m_at = SCTP_BUF_NEXT(m_at);
		}
	}
	/* pre-calulate the size and update pkt header and chunk header */
	p_len = 0;
	for (m_tmp = m; m_tmp; m_tmp = SCTP_BUF_NEXT(m_tmp)) {
		p_len += SCTP_BUF_LEN(m_tmp);
		if (SCTP_BUF_NEXT(m_tmp) == NULL) {
			/* m_tmp should now point to last one */
			break;
		}
	}

	/* Now we must build a cookie */
	m_cookie = sctp_add_cookie(inp, init_pkt, offset, m, 0, &stc, &signature);
	if (m_cookie == NULL) {
		/* memory problem */
		sctp_m_freem(m);
		return;
	}
	/* Now append the cookie to the end and update the space/size */
	SCTP_BUF_NEXT(m_tmp) = m_cookie;

	for (m_tmp = m_cookie; m_tmp; m_tmp = SCTP_BUF_NEXT(m_tmp)) {
		p_len += SCTP_BUF_LEN(m_tmp);
		if (SCTP_BUF_NEXT(m_tmp) == NULL) {
			/* m_tmp should now point to last one */
			mp_last = m_tmp;
			break;
		}
	}
	/* Place in the size, but we don't include
	 * the last pad (if any) in the INIT-ACK.
	 */
	initack->ch.chunk_length = htons(p_len);

	/* Time to sign the cookie, we don't sign over the cookie
	 * signature though thus we set trailer.
	 */
	(void)sctp_hmac_m(SCTP_HMAC,
			  (uint8_t *)inp->sctp_ep.secret_key[(int)(inp->sctp_ep.current_secret_number)],
			  SCTP_SECRET_SIZE, m_cookie, sizeof(struct sctp_paramhdr),
			  (uint8_t *)signature, SCTP_SIGNATURE_SIZE);
	/*
	 * We sifa 0 here to NOT set IP_DF if its IPv4, we ignore the return
	 * here since the timer will drive a retranmission.
	 */
	padval = p_len % 4;
	if ((padval) && (mp_last)) {
		/* see my previous comments on mp_last */
		int ret;

		ret = sctp_add_pad_tombuf(mp_last, (4 - padval));
		if (ret) {
			/* Houston we have a problem, no space */
			sctp_m_freem(m);
			return;
		}
		p_len += padval;
	}
	if (stc.loopback_scope) {
		over_addr = &store1;
	} else {
		over_addr = NULL;
	}
	
	(void)sctp_lowlevel_chunk_output(inp, NULL, NULL, to, m, 0, NULL, 0, 0,
	                                 0, NULL, 0,
	                                 inp->sctp_lport, sh->src_port, init_chk->init.initiate_tag,
	                                 port, SCTP_SO_NOT_LOCKED, over_addr, init_pkt);
	SCTP_STAT_INCR_COUNTER64(sctps_outcontrolchunks);
}


static void
sctp_prune_prsctp(struct sctp_tcb *stcb,
    struct sctp_association *asoc,
    struct sctp_sndrcvinfo *srcv,
    int dataout)
{
	int freed_spc = 0;
	struct sctp_tmit_chunk *chk, *nchk;

	SCTP_TCB_LOCK_ASSERT(stcb);
	if ((asoc->peer_supports_prsctp) &&
	    (asoc->sent_queue_cnt_removeable > 0)) {
		TAILQ_FOREACH(chk, &asoc->sent_queue, sctp_next) {
			/*
			 * Look for chunks marked with the PR_SCTP flag AND
			 * the buffer space flag. If the one being sent is
			 * equal or greater priority then purge the old one
			 * and free some space.
			 */
			if (PR_SCTP_BUF_ENABLED(chk->flags)) {
				/*
				 * This one is PR-SCTP AND buffer space
				 * limited type
				 */
				if (chk->rec.data.timetodrop.tv_sec >= (long)srcv->sinfo_timetolive) {
					/*
					 * Lower numbers equates to higher
					 * priority so if the one we are
					 * looking at has a larger or equal
					 * priority we want to drop the data
					 * and NOT retransmit it.
					 */
					if (chk->data) {
						/*
						 * We release the book_size
						 * if the mbuf is here
						 */
						int ret_spc;
						int cause;

						if (chk->sent > SCTP_DATAGRAM_UNSENT)
							cause = SCTP_RESPONSE_TO_USER_REQ | SCTP_NOTIFY_DATAGRAM_SENT;
						else
							cause = SCTP_RESPONSE_TO_USER_REQ | SCTP_NOTIFY_DATAGRAM_UNSENT;
						ret_spc = sctp_release_pr_sctp_chunk(stcb, chk,
						    cause,
						    SCTP_SO_LOCKED);
						freed_spc += ret_spc;
						if (freed_spc >= dataout) {
							return;
						}
					}	/* if chunk was present */
				}	/* if of sufficent priority */
			}	/* if chunk has enabled */
		}		/* tailqforeach */

		TAILQ_FOREACH_SAFE(chk, &asoc->send_queue, sctp_next, nchk) {
			/* Here we must move to the sent queue and mark */
			if (PR_SCTP_BUF_ENABLED(chk->flags)) {
				if (chk->rec.data.timetodrop.tv_sec >= (long)srcv->sinfo_timetolive) {
					if (chk->data) {
						/*
						 * We release the book_size
						 * if the mbuf is here
						 */
						int ret_spc;

						ret_spc = sctp_release_pr_sctp_chunk(stcb, chk,
						    SCTP_RESPONSE_TO_USER_REQ | SCTP_NOTIFY_DATAGRAM_UNSENT,
						    SCTP_SO_LOCKED);

						freed_spc += ret_spc;
						if (freed_spc >= dataout) {
							return;
						}
					}	/* end if chk->data */
				}	/* end if right class */
			}	/* end if chk pr-sctp */
		}		/* tailqforeachsafe (chk) */
	}			/* if enabled in asoc */
}

int
sctp_get_frag_point(struct sctp_tcb *stcb,
    struct sctp_association *asoc)
{
	int siz, ovh;

	/*
	 * For endpoints that have both v6 and v4 addresses we must reserve
	 * room for the ipv6 header, for those that are only dealing with V4
	 * we use a larger frag point.
	 */
	if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) {
		ovh = SCTP_MED_OVERHEAD;
	} else {
		ovh = SCTP_MED_V4_OVERHEAD;
	}

	if (stcb->asoc.sctp_frag_point > asoc->smallest_mtu)
		siz = asoc->smallest_mtu - ovh;
	else
		siz = (stcb->asoc.sctp_frag_point - ovh);
	/*
	 * if (siz > (MCLBYTES-sizeof(struct sctp_data_chunk))) {
	 */
	/* A data chunk MUST fit in a cluster */
	/* siz = (MCLBYTES - sizeof(struct sctp_data_chunk)); */
	/* } */

	/* adjust for an AUTH chunk if DATA requires auth */
	if (sctp_auth_is_required_chunk(SCTP_DATA, stcb->asoc.peer_auth_chunks))
		siz -= sctp_get_auth_chunk_len(stcb->asoc.peer_hmac_id);

	if (siz % 4) {
		/* make it an even word boundary please */
		siz -= (siz % 4);
	}
	return (siz);
}

static void
sctp_set_prsctp_policy(struct sctp_stream_queue_pending *sp)
{
	sp->pr_sctp_on = 0;
	/*
	 * We assume that the user wants PR_SCTP_TTL if the user
	 * provides a positive lifetime but does not specify any
	 * PR_SCTP policy. This is a BAD assumption and causes
	 * problems at least with the U-Vancovers MPI folks. I will
	 * change this to be no policy means NO PR-SCTP.
	 */
	if (PR_SCTP_ENABLED(sp->sinfo_flags)) {
		sp->act_flags |= PR_SCTP_POLICY(sp->sinfo_flags);
		sp->pr_sctp_on = 1;
	} else {
		return;
	}
	switch (PR_SCTP_POLICY(sp->sinfo_flags)) {
	case CHUNK_FLAGS_PR_SCTP_BUF:
		/*
		 * Time to live is a priority stored in tv_sec when
		 * doing the buffer drop thing.
		 */
		sp->ts.tv_sec = sp->timetolive;
		sp->ts.tv_usec = 0;
		break;
	case CHUNK_FLAGS_PR_SCTP_TTL:
	{
		struct timeval tv;
		(void)SCTP_GETTIME_TIMEVAL(&sp->ts);
		tv.tv_sec = sp->timetolive / 1000;
		tv.tv_usec = (sp->timetolive * 1000) % 1000000;
		/* TODO sctp_constants.h needs alternative time macros when
		 *  _KERNEL is undefined.
		 */
#ifndef __FreeBSD__
		timeradd(&sp->ts, &tv, &sp->ts);
#else
		timevaladd(&sp->ts, &tv);
#endif
	}
		break;
	case CHUNK_FLAGS_PR_SCTP_RTX:
		/*
		 * Time to live is a the number or retransmissions
		 * stored in tv_sec.
		 */
		sp->ts.tv_sec = sp->timetolive;
		sp->ts.tv_usec = 0;
		break;
	default:
		SCTPDBG(SCTP_DEBUG_USRREQ1,
			"Unknown PR_SCTP policy %u.\n",
			PR_SCTP_POLICY(sp->sinfo_flags));
		break;
	}
}

static int
sctp_msg_append(struct sctp_tcb *stcb,
		struct sctp_nets *net,
		struct mbuf *m,
		struct sctp_sndrcvinfo *srcv, int hold_stcb_lock)
{
	int error = 0, holds_lock;
	struct mbuf *at;
	struct sctp_stream_queue_pending *sp = NULL;
	struct sctp_stream_out *strm;

	/* Given an mbuf chain, put it
	 * into the association send queue and
	 * place it on the wheel
	 */
	holds_lock = hold_stcb_lock;
	if (srcv->sinfo_stream >= stcb->asoc.streamoutcnt) {
		/* Invalid stream number */
		SCTP_LTRACE_ERR_RET_PKT(m, NULL, stcb, net, SCTP_FROM_SCTP_OUTPUT, EINVAL);
		error = EINVAL;
		goto out_now;
	}
	if ((stcb->asoc.stream_locked) &&
	    (stcb->asoc.stream_locked_on  != srcv->sinfo_stream)) {
		SCTP_LTRACE_ERR_RET_PKT(m, NULL, stcb, net, SCTP_FROM_SCTP_OUTPUT, EINVAL);
		error = EINVAL;
		goto out_now;
	}
	strm = &stcb->asoc.strmout[srcv->sinfo_stream];
	/* Now can we send this? */
	if ((SCTP_GET_STATE(&stcb->asoc) == SCTP_STATE_SHUTDOWN_SENT) ||
	    (SCTP_GET_STATE(&stcb->asoc) == SCTP_STATE_SHUTDOWN_ACK_SENT) ||
	    (SCTP_GET_STATE(&stcb->asoc) == SCTP_STATE_SHUTDOWN_RECEIVED) ||
	    (stcb->asoc.state & SCTP_STATE_SHUTDOWN_PENDING)) {
		/* got data while shutting down */
		SCTP_LTRACE_ERR_RET(NULL, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, ECONNRESET);
		error = ECONNRESET;
		goto out_now;
	}
	sctp_alloc_a_strmoq(stcb, sp);
	if (sp == NULL) {
		SCTP_LTRACE_ERR_RET(NULL, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, ENOMEM);
		error = ENOMEM;
		goto out_now;
	}
	sp->sinfo_flags = srcv->sinfo_flags;
	sp->timetolive = srcv->sinfo_timetolive;
	sp->ppid = srcv->sinfo_ppid;
	sp->context = srcv->sinfo_context;
	sp->strseq = 0;
	if (sp->sinfo_flags & SCTP_ADDR_OVER) {
		sp->net = net;
		atomic_add_int(&sp->net->ref_count, 1);
	} else {
		sp->net = NULL;
	}
	(void)SCTP_GETTIME_TIMEVAL(&sp->ts);
	sp->stream = srcv->sinfo_stream;
	sp->msg_is_complete = 1;
	sp->sender_all_done = 1;
	sp->some_taken = 0;
	sp->data = m;
	sp->tail_mbuf = NULL;
	sctp_set_prsctp_policy(sp);
	/* We could in theory (for sendall) sifa the length
	 * in, but we would still have to hunt through the
	 * chain since we need to setup the tail_mbuf
	 */
	sp->length = 0;
	for (at = m; at; at = SCTP_BUF_NEXT(at)) {
		if (SCTP_BUF_NEXT(at) == NULL)
			sp->tail_mbuf = at;
		sp->length += SCTP_BUF_LEN(at);
	}
	if (srcv->sinfo_keynumber_valid) {
		sp->auth_keyid = srcv->sinfo_keynumber;
	} else {
		sp->auth_keyid = stcb->asoc.authinfo.active_keyid;
	}
	if (sctp_auth_is_required_chunk(SCTP_DATA, stcb->asoc.peer_auth_chunks)) {
		sctp_auth_key_acquire(stcb, sp->auth_keyid);
		sp->holds_key_ref = 1;
	}
	SCTP_TCB_SEND_LOCK(stcb);
	sctp_snd_sb_alloc(stcb, sp->length);
	atomic_add_int(&stcb->asoc.stream_queue_cnt, 1);
	TAILQ_INSERT_TAIL(&strm->outqueue, sp, next);
	if ((srcv->sinfo_flags & SCTP_UNORDERED) == 0) {
		sp->strseq = strm->next_sequence_sent;
		strm->next_sequence_sent++;
	}

	stcb->asoc.ss_functions.sctp_ss_add_to_stream(stcb, &stcb->asoc, strm, sp, 1);
	m = NULL;
	SCTP_TCB_SEND_UNLOCK(stcb);
out_now:
	if (m) {
		sctp_m_freem(m);
	}
	return (error);
}


static struct mbuf *
sctp_copy_mbufchain(struct mbuf *clonechain,
		    struct mbuf *outchain,
		    struct mbuf **endofchain,
		    int can_take_mbuf,
		    int sizeofcpy,
		    uint8_t copy_by_ref)
{
	struct mbuf *m;
	struct mbuf *appendchain;
	caddr_t cp;
	int len;

	if (endofchain == NULL) {
		/* error */
	error_out:
		if (outchain)
			sctp_m_freem(outchain);
		return (NULL);
	}
	if (can_take_mbuf) {
		appendchain = clonechain;
	} else {
		if (!copy_by_ref &&
#if defined(__Panda__)
		    0
#else
		    (sizeofcpy <= (int)((((SCTP_BASE_SYSCTL(sctp_mbuf_threshold_count) - 1) * MLEN) + MHLEN)))
#endif
		    ) {
			/* Its not in a cluster */
			if (*endofchain == NULL) {
				/* lets get a mbuf cluster */
				if (outchain == NULL) {
					/* This is the general case */
				new_mbuf:
					outchain = sctp_get_mbuf_for_msg(MCLBYTES, 0, M_DONTWAIT, 1, MT_HEADER);
					if (outchain == NULL) {
						goto error_out;
					}
					SCTP_BUF_LEN(outchain) = 0;
					*endofchain = outchain;
					/* get the prepend space */
					SCTP_BUF_RESV_UF(outchain, (SCTP_FIRST_MBUF_RESV+4));
				} else {
					/* We really should not get a NULL in endofchain */
					/* find end */
					m = outchain;
					while (m) {
						if (SCTP_BUF_NEXT(m) == NULL) {
							*endofchain = m;
							break;
						}
						m = SCTP_BUF_NEXT(m);
					}
					/* sanity */
					if (*endofchain == NULL) {
						/* huh, TSNH XXX maybe we should panic */
						sctp_m_freem(outchain);
						goto new_mbuf;
					}
				}
				/* get the new end of length */
				len = M_TRAILINGSPACE(*endofchain);
			} else {
				/* how much is left at the end? */
				len = M_TRAILINGSPACE(*endofchain);
			}
			/* Find the end of the data, for appending */
			cp = (mtod((*endofchain), caddr_t) + SCTP_BUF_LEN((*endofchain)));

			/* Now lets copy it out */
			if (len >= sizeofcpy) {
				/* It all fits, copy it in */
				m_copydata(clonechain, 0, sizeofcpy, cp);
				SCTP_BUF_LEN((*endofchain)) += sizeofcpy;
			} else {
				/* fill up the end of the chain */
				if (len > 0) {
					m_copydata(clonechain, 0, len, cp);
					SCTP_BUF_LEN((*endofchain)) += len;
					/* now we need another one */
					sizeofcpy -= len;
				}
				m = sctp_get_mbuf_for_msg(MCLBYTES, 0, M_DONTWAIT, 1, MT_HEADER);
				if (m == NULL) {
					/* We failed */
					goto error_out;
				}
				SCTP_BUF_NEXT((*endofchain)) = m;
				*endofchain = m;
				cp = mtod((*endofchain), caddr_t);
				m_copydata(clonechain, len, sizeofcpy, cp);
				SCTP_BUF_LEN((*endofchain)) += sizeofcpy;
			}
			return(outchain);
		} else {
			/* copy the old fashion way */
			appendchain = SCTP_M_COPYM(clonechain, 0, M_COPYALL, M_DONTWAIT);
#ifdef SCTP_MBUF_LOGGING
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MBUF_LOGGING_ENABLE) {
				struct mbuf *mat;
				mat = appendchain;
				while (mat) {
					if (SCTP_BUF_IS_EXTENDED(mat)) {
						sctp_log_mb(mat, SCTP_MBUF_ICOPY);
					}
					mat = SCTP_BUF_NEXT(mat);
				}
			}
#endif
		}
	}
	if (appendchain == NULL) {
		/* error */
		if (outchain)
			sctp_m_freem(outchain);
		return (NULL);
	}
	if (outchain) {
		/* tack on to the end */
		if (*endofchain != NULL) {
			SCTP_BUF_NEXT(((*endofchain))) = appendchain;
		} else {
			m = outchain;
			while (m) {
				if (SCTP_BUF_NEXT(m) == NULL) {
					SCTP_BUF_NEXT(m) = appendchain;
					break;
				}
				m = SCTP_BUF_NEXT(m);
			}
		}
		/*
		 * save off the end and update the end-chain
		 * postion
		 */
		m = appendchain;
		while (m) {
			if (SCTP_BUF_NEXT(m) == NULL) {
				*endofchain = m;
				break;
			}
			m = SCTP_BUF_NEXT(m);
		}
		return (outchain);
	} else {
		/* save off the end and update the end-chain postion */
		m = appendchain;
		while (m) {
			if (SCTP_BUF_NEXT(m) == NULL) {
				*endofchain = m;
				break;
			}
			m = SCTP_BUF_NEXT(m);
		}
		return (appendchain);
	}
}

int
sctp_med_chunk_output(struct sctp_inpcb *inp,
		      struct sctp_tcb *stcb,
		      struct sctp_association *asoc,
		      int *num_out,
		      int *reason_code,
		      int control_only, int from_where,
		      struct timeval *now, int *now_filled, int frag_point, int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
		      SCTP_UNUSED
#endif
                      );

static void
sctp_sendall_iterator(struct sctp_inpcb *inp, struct sctp_tcb *stcb, void *ptr,
    uint32_t val)
{
	struct sctp_copy_all *ca;
	struct mbuf *m;
	int ret=0;
	int added_control=0;
	int un_sent, do_chunk_output=1;
	struct sctp_association *asoc;
	struct sctp_nets *net;

	ca = (struct sctp_copy_all *)ptr;
	if (ca->m == NULL) {
		return;
	}
	if (ca->inp != inp) {
		/* TSNH */
		return;
	}
	if ((ca->m) && ca->sndlen) {
		m = SCTP_M_COPYM(ca->m, 0, M_COPYALL, M_DONTWAIT);
		if (m == NULL) {
			/* can't copy so we are done */
			ca->cnt_failed++;
			return;
		}
#ifdef SCTP_MBUF_LOGGING
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MBUF_LOGGING_ENABLE) {
			struct mbuf *mat;
			mat = m;
			while (mat) {
				if (SCTP_BUF_IS_EXTENDED(mat)) {
					sctp_log_mb(mat, SCTP_MBUF_ICOPY);
				}
				mat = SCTP_BUF_NEXT(mat);
			}
		}
#endif
	} else {
		m = NULL;
	}
	SCTP_TCB_LOCK_ASSERT(stcb);
	if (stcb->asoc.alternate) {
		net = stcb->asoc.alternate;
	} else {
		net = stcb->asoc.primary_destination;
	}
	if (ca->sndrcv.sinfo_flags & SCTP_ABORT) {
		/* Abort this assoc with m as the user defined reason */
		if (m) {
			struct sctp_paramhdr *ph;

			SCTP_BUF_PREPEND(m, sizeof(struct sctp_paramhdr), M_DONTWAIT);
			if (m) {
				ph = mtod(m, struct sctp_paramhdr *);
				ph->param_type = htons(SCTP_CAUSE_USER_INITIATED_ABT);
				ph->param_length = htons(ca->sndlen);
			}
			/* We add one here to keep the assoc from
			 * dis-appearing on us.
			 */
			atomic_add_int(&stcb->asoc.refcnt, 1);
			sctp_abort_an_association(inp, stcb,
						  SCTP_RESPONSE_TO_USER_REQ,
						  m, SCTP_SO_NOT_LOCKED);
			/* sctp_abort_an_association calls sctp_free_asoc()
			 * free association will NOT free it since we
			 * incremented the refcnt .. we do this to prevent
			 * it being freed and things getting tricky since
			 * we could end up (from free_asoc) calling inpcb_free
			 * which would get a recursive lock call to the
			 * iterator lock.. But as a consequence of that the
			 * stcb will return to us un-locked.. since free_asoc
			 * returns with either no TCB or the TCB unlocked, we
			 * must relock.. to unlock in the iterator timer :-0
			 */
			SCTP_TCB_LOCK(stcb);
			atomic_add_int(&stcb->asoc.refcnt, -1);
			goto no_chunk_output;
		}
	} else {
		if (m) {
			ret = sctp_msg_append(stcb, net, m,
					      &ca->sndrcv, 1);
		}
		asoc = &stcb->asoc;
		if (ca->sndrcv.sinfo_flags & SCTP_EOF) {
			/* shutdown this assoc */
			int cnt;
			cnt = sctp_is_there_unsent_data(stcb, SCTP_SO_NOT_LOCKED);

			if (TAILQ_EMPTY(&asoc->send_queue) &&
			    TAILQ_EMPTY(&asoc->sent_queue) &&
			    (cnt == 0)) {
				if (asoc->locked_on_sending) {
					goto abort_anyway;
				}
				/* there is nothing queued to send, so I'm done... */
				if ((SCTP_GET_STATE(asoc) != SCTP_STATE_SHUTDOWN_SENT) &&
				    (SCTP_GET_STATE(asoc) != SCTP_STATE_SHUTDOWN_RECEIVED) &&
				    (SCTP_GET_STATE(asoc) != SCTP_STATE_SHUTDOWN_ACK_SENT)) {
					/* only send SHUTDOWN the first time through */
					sctp_send_shutdown(stcb, net);
					if (SCTP_GET_STATE(asoc) == SCTP_STATE_OPEN) {
						SCTP_STAT_DECR_GAUGE32(sctps_currestab);
					}
					SCTP_SET_STATE(asoc, SCTP_STATE_SHUTDOWN_SENT);
					SCTP_CLEAR_SUBSTATE(asoc, SCTP_STATE_SHUTDOWN_PENDING);
					sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWN, stcb->sctp_ep, stcb,
							 net);
					sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWNGUARD, stcb->sctp_ep, stcb,
							 asoc->primary_destination);
					added_control = 1;
					do_chunk_output = 0;
				}
			} else {
				/*
				 * we still got (or just got) data to send, so set
				 * SHUTDOWN_PENDING
				 */
				/*
				 * XXX sockets draft says that SCTP_EOF should be
				 * sent with no data.  currently, we will allow user
				 * data to be sent first and move to
				 * SHUTDOWN-PENDING
				 */
				if ((SCTP_GET_STATE(asoc) != SCTP_STATE_SHUTDOWN_SENT) &&
				    (SCTP_GET_STATE(asoc) != SCTP_STATE_SHUTDOWN_RECEIVED) &&
				    (SCTP_GET_STATE(asoc) != SCTP_STATE_SHUTDOWN_ACK_SENT)) {
					if (asoc->locked_on_sending) {
						/* Locked to send out the data */
						struct sctp_stream_queue_pending *sp;
						sp = TAILQ_LAST(&asoc->locked_on_sending->outqueue, sctp_streamhead);
						if (sp) {
							if ((sp->length == 0) && (sp->msg_is_complete == 0))
								asoc->state |= SCTP_STATE_PARTIAL_MSG_LEFT;
						}
					}
					asoc->state |= SCTP_STATE_SHUTDOWN_PENDING;
					if (TAILQ_EMPTY(&asoc->send_queue) &&
					    TAILQ_EMPTY(&asoc->sent_queue) &&
					    (asoc->state & SCTP_STATE_PARTIAL_MSG_LEFT)) {
					abort_anyway:
						atomic_add_int(&stcb->asoc.refcnt, 1);
						sctp_abort_an_association(stcb->sctp_ep, stcb,
									  SCTP_RESPONSE_TO_USER_REQ,
									  NULL, SCTP_SO_NOT_LOCKED);
						atomic_add_int(&stcb->asoc.refcnt, -1);
						goto no_chunk_output;
					}
					sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWNGUARD, stcb->sctp_ep, stcb,
							 asoc->primary_destination);
				}
			}

		}
	}
	un_sent = ((stcb->asoc.total_output_queue_size - stcb->asoc.total_flight) +
		   (stcb->asoc.stream_queue_cnt * sizeof(struct sctp_data_chunk)));

	if ((sctp_is_feature_off(inp, SCTP_PCB_FLAGS_NODELAY)) &&
	    (stcb->asoc.total_flight > 0) &&
	    (un_sent < (int)(stcb->asoc.smallest_mtu - SCTP_MIN_OVERHEAD))
	    ) {
		do_chunk_output = 0;
	}
	if (do_chunk_output)
		sctp_chunk_output(inp, stcb, SCTP_OUTPUT_FROM_USR_SEND, SCTP_SO_NOT_LOCKED);
	else if (added_control) {
		int num_out=0, reason=0, now_filled=0;
		struct timeval now;
		int frag_point;
		frag_point = sctp_get_frag_point(stcb, &stcb->asoc);
		(void)sctp_med_chunk_output(inp, stcb, &stcb->asoc, &num_out,
				      &reason, 1, 1, &now, &now_filled, frag_point, SCTP_SO_NOT_LOCKED);
	}
 no_chunk_output:
	if (ret) {
		ca->cnt_failed++;
	} else {
		ca->cnt_sent++;
	}
}

static void
sctp_sendall_completes(void *ptr, uint32_t val)
{
	struct sctp_copy_all *ca;

	ca = (struct sctp_copy_all *)ptr;
	/*
	 * Do a notify here? Kacheong suggests that the notify be done at
	 * the send time.. so you would push up a notification if any send
	 * failed. Don't know if this is feasable since the only failures we
	 * have is "memory" related and if you cannot get an mbuf to send
	 * the data you surely can't get an mbuf to send up to notify the
	 * user you can't send the data :->
	 */

	/* now free everything */
	sctp_m_freem(ca->m);
	SCTP_FREE(ca, SCTP_M_COPYAL);
}


#define	MC_ALIGN(m, len) do {						\
	SCTP_BUF_RESV_UF(m, ((MCLBYTES - (len)) & ~(sizeof(long) - 1));	\
} while (0)



static struct mbuf *
sctp_copy_out_all(struct uio *uio, int len)
{
	struct mbuf *ret, *at;
	int left, willcpy, cancpy, error;

	ret = sctp_get_mbuf_for_msg(MCLBYTES, 0, M_WAIT, 1, MT_DATA);
	if (ret == NULL) {
		/* TSNH */
		return (NULL);
	}
	left = len;
	SCTP_BUF_LEN(ret) = 0;
	/* save space for the data chunk header */
	cancpy = M_TRAILINGSPACE(ret);
	willcpy = min(cancpy, left);
	at = ret;
	while (left > 0) {
		/* Align data to the end */
		error = uiomove(mtod(at, caddr_t), willcpy, uio);
		if (error) {
	err_out_now:
			sctp_m_freem(at);
			return (NULL);
		}
		SCTP_BUF_LEN(at) = willcpy;
		SCTP_BUF_NEXT_PKT(at) = SCTP_BUF_NEXT(at) = 0;
		left -= willcpy;
		if (left > 0) {
			SCTP_BUF_NEXT(at) = sctp_get_mbuf_for_msg(left, 0, M_WAIT, 1, MT_DATA);
			if (SCTP_BUF_NEXT(at) == NULL) {
				goto err_out_now;
			}
			at = SCTP_BUF_NEXT(at);
			SCTP_BUF_LEN(at) = 0;
			cancpy = M_TRAILINGSPACE(at);
			willcpy = min(cancpy, left);
		}
	}
	return (ret);
}

static int
sctp_sendall(struct sctp_inpcb *inp, struct uio *uio, struct mbuf *m,
    struct sctp_sndrcvinfo *srcv)
{
	int ret;
	struct sctp_copy_all *ca;

	SCTP_MALLOC(ca, struct sctp_copy_all *, sizeof(struct sctp_copy_all),
		    SCTP_M_COPYAL);
	if (ca == NULL) {
		sctp_m_freem(m);
		SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_OUTPUT, ENOMEM);
		return (ENOMEM);
	}
	memset(ca, 0, sizeof(struct sctp_copy_all));

	ca->inp = inp;
	if (srcv) {
		memcpy(&ca->sndrcv, srcv, sizeof(struct sctp_nonpad_sndrcvinfo));
	}
	/*
	 * take off the sendall flag, it would be bad if we failed to do
	 * this :-0
	 */
	ca->sndrcv.sinfo_flags &= ~SCTP_SENDALL;
	/* get length and mbuf chain */
	if (uio) {
#if defined(__APPLE__)
#if defined(APPLE_SNOWLEOPARD) || defined(APPLE_LION)
		ca->sndlen = uio_resid(uio);
#else
		ca->sndlen = uio->uio_resid;
#endif
#else
		ca->sndlen = uio->uio_resid;
#endif
#if defined(__APPLE__)
		SCTP_SOCKET_UNLOCK(SCTP_INP_SO(inp), 0);
#endif
		ca->m = sctp_copy_out_all(uio, ca->sndlen);
#if defined(__APPLE__)
		SCTP_SOCKET_LOCK(SCTP_INP_SO(inp), 0);
#endif
		if (ca->m == NULL) {
			SCTP_FREE(ca, SCTP_M_COPYAL);
			SCTP_LTRACE_ERR_RET(inp, NULL, NULL, SCTP_FROM_SCTP_OUTPUT, ENOMEM);
			return (ENOMEM);
		}
	} else {
		/* Gather the length of the send */
		struct mbuf *mat;

		mat = m;
		ca->sndlen = 0;
		while (m) {
			ca->sndlen += SCTP_BUF_LEN(m);
			m = SCTP_BUF_NEXT(m);
		}
		ca->m = mat;
	}
	ret = sctp_initiate_iterator(NULL, sctp_sendall_iterator, NULL,
				     SCTP_PCB_ANY_FLAGS, SCTP_PCB_ANY_FEATURES,
				     SCTP_ASOC_ANY_STATE,
				     (void *)ca, 0,
				     sctp_sendall_completes, inp, 1);
	if (ret) {
		SCTP_PRINTF("Failed to initiate iterator for sendall\n");
		SCTP_FREE(ca, SCTP_M_COPYAL);
		SCTP_LTRACE_ERR_RET_PKT(m, inp, NULL, NULL, SCTP_FROM_SCTP_OUTPUT, EFAULT);
		return (EFAULT);
	}
	return (0);
}


void
sctp_toss_old_cookies(struct sctp_tcb *stcb, struct sctp_association *asoc)
{
	struct sctp_tmit_chunk *chk, *nchk;

	TAILQ_FOREACH_SAFE(chk, &asoc->control_send_queue, sctp_next, nchk) {
		if (chk->rec.chunk_id.id == SCTP_COOKIE_ECHO) {
			TAILQ_REMOVE(&asoc->control_send_queue, chk, sctp_next);
			if (chk->data) {
				sctp_m_freem(chk->data);
				chk->data = NULL;
			}
			asoc->ctrl_queue_cnt--;
			sctp_free_a_chunk(stcb, chk, SCTP_SO_NOT_LOCKED);
		}
	}
}

void
sctp_toss_old_asconf(struct sctp_tcb *stcb)
{
	struct sctp_association *asoc;
	struct sctp_tmit_chunk *chk, *nchk;
	struct sctp_asconf_chunk *acp;

	asoc = &stcb->asoc;
	TAILQ_FOREACH_SAFE(chk, &asoc->asconf_send_queue, sctp_next, nchk) {
		/* find SCTP_ASCONF chunk in queue */
		if (chk->rec.chunk_id.id == SCTP_ASCONF) {
			if (chk->data) {
				acp = mtod(chk->data, struct sctp_asconf_chunk *);
				if (SCTP_TSN_GT(ntohl(acp->serial_number), asoc->asconf_seq_out_acked)) {
					/* Not Acked yet */
					break;
				}
			}
			TAILQ_REMOVE(&asoc->asconf_send_queue, chk, sctp_next);
			if (chk->data) {
				sctp_m_freem(chk->data);
				chk->data = NULL;
			}
			asoc->ctrl_queue_cnt--;
			sctp_free_a_chunk(stcb, chk, SCTP_SO_NOT_LOCKED);
		}
	}
}


static void
sctp_clean_up_datalist(struct sctp_tcb *stcb,
    struct sctp_association *asoc,
    struct sctp_tmit_chunk **data_list,
    int bundle_at,
    struct sctp_nets *net)
{
	int i;
	struct sctp_tmit_chunk *tp1;

	for (i = 0; i < bundle_at; i++) {
		/* off of the send queue */
		TAILQ_REMOVE(&asoc->send_queue, data_list[i], sctp_next);
		asoc->send_queue_cnt--;
		if (i > 0) {
			/*
			 * Any chunk NOT 0 you zap the time chunk 0 gets
			 * zapped or set based on if a RTO measurment is
			 * needed.
			 */
			data_list[i]->do_rtt = 0;
		}
		/* record time */
		data_list[i]->sent_rcv_time = net->last_sent_time;
		data_list[i]->rec.data.cwnd_at_send = net->cwnd;
		data_list[i]->rec.data.fast_retran_tsn = data_list[i]->rec.data.TSN_seq;
		if (data_list[i]->whoTo == NULL) {
			data_list[i]->whoTo = net;
			atomic_add_int(&net->ref_count, 1);
		}
		/* on to the sent queue */
		tp1 = TAILQ_LAST(&asoc->sent_queue, sctpchunk_listhead);
		if ((tp1) && SCTP_TSN_GT(tp1->rec.data.TSN_seq, data_list[i]->rec.data.TSN_seq)) {
			struct sctp_tmit_chunk *tpp;

			/* need to move back */
		back_up_more:
			tpp = TAILQ_PREV(tp1, sctpchunk_listhead, sctp_next);
			if (tpp == NULL) {
				TAILQ_INSERT_BEFORE(tp1, data_list[i], sctp_next);
				goto all_done;
			}
			tp1 = tpp;
			if (SCTP_TSN_GT(tp1->rec.data.TSN_seq, data_list[i]->rec.data.TSN_seq)) {
				goto back_up_more;
			}
			TAILQ_INSERT_AFTER(&asoc->sent_queue, tp1, data_list[i], sctp_next);
		} else {
			TAILQ_INSERT_TAIL(&asoc->sent_queue,
					  data_list[i],
					  sctp_next);
		}
	all_done:
		/* This does not lower until the cum-ack passes it */
		asoc->sent_queue_cnt++;
		if ((asoc->peers_rwnd <= 0) &&
		    (asoc->total_flight == 0) &&
		    (bundle_at == 1)) {
			/* Mark the chunk as being a window probe */
			SCTP_STAT_INCR(sctps_windowprobed);
		}
#ifdef SCTP_AUDITING_ENABLED
		sctp_audit_log(0xC2, 3);
#endif
		data_list[i]->sent = SCTP_DATAGRAM_SENT;
		data_list[i]->snd_count = 1;
		data_list[i]->rec.data.chunk_was_revoked = 0;
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_FLIGHT_LOGGING_ENABLE) {
			sctp_misc_ints(SCTP_FLIGHT_LOG_UP,
				       data_list[i]->whoTo->flight_size,
				       data_list[i]->book_size,
				       (uintptr_t)data_list[i]->whoTo,
				       data_list[i]->rec.data.TSN_seq);
		}
		sctp_flight_size_increase(data_list[i]);
		sctp_total_flight_increase(stcb, data_list[i]);
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LOG_RWND_ENABLE) {
			sctp_log_rwnd(SCTP_DECREASE_PEER_RWND,
			      asoc->peers_rwnd, data_list[i]->send_size, SCTP_BASE_SYSCTL(sctp_peer_chunk_oh));
		}
		asoc->peers_rwnd = sctp_sbspace_sub(asoc->peers_rwnd,
						    (uint32_t) (data_list[i]->send_size + SCTP_BASE_SYSCTL(sctp_peer_chunk_oh)));
		if (asoc->peers_rwnd < stcb->sctp_ep->sctp_ep.sctp_sws_sender) {
			/* SWS sender side engages */
			asoc->peers_rwnd = 0;
		}
	}
	if (asoc->cc_functions.sctp_cwnd_update_packet_transmitted) {
		(*asoc->cc_functions.sctp_cwnd_update_packet_transmitted)(stcb, net);
	}
}

static void
sctp_clean_up_ctl(struct sctp_tcb *stcb, struct sctp_association *asoc, int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
	SCTP_UNUSED
#endif
)
{
	struct sctp_tmit_chunk *chk, *nchk;

	TAILQ_FOREACH_SAFE(chk, &asoc->control_send_queue, sctp_next, nchk) {
		if ((chk->rec.chunk_id.id == SCTP_SELECTIVE_ACK) ||
		    (chk->rec.chunk_id.id == SCTP_NR_SELECTIVE_ACK) ||	/* EY */
		    (chk->rec.chunk_id.id == SCTP_HEARTBEAT_REQUEST) ||
		    (chk->rec.chunk_id.id == SCTP_HEARTBEAT_ACK) ||
		    (chk->rec.chunk_id.id == SCTP_FORWARD_CUM_TSN) ||
		    (chk->rec.chunk_id.id == SCTP_SHUTDOWN) ||
		    (chk->rec.chunk_id.id == SCTP_SHUTDOWN_ACK) ||
		    (chk->rec.chunk_id.id == SCTP_OPERATION_ERROR) ||
		    (chk->rec.chunk_id.id == SCTP_PACKET_DROPPED) ||
		    (chk->rec.chunk_id.id == SCTP_COOKIE_ACK) ||
		    (chk->rec.chunk_id.id == SCTP_ECN_CWR) ||
		    (chk->rec.chunk_id.id == SCTP_ASCONF_ACK)) {
			/* Stray chunks must be cleaned up */
	clean_up_anyway:
			TAILQ_REMOVE(&asoc->control_send_queue, chk, sctp_next);
			if (chk->data) {
				sctp_m_freem(chk->data);
				chk->data = NULL;
			}
			asoc->ctrl_queue_cnt--;
			if (chk->rec.chunk_id.id == SCTP_FORWARD_CUM_TSN)
				asoc->fwd_tsn_cnt--;
			sctp_free_a_chunk(stcb, chk, so_locked);
		} else if (chk->rec.chunk_id.id == SCTP_STREAM_RESET) {
			/* special handling, we must look into the param */
			if (chk != asoc->str_reset) {
				goto clean_up_anyway;
			}
		}
	}
}


static int
sctp_can_we_split_this(struct sctp_tcb *stcb,
					   uint32_t length,
		       uint32_t goal_mtu, uint32_t frag_point, int eeor_on)
{
	/* Make a decision on if I should split a
	 * msg into multiple parts. This is only asked of
	 * incomplete messages.
	 */
	if (eeor_on) {
		/* If we are doing EEOR we need to always send
		 * it if its the entire thing, since it might
		 * be all the guy is putting in the hopper.
		 */
		if (goal_mtu >= length) {
			/*-
			 * If we have data outstanding,
			 * we get another chance when the sack
			 * arrives to transmit - wait for more data
			 */
			if (stcb->asoc.total_flight == 0) {
				/* If nothing is in flight, we zero
				 * the packet counter.
				 */
				return (length);
			}
			return (0);

		} else {
			/* You can fill the rest */
			return (goal_mtu);
		}
	}
	/*-
	 * For those strange folk that make the send buffer
	 * smaller than our fragmentation point, we can't
	 * get a full msg in so we have to allow splitting.
	 */
	if (SCTP_SB_LIMIT_SND(stcb->sctp_socket) < frag_point) {
		return(length);
	}

	if ((length <= goal_mtu) ||
	    ((length - goal_mtu) < SCTP_BASE_SYSCTL(sctp_min_residual))) {
		/* Sub-optimial residual don't split in non-eeor mode. */
		return(0);
	}
	/* If we reach here length is larger
	 * than the goal_mtu. Do we wish to split
	 * it for the sake of packet putting together?
	 */
	if (goal_mtu >= min(SCTP_BASE_SYSCTL(sctp_min_split_point), frag_point)) {
		/* Its ok to split it */
		return(min(goal_mtu, frag_point));
	}
	/* Nope, can't split */
	return(0);

}

static uint32_t
sctp_move_to_outqueue(struct sctp_tcb *stcb,
	struct sctp_stream_out *strq,
	uint32_t goal_mtu,
	uint32_t frag_point,
	int *locked,
        int *giveup,
	int eeor_mode,
        int *bail,
	int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
	SCTP_UNUSED
#endif
)
{
	/* Move from the stream to the send_queue keeping track of the total */
	struct sctp_association *asoc;
	struct sctp_stream_queue_pending *sp;
	struct sctp_tmit_chunk *chk;
	struct sctp_data_chunk *dchkh;
	uint32_t to_move, length;
	uint8_t rcv_flags = 0;
	uint8_t some_taken;
	uint8_t send_lock_up = 0;

	SCTP_TCB_LOCK_ASSERT(stcb);
	asoc = &stcb->asoc;
 one_more_time:
	/*sa_ignore FREED_MEMORY*/
	sp = TAILQ_FIRST(&strq->outqueue);
	if (sp == NULL) {
		*locked = 0;
		if (send_lock_up == 0) {
			SCTP_TCB_SEND_LOCK(stcb);
			send_lock_up = 1;
		}
		sp = TAILQ_FIRST(&strq->outqueue);
		if (sp) {
			goto one_more_time;
		}
		if (strq->last_msg_incomplete) {
			SCTP_PRINTF("Huh? Stream:%d lm_in_c=%d but queue is NULL\n",
			            strq->stream_no,
			            strq->last_msg_incomplete);
			strq->last_msg_incomplete = 0;
		}
		to_move = 0;
		if (send_lock_up) {
			SCTP_TCB_SEND_UNLOCK(stcb);
			send_lock_up = 0;
		}
		goto out_of;
	}
	if ((sp->msg_is_complete) && (sp->length == 0)) {
		if (sp->sender_all_done) {
			/* We are doing differed cleanup. Last
			 * time through when we took all the data
			 * the sender_all_done was not set.
			 */
			if ((sp->put_last_out == 0) && (sp->discard_rest == 0)) {
				SCTP_PRINTF("Gak, put out entire msg with NO end!-1\n");
				SCTP_PRINTF("sender_done:%d len:%d msg_comp:%d put_last_out:%d send_lock:%d\n",
				             sp->sender_all_done,
				             sp->length,
				             sp->msg_is_complete,
				             sp->put_last_out,
				             send_lock_up);
			}
			if ((TAILQ_NEXT(sp, next) == NULL) && (send_lock_up  == 0)) {
				SCTP_TCB_SEND_LOCK(stcb);
				send_lock_up = 1;
			}
			atomic_subtract_int(&asoc->stream_queue_cnt, 1);
			TAILQ_REMOVE(&strq->outqueue, sp, next);
			stcb->asoc.ss_functions.sctp_ss_remove_from_stream(stcb, asoc, strq, sp, send_lock_up);
			if (sp->net) {
				sctp_free_remote_addr(sp->net);
				sp->net = NULL;
			}
			if (sp->data) {
				sctp_m_freem(sp->data);
				sp->data = NULL;
			}
			sctp_free_a_strmoq(stcb, sp, so_locked);
			/* we can't be locked to it */
			*locked = 0;
			stcb->asoc.locked_on_sending = NULL;
			if (send_lock_up) {
				SCTP_TCB_SEND_UNLOCK(stcb);
				send_lock_up = 0;
			}
			/* back to get the next msg */
			goto one_more_time;
		} else {
			/* sender just finished this but
			 * still holds a reference
			 */
			*locked = 1;
			*giveup = 1;
			to_move = 0;
			goto out_of;
		}
	} else {
		/* is there some to get */
		if (sp->length == 0) {
			/* no */
			*locked = 1;
			*giveup = 1;
			to_move = 0;
			goto out_of;
		} else if (sp->discard_rest) {
			if (send_lock_up == 0) {
				SCTP_TCB_SEND_LOCK(stcb);
				send_lock_up = 1;
			}
			/* Whack down the size */
			atomic_subtract_int(&stcb->asoc.total_output_queue_size, sp->length);
			if ((stcb->sctp_socket != NULL) &&	     \
			    ((stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
			     (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL))) {
				atomic_subtract_int(&stcb->sctp_socket->so_snd.sb_cc, sp->length);
			}
			if (sp->data) {
				sctp_m_freem(sp->data);
				sp->data = NULL;
				sp->tail_mbuf = NULL;
			}
			sp->length = 0;
			sp->some_taken = 1;
			*locked = 1;
			*giveup = 1;
			to_move = 0;
			goto out_of;
		}
	}
	some_taken = sp->some_taken;
	if (stcb->asoc.state & SCTP_STATE_CLOSED_SOCKET) {
		sp->msg_is_complete = 1;
	}
 re_look:
	length = sp->length;
	if (sp->msg_is_complete) {
		/* The message is complete */
		to_move = min(length, frag_point);
		if (to_move == length) {
			/* All of it fits in the MTU */
			if (sp->some_taken)  {
				rcv_flags |= SCTP_DATA_LAST_FRAG;
				sp->put_last_out = 1;
			} else {
				rcv_flags |= SCTP_DATA_NOT_FRAG;
				sp->put_last_out = 1;
			}
		} else {
			/* Not all of it fits, we fragment */
			if (sp->some_taken == 0) {
				rcv_flags |= SCTP_DATA_FIRST_FRAG;
			}
			sp->some_taken = 1;
		}
	} else {
		to_move = sctp_can_we_split_this(stcb, length, goal_mtu, frag_point, eeor_mode);
		if (to_move) {
			/*-
			 * We use a snapshot of length in case it
			 * is expanding during the compare.
			 */
			uint32_t llen;
			
			llen = length;
			if (to_move >= llen) {
				to_move = llen;
				if (send_lock_up == 0) {
					/*-
					 * We are taking all of an incomplete msg
					 * thus we need a send lock.
					 */
					SCTP_TCB_SEND_LOCK(stcb);
					send_lock_up = 1;
					if (sp->msg_is_complete) {
						/* the sender finished the msg */
						goto re_look;
					}
				}
			}
			if (sp->some_taken == 0) {
				rcv_flags |= SCTP_DATA_FIRST_FRAG;
				sp->some_taken = 1;
			}
		} else {
			/* Nothing to take. */
			if (sp->some_taken) {
				*locked = 1;
			}
			*giveup = 1;
			to_move = 0;
			goto out_of;
		}
	}

	/* If we reach here, we can copy out a chunk */
	sctp_alloc_a_chunk(stcb, chk);
	if (chk == NULL) {
		/* No chunk memory */
		*giveup = 1;
		to_move = 0;
		goto out_of;
	}
	/* Setup for unordered if needed by looking
	 * at the user sent info flags.
	 */
	if (sp->sinfo_flags & SCTP_UNORDERED) {
		rcv_flags |= SCTP_DATA_UNORDERED;
	}
	if ((SCTP_BASE_SYSCTL(sctp_enable_sack_immediately) && ((sp->sinfo_flags & SCTP_EOF) == SCTP_EOF)) ||
	    ((sp->sinfo_flags & SCTP_SACK_IMMEDIATELY) == SCTP_SACK_IMMEDIATELY)) {
		rcv_flags |= SCTP_DATA_SACK_IMMEDIATELY;
	}
	/* clear out the chunk before setting up */
	memset(chk, 0, sizeof(*chk));
	chk->rec.data.rcv_flags = rcv_flags;

	if (to_move >= length) {
		/* we think we can steal the whole thing */
		if ((sp->sender_all_done == 0) && (send_lock_up == 0)) {
			SCTP_TCB_SEND_LOCK(stcb);
			send_lock_up = 1;
		}
		if (to_move < sp->length) {
			/* bail, it changed */
			goto dont_do_it;
		}
		chk->data = sp->data;
		chk->last_mbuf = sp->tail_mbuf;
		/* register the stealing */
		sp->data = sp->tail_mbuf = NULL;
	} else {
		struct mbuf *m;
  dont_do_it:
		chk->data = SCTP_M_COPYM(sp->data, 0, to_move, M_DONTWAIT);
		chk->last_mbuf = NULL;
		if (chk->data == NULL) {
			sp->some_taken = some_taken;
			sctp_free_a_chunk(stcb, chk, so_locked);
			*bail = 1;
			to_move = 0;
			goto out_of;
		}
#ifdef SCTP_MBUF_LOGGING
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MBUF_LOGGING_ENABLE) {
			struct mbuf *mat;
			mat = chk->data;
			while (mat) {
				if (SCTP_BUF_IS_EXTENDED(mat)) {
					sctp_log_mb(mat, SCTP_MBUF_ICOPY);
				}
				mat = SCTP_BUF_NEXT(mat);
			}
		}
#endif
		/* Pull off the data */
		m_adj(sp->data, to_move);
		/* Now lets work our way down and compact it */
		m = sp->data;
		while (m && (SCTP_BUF_LEN(m) == 0)) {
			sp->data  = SCTP_BUF_NEXT(m);
			SCTP_BUF_NEXT(m) = NULL;
			if (sp->tail_mbuf == m) {
				/*-
				 * Freeing tail? TSNH since
				 * we supposedly were taking less
				 * than the sp->length.
				 */
#ifdef INVARIANTS
				panic("Huh, freing tail? - TSNH");
#else
				SCTP_PRINTF("Huh, freeing tail? - TSNH\n");
				sp->tail_mbuf = sp->data = NULL;
				sp->length = 0;
#endif

			}
			sctp_m_free(m);
			m = sp->data;
		}
	}
	if (SCTP_BUF_IS_EXTENDED(chk->data)) {
		chk->copy_by_ref = 1;
	} else {
		chk->copy_by_ref = 0;
	}
	/* get last_mbuf and counts of mb useage
	 * This is ugly but hopefully its only one mbuf.
	 */
	if (chk->last_mbuf == NULL) {
		chk->last_mbuf = chk->data;
		while(SCTP_BUF_NEXT(chk->last_mbuf) != NULL) {
			chk->last_mbuf = SCTP_BUF_NEXT(chk->last_mbuf);
		}
	}

	if (to_move > length) {
		/*- This should not happen either
		 * since we always lower to_move to the size
		 * of sp->length if its larger.
		 */
#ifdef INVARIANTS
		panic("Huh, how can to_move be larger?");
#else
		SCTP_PRINTF("Huh, how can to_move be larger?\n");
		sp->length = 0;
#endif
	} else {
		atomic_subtract_int(&sp->length, to_move);
	}
	if (M_LEADINGSPACE(chk->data) < (int)sizeof(struct sctp_data_chunk)) {
		/* Not enough room for a chunk header, get some */
		struct mbuf *m;
		m = sctp_get_mbuf_for_msg(1, 0, M_DONTWAIT, 0, MT_DATA);
		if (m == NULL) {
			/*
			 * we're in trouble here. _PREPEND below will free
			 * all the data if there is no leading space, so we
			 * must put the data back and restore.
			 */
			if (send_lock_up == 0) {
				SCTP_TCB_SEND_LOCK(stcb);
				send_lock_up = 1;
			}
			if (chk->data == NULL) {
				/* unsteal the data */
				sp->data = chk->data;
				sp->tail_mbuf = chk->last_mbuf;
			} else {
				struct mbuf *m_tmp;
				/* reassemble the data */
				m_tmp = sp->data;
				sp->data = chk->data;
				SCTP_BUF_NEXT(chk->last_mbuf) = m_tmp;
			}
			sp->some_taken = some_taken;
			atomic_add_int(&sp->length, to_move);
			chk->data = NULL;
			*bail = 1;
			sctp_free_a_chunk(stcb, chk, so_locked);
			to_move = 0;
			goto out_of;
		} else {
			SCTP_BUF_LEN(m) = 0;
			SCTP_BUF_NEXT(m) = chk->data;
			chk->data = m;
			M_ALIGN(chk->data, 4);
		}
	}
	SCTP_BUF_PREPEND(chk->data, sizeof(struct sctp_data_chunk), M_DONTWAIT);
	if (chk->data == NULL) {
		/* HELP, TSNH since we assured it would not above? */
#ifdef INVARIANTS
		panic("prepend failes HELP?");
#else
		SCTP_PRINTF("prepend fails HELP?\n");
		sctp_free_a_chunk(stcb, chk, so_locked);
#endif
		*bail = 1;
		to_move = 0;
		goto out_of;
	}
	sctp_snd_sb_alloc(stcb, sizeof(struct sctp_data_chunk));
	chk->book_size = chk->send_size = (to_move + sizeof(struct sctp_data_chunk));
	chk->book_size_scale = 0;
	chk->sent = SCTP_DATAGRAM_UNSENT;

	chk->flags = 0;
	chk->asoc = &stcb->asoc;
	chk->pad_inplace = 0;
	chk->no_fr_allowed = 0;
	chk->rec.data.stream_seq = sp->strseq;
	chk->rec.data.stream_number = sp->stream;
	chk->rec.data.payloadtype = sp->ppid;
	chk->rec.data.context = sp->context;
	chk->rec.data.doing_fast_retransmit = 0;

	chk->rec.data.timetodrop = sp->ts;
	chk->flags = sp->act_flags;

	if (sp->net) {
		chk->whoTo = sp->net;
		atomic_add_int(&chk->whoTo->ref_count, 1);
	} else
		chk->whoTo = NULL;

	if (sp->holds_key_ref) {
		chk->auth_keyid = sp->auth_keyid;
		sctp_auth_key_acquire(stcb, chk->auth_keyid);
		chk->holds_key_ref = 1;
	}
	
#if defined(__FreeBSD__) || defined(__Panda__)
	chk->rec.data.TSN_seq = atomic_fetchadd_int(&asoc->sending_seq, 1);
#else
	chk->rec.data.TSN_seq = asoc->sending_seq++;
#endif
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LOG_AT_SEND_2_OUTQ) {
		sctp_misc_ints(SCTP_STRMOUT_LOG_SEND,
		               (uintptr_t)stcb, sp->length,
		               (uint32_t)((chk->rec.data.stream_number << 16) | chk->rec.data.stream_seq),
		               chk->rec.data.TSN_seq);
	}
	dchkh = mtod(chk->data, struct sctp_data_chunk *);
	/*
	 * Put the rest of the things in place now. Size was done
	 * earlier in previous loop prior to padding.
	 */

#ifdef SCTP_ASOCLOG_OF_TSNS
	SCTP_TCB_LOCK_ASSERT(stcb);
	if (asoc->tsn_out_at >= SCTP_TSN_LOG_SIZE) {
		asoc->tsn_out_at = 0;
		asoc->tsn_out_wrapped = 1;
	}
	asoc->out_tsnlog[asoc->tsn_out_at].tsn = chk->rec.data.TSN_seq;
	asoc->out_tsnlog[asoc->tsn_out_at].strm = chk->rec.data.stream_number;
	asoc->out_tsnlog[asoc->tsn_out_at].seq = chk->rec.data.stream_seq;
	asoc->out_tsnlog[asoc->tsn_out_at].sz = chk->send_size;
	asoc->out_tsnlog[asoc->tsn_out_at].flgs =  chk->rec.data.rcv_flags;
	asoc->out_tsnlog[asoc->tsn_out_at].stcb =  (void *)stcb;
	asoc->out_tsnlog[asoc->tsn_out_at].in_pos =  asoc->tsn_out_at;
	asoc->out_tsnlog[asoc->tsn_out_at].in_out =  2;
	asoc->tsn_out_at++;
#endif

	dchkh->ch.chunk_type = SCTP_DATA;
	dchkh->ch.chunk_flags = chk->rec.data.rcv_flags;
	dchkh->dp.tsn = htonl(chk->rec.data.TSN_seq);
	dchkh->dp.stream_id = htons(strq->stream_no);
	dchkh->dp.stream_sequence = htons(chk->rec.data.stream_seq);
	dchkh->dp.protocol_id = chk->rec.data.payloadtype;
	dchkh->ch.chunk_length = htons(chk->send_size);
	/* Now advance the chk->send_size by the actual pad needed. */
	if (chk->send_size < SCTP_SIZE32(chk->book_size)) {
		/* need a pad */
		struct mbuf *lm;
		int pads;
		
		pads = SCTP_SIZE32(chk->book_size) - chk->send_size;
		if (sctp_pad_lastmbuf(chk->data, pads, chk->last_mbuf) == 0) {
			chk->pad_inplace = 1;
		}
		if ((lm = SCTP_BUF_NEXT(chk->last_mbuf)) != NULL) {
			/* pad added an mbuf */
			chk->last_mbuf = lm;
		}
		chk->send_size += pads;
	}
	/* We only re-set the policy if it is on */
	if (sp->pr_sctp_on) {
		sctp_set_prsctp_policy(sp);
		asoc->pr_sctp_cnt++;
		chk->pr_sctp_on = 1;
	} else {
		chk->pr_sctp_on = 0;
	}
	if (sp->msg_is_complete && (sp->length == 0) && (sp->sender_all_done)) {
		/* All done pull and kill the message */
		atomic_subtract_int(&asoc->stream_queue_cnt, 1);
		if (sp->put_last_out == 0) {
			SCTP_PRINTF("Gak, put out entire msg with NO end!-2\n");
			SCTP_PRINTF("sender_done:%d len:%d msg_comp:%d put_last_out:%d send_lock:%d\n",
			            sp->sender_all_done,
			            sp->length,
			            sp->msg_is_complete,
			            sp->put_last_out,
			            send_lock_up);
		}
		if ((send_lock_up == 0) && (TAILQ_NEXT(sp, next) == NULL)) {
			SCTP_TCB_SEND_LOCK(stcb);
			send_lock_up = 1;
		}
		TAILQ_REMOVE(&strq->outqueue, sp, next);
		stcb->asoc.ss_functions.sctp_ss_remove_from_stream(stcb, asoc, strq, sp, send_lock_up);
		if (sp->net) {
			sctp_free_remote_addr(sp->net);
			sp->net = NULL;
		}
		if (sp->data) {
			sctp_m_freem(sp->data);
			sp->data = NULL;
		}
		sctp_free_a_strmoq(stcb, sp, so_locked);

		/* we can't be locked to it */
		*locked = 0;
		stcb->asoc.locked_on_sending = NULL;
	} else {
		/* more to go, we are locked */
		*locked = 1;
	}
	asoc->chunks_on_out_queue++;
	TAILQ_INSERT_TAIL(&asoc->send_queue, chk, sctp_next);
	asoc->send_queue_cnt++;
 out_of:
	if (send_lock_up) {
		SCTP_TCB_SEND_UNLOCK(stcb);
		send_lock_up = 0;
	}
	return (to_move);
}


static void
sctp_fill_outqueue(struct sctp_tcb *stcb,
    struct sctp_nets *net, int frag_point, int eeor_mode, int *quit_now, int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
	SCTP_UNUSED
#endif
)
{
	struct sctp_association *asoc;
	struct sctp_stream_out *strq, *strqn;
	int goal_mtu, moved_how_much, total_moved=0, bail=0;
	int locked, giveup;

	SCTP_TCB_LOCK_ASSERT(stcb);
	asoc = &stcb->asoc;
#ifdef INET6
	if (net->ro._l_addr.sin6.sin6_family == AF_INET6) {
		goal_mtu = net->mtu - SCTP_MIN_OVERHEAD;
	} else {
		/* ?? not sure what else to do */
		goal_mtu = net->mtu - SCTP_MIN_V4_OVERHEAD;
	}
#else
	goal_mtu = net->mtu - SCTP_MIN_OVERHEAD;
#endif
	/* Need an allowance for the data chunk header too */
	goal_mtu -= sizeof(struct sctp_data_chunk);

	/* must make even word boundary */
	goal_mtu &= 0xfffffffc;
	if (asoc->locked_on_sending) {
		/* We are stuck on one stream until the message completes. */
		strq = asoc->locked_on_sending;
		locked = 1;
	} else {
		strq = stcb->asoc.ss_functions.sctp_ss_select_stream(stcb, net, asoc);
		locked = 0;
	}
	strqn = strq;
	while ((goal_mtu > 0) && strq) {
		giveup = 0;
		bail = 0;
		moved_how_much = sctp_move_to_outqueue(stcb, strq, goal_mtu, frag_point, &locked,
						       &giveup, eeor_mode, &bail, so_locked);
		if (moved_how_much)
			stcb->asoc.ss_functions.sctp_ss_scheduled(stcb, net, asoc, strq, moved_how_much);

		if (locked) {
			asoc->locked_on_sending = strq;
			if ((moved_how_much == 0) || (giveup) || bail)
				/* no more to move for now */
				break;
		} else {
			asoc->locked_on_sending = NULL;
			if ((giveup) || bail) {
				break;
			}
			strq = stcb->asoc.ss_functions.sctp_ss_select_stream(stcb, net, asoc);
			if (strq == NULL) {
				break;
			}
		}
		total_moved += moved_how_much;
		goal_mtu -= (moved_how_much + sizeof(struct sctp_data_chunk));
		goal_mtu &= 0xfffffffc;
	}
	if (bail)
		*quit_now = 1;

	stcb->asoc.ss_functions.sctp_ss_packet_done(stcb, net, asoc);

	if (total_moved == 0) {
		if ((stcb->asoc.sctp_cmt_on_off == 0) &&
		    (net == stcb->asoc.primary_destination)) {
			/* ran dry for primary network net */
			SCTP_STAT_INCR(sctps_primary_randry);
		} else if (stcb->asoc.sctp_cmt_on_off > 0) {
			/* ran dry with CMT on */
			SCTP_STAT_INCR(sctps_cmt_randry);
		}
	}
}

void
sctp_fix_ecn_echo(struct sctp_association *asoc)
{
	struct sctp_tmit_chunk *chk;

	TAILQ_FOREACH(chk, &asoc->control_send_queue, sctp_next) {
		if (chk->rec.chunk_id.id == SCTP_ECN_ECHO) {
			chk->sent = SCTP_DATAGRAM_UNSENT;
		}
	}
}

void
sctp_move_chunks_from_net(struct sctp_tcb *stcb, struct sctp_nets *net)
{
	struct sctp_association *asoc;
	struct sctp_tmit_chunk *chk;
	struct sctp_stream_queue_pending *sp;
	unsigned int i;

	if (net == NULL) {
		return;
	}
	asoc = &stcb->asoc;
	for (i = 0; i < stcb->asoc.streamoutcnt; i++) {
		TAILQ_FOREACH(sp, &stcb->asoc.strmout[i].outqueue, next) {
			if (sp->net == net) {
				sctp_free_remote_addr(sp->net);
				sp->net = NULL;
			}
		}
	}
	TAILQ_FOREACH(chk, &asoc->send_queue, sctp_next) {
		if (chk->whoTo == net) {
			sctp_free_remote_addr(chk->whoTo);
			chk->whoTo = NULL;
		}
	}
}

int
sctp_med_chunk_output(struct sctp_inpcb *inp,
		      struct sctp_tcb *stcb,
		      struct sctp_association *asoc,
		      int *num_out,
		      int *reason_code,
		      int control_only, int from_where,
		      struct timeval *now, int *now_filled, int frag_point, int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
		      SCTP_UNUSED
#endif
	)
{
	/*
	 * Ok this is the generic chunk service queue. we must do the
	 * following: - Service the stream queue that is next, moving any
	 * message (note I must get a complete message i.e. FIRST/MIDDLE and
	 * LAST to the out queue in one pass) and assigning TSN's - Check to
	 * see if the cwnd/rwnd allows any output, if so we go ahead and
	 * fomulate and send the low level chunks. Making sure to combine
	 * any control in the control chunk queue also.
	 */
	struct sctp_nets *net, *start_at, *sack_goes_to=NULL, *old_start_at=NULL;
	struct mbuf *outchain, *endoutchain;
	struct sctp_tmit_chunk *chk, *nchk;

	/* temp arrays for unlinking */
	struct sctp_tmit_chunk *data_list[SCTP_MAX_DATA_BUNDLING];
	int no_fragmentflg, error;
	unsigned int max_rwnd_per_dest, max_send_per_dest;
	int one_chunk, hbflag, skip_data_for_this_net;
	int asconf, cookie, no_out_cnt;
	int bundle_at, ctl_cnt, no_data_chunks, eeor_mode;
	unsigned int mtu, r_mtu, omtu, mx_mtu, to_out;
	int tsns_sent = 0;
	uint32_t auth_offset = 0;
	struct sctp_auth_chunk *auth = NULL;
	uint16_t auth_keyid;
	int override_ok = 1;
	int skip_fill_up = 0;
	int data_auth_reqd = 0;
	/* JRS 5/14/07 - Add flag for whether a heartbeat is sent to
	   the destination. */
	int quit_now = 0;

#if defined(__APPLE__)
	if (so_locked) {
		sctp_lock_assert(SCTP_INP_SO(inp));
	} else {
		sctp_unlock_assert(SCTP_INP_SO(inp));
	}
#endif
	*num_out = 0;
	auth_keyid = stcb->asoc.authinfo.active_keyid;

	if ((asoc->state & SCTP_STATE_SHUTDOWN_PENDING) ||
	    (asoc->state & SCTP_STATE_SHUTDOWN_RECEIVED) ||
	    (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_EXPLICIT_EOR))) {
		eeor_mode = 1;
	} else {
		eeor_mode = 0;
	}
	ctl_cnt = no_out_cnt = asconf = cookie = 0;
	/*
	 * First lets prime the pump. For each destination, if there is room
	 * in the flight size, attempt to pull an MTU's worth out of the
	 * stream queues into the general send_queue
	 */
#ifdef SCTP_AUDITING_ENABLED
	sctp_audit_log(0xC2, 2);
#endif
	SCTP_TCB_LOCK_ASSERT(stcb);
	hbflag = 0;
	if ((control_only) || (asoc->stream_reset_outstanding))
		no_data_chunks = 1;
	else
		no_data_chunks = 0;

	/* Nothing to possible to send? */
	if ((TAILQ_EMPTY(&asoc->control_send_queue) ||
	     (asoc->ctrl_queue_cnt == stcb->asoc.ecn_echo_cnt_onq)) &&
	    TAILQ_EMPTY(&asoc->asconf_send_queue) &&
	    TAILQ_EMPTY(&asoc->send_queue) &&
	    stcb->asoc.ss_functions.sctp_ss_is_empty(stcb, asoc)) {
	nothing_to_send:
		*reason_code = 9;
		return (0);
	}
	if (asoc->peers_rwnd == 0) {
		/* No room in peers rwnd */
		*reason_code = 1;
		if (asoc->total_flight > 0) {
			/* we are allowed one chunk in flight */
			no_data_chunks = 1;
		}
	}
	if (stcb->asoc.ecn_echo_cnt_onq) {
		/* Record where a sack goes, if any */
		if (no_data_chunks && 
		    (asoc->ctrl_queue_cnt == stcb->asoc.ecn_echo_cnt_onq)) {
			/* Nothing but ECNe to send - we don't do that */
			goto nothing_to_send;
		}
		TAILQ_FOREACH(chk, &asoc->control_send_queue, sctp_next) {
			if ((chk->rec.chunk_id.id == SCTP_SELECTIVE_ACK) ||
			    (chk->rec.chunk_id.id == SCTP_NR_SELECTIVE_ACK)) {
				sack_goes_to = chk->whoTo;
				break;
			}
		}
	}
	max_rwnd_per_dest = ((asoc->peers_rwnd + asoc->total_flight) / asoc->numnets);
	if (stcb->sctp_socket)
		max_send_per_dest = SCTP_SB_LIMIT_SND(stcb->sctp_socket) / asoc->numnets;
	else
		max_send_per_dest = 0;
	if (no_data_chunks == 0) {
		/* How many non-directed chunks are there? */
		TAILQ_FOREACH(chk, &asoc->send_queue, sctp_next) {
			if (chk->whoTo == NULL) {
				/* We already have non-directed
				 * chunks on the queue, no need
				 * to do a fill-up.
				 */
				skip_fill_up = 1;
				break;
			}
		}

	}
	if ((no_data_chunks == 0) &&
	    (skip_fill_up == 0) &&
	    (!stcb->asoc.ss_functions.sctp_ss_is_empty(stcb, asoc))) {
		TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
			/*
			 * This for loop we are in takes in
			 * each net, if its's got space in cwnd and
			 * has data sent to it (when CMT is off) then it
			 * calls sctp_fill_outqueue for the net. This gets
			 * data on the send queue for that network.
			 *
			 * In sctp_fill_outqueue TSN's are assigned and
			 * data is copied out of the stream buffers. Note
			 * mostly copy by reference (we hope).
			 */
			net->window_probe = 0;
			if ((net != stcb->asoc.alternate) &&
			    ((net->dest_state & SCTP_ADDR_PF) ||
			     (!(net->dest_state & SCTP_ADDR_REACHABLE)) ||
			     (net->dest_state & SCTP_ADDR_UNCONFIRMED))) {
				if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_LOGGING_ENABLE) {
					sctp_log_cwnd(stcb, net, 1,
						      SCTP_CWND_LOG_FILL_OUTQ_CALLED);
				}
			        continue;
			}
			if ((stcb->asoc.cc_functions.sctp_cwnd_new_transmission_begins) && 
			    (net->flight_size == 0)) {
				(*stcb->asoc.cc_functions.sctp_cwnd_new_transmission_begins)(stcb, net);
			}
			if (net->flight_size >= net->cwnd) {
				/* skip this network, no room - can't fill */
				if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_LOGGING_ENABLE) {
					sctp_log_cwnd(stcb, net, 3,
						      SCTP_CWND_LOG_FILL_OUTQ_CALLED);
				}
				continue;
			}
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_LOGGING_ENABLE) {
				sctp_log_cwnd(stcb, net, 4, SCTP_CWND_LOG_FILL_OUTQ_CALLED);
			}
			sctp_fill_outqueue(stcb, net, frag_point, eeor_mode, &quit_now, so_locked);
			if (quit_now) {
				/* memory alloc failure */
				no_data_chunks = 1;
				break;
			}
		}
	}
	/* now service each destination and send out what we can for it */
	/* Nothing to send? */
	if (TAILQ_EMPTY(&asoc->control_send_queue) &&
	    TAILQ_EMPTY(&asoc->asconf_send_queue) &&
	    TAILQ_EMPTY(&asoc->send_queue)) {
		*reason_code = 8;
		return (0);
	}
	
	if (asoc->sctp_cmt_on_off > 0) {
		/* get the last start point */
		start_at = asoc->last_net_cmt_send_started;
		if (start_at == NULL) {
			/* null so to beginning */
			start_at = TAILQ_FIRST(&asoc->nets);
		} else {
			start_at = TAILQ_NEXT(asoc->last_net_cmt_send_started, sctp_next);
			if (start_at == NULL) {
				start_at = TAILQ_FIRST(&asoc->nets);
			}
		}
		asoc->last_net_cmt_send_started = start_at;		
	} else {
		start_at = TAILQ_FIRST(&asoc->nets);
	}
	TAILQ_FOREACH(chk, &asoc->control_send_queue, sctp_next) {
		if (chk->whoTo == NULL) {
			if (asoc->alternate) {
				chk->whoTo = asoc->alternate;
			} else {
				chk->whoTo = asoc->primary_destination;
			}
			atomic_add_int(&chk->whoTo->ref_count, 1);
		}
	}
	old_start_at = NULL;
again_one_more_time:
	for (net = start_at ; net != NULL; net = TAILQ_NEXT(net, sctp_next)) {
		/* how much can we send? */
		/* SCTPDBG("Examine for sending net:%x\n", (uint32_t)net); */
		if (old_start_at && (old_start_at == net)) {
			/* through list ocmpletely. */
			break;
		}
		tsns_sent = 0xa;
		if (TAILQ_EMPTY(&asoc->control_send_queue) &&
		    TAILQ_EMPTY(&asoc->asconf_send_queue) &&
		    (net->flight_size >= net->cwnd)) {
			/* Nothing on control or asconf and flight is full, we can skip
			 * even in the CMT case.
			 */
			continue;
		}
		ctl_cnt = bundle_at = 0;
		endoutchain = outchain = NULL;
		no_fragmentflg = 1;
		one_chunk = 0;
		if (net->dest_state & SCTP_ADDR_UNCONFIRMED) {
			skip_data_for_this_net = 1;
		} else {
			skip_data_for_this_net = 0;
		}
#if !(defined(__Panda__) || defined(__Windows__) || defined(__Userspace__))
		if ((net->ro.ro_rt) && (net->ro.ro_rt->rt_ifp)) {
			/*
			 * if we have a route and an ifp check to see if we
			 * have room to send to this guy
			 */
			struct ifnet *ifp;

			ifp = net->ro.ro_rt->rt_ifp;
			if ((ifp->if_snd.ifq_len + 2) >= ifp->if_snd.ifq_maxlen) {
				SCTP_STAT_INCR(sctps_ifnomemqueued);
				if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LOG_MAXBURST_ENABLE) {
					sctp_log_maxburst(stcb, net, ifp->if_snd.ifq_len, ifp->if_snd.ifq_maxlen, SCTP_MAX_IFP_APPLIED);
				}
				continue;
			}
		}
#endif
		switch (((struct sockaddr *)&net->ro._l_addr)->sa_family) {
#ifdef INET
		case AF_INET:
			mtu = net->mtu - (sizeof(struct ip) + sizeof(struct sctphdr));
			break;
#endif
#ifdef INET6
		case AF_INET6:
			mtu = net->mtu - (sizeof(struct ip6_hdr) + sizeof(struct sctphdr));
			break;
#endif
		default:
			/* TSNH */
			mtu = net->mtu;
			break;
		}
		mx_mtu = mtu;
		to_out = 0;
		if (mtu > asoc->peers_rwnd) {
			if (asoc->total_flight > 0) {
				/* We have a packet in flight somewhere */
				r_mtu = asoc->peers_rwnd;
			} else {
				/* We are always allowed to send one MTU out */
				one_chunk = 1;
				r_mtu = mtu;
			}
		} else {
			r_mtu = mtu;
		}
		/************************/
		/* ASCONF transmission */
		/************************/
		/* Now first lets go through the asconf queue */
		TAILQ_FOREACH_SAFE(chk, &asoc->asconf_send_queue, sctp_next, nchk) {
			if (chk->rec.chunk_id.id != SCTP_ASCONF) {
				continue;
			}
			if (chk->whoTo != net) {
				/*
				 * No, not sent to the network we are
				 * looking at
				 */
				break;
			}
			if (chk->data == NULL) {
				break;
			}
			if (chk->sent != SCTP_DATAGRAM_UNSENT &&
			    chk->sent != SCTP_DATAGRAM_RESEND) {
				break;
			}
			/*
			 * if no AUTH is yet included and this chunk
			 * requires it, make sure to account for it.  We
			 * don't apply the size until the AUTH chunk is
			 * actually added below in case there is no room for
			 * this chunk. NOTE: we overload the use of "omtu"
			 * here
			 */
			if ((auth == NULL) &&
			    sctp_auth_is_required_chunk(chk->rec.chunk_id.id,
							stcb->asoc.peer_auth_chunks)) {
				omtu = sctp_get_auth_chunk_len(stcb->asoc.peer_hmac_id);
			} else
				omtu = 0;
			/* Here we do NOT factor the r_mtu */
			if ((chk->send_size < (int)(mtu - omtu)) ||
			    (chk->flags & CHUNK_FLAGS_FRAGMENT_OK)) {
				/*
				 * We probably should glom the mbuf chain
				 * from the chk->data for control but the
				 * problem is it becomes yet one more level
				 * of tracking to do if for some reason
				 * output fails. Then I have got to
				 * reconstruct the merged control chain.. el
				 * yucko.. for now we take the easy way and
				 * do the copy
				 */
				/*
				 * Add an AUTH chunk, if chunk requires it
				 * save the offset into the chain for AUTH
				 */
				if ((auth == NULL) &&
				    (sctp_auth_is_required_chunk(chk->rec.chunk_id.id,
								 stcb->asoc.peer_auth_chunks))) {
					outchain = sctp_add_auth_chunk(outchain,
								       &endoutchain,
								       &auth,
								       &auth_offset,
								       stcb,
								       chk->rec.chunk_id.id);
					SCTP_STAT_INCR_COUNTER64(sctps_outcontrolchunks);
				}
				outchain = sctp_copy_mbufchain(chk->data, outchain, &endoutchain,
							       (int)chk->rec.chunk_id.can_take_data,
							       chk->send_size, chk->copy_by_ref);
				if (outchain == NULL) {
					*reason_code = 8;
					SCTP_LTRACE_ERR_RET(inp, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, ENOMEM);
					return (ENOMEM);
				}
				SCTP_STAT_INCR_COUNTER64(sctps_outcontrolchunks);
				/* update our MTU size */
				if (mtu > (chk->send_size + omtu))
					mtu -= (chk->send_size + omtu);
				else
					mtu = 0;
				to_out += (chk->send_size + omtu);
				/* Do clear IP_DF ? */
				if (chk->flags & CHUNK_FLAGS_FRAGMENT_OK) {
					no_fragmentflg = 0;
				}
				if (chk->rec.chunk_id.can_take_data)
					chk->data = NULL;
				/*
				 * set hb flag since we can
				 * use these for RTO
				 */
				hbflag = 1;
				asconf = 1;
				/*
				 * should sysctl this: don't
				 * bundle data with ASCONF
				 * since it requires AUTH
				 */
				no_data_chunks = 1;
				chk->sent = SCTP_DATAGRAM_SENT;
				chk->snd_count++;
				if (mtu == 0) {
					/*
					 * Ok we are out of room but we can
					 * output without effecting the
					 * flight size since this little guy
					 * is a control only packet.
					 */
					sctp_timer_start(SCTP_TIMER_TYPE_ASCONF, inp, stcb, net);
					/*
					 * do NOT clear the asconf
					 * flag as it is used to do
					 * appropriate source address
					 * selection.
					 */
					if ((error = sctp_lowlevel_chunk_output(inp, stcb, net,
					                                        (struct sockaddr *)&net->ro._l_addr,
					                                        outchain, auth_offset, auth,
					                                        stcb->asoc.authinfo.active_keyid,
					                                        no_fragmentflg, 0, NULL, asconf,
					                                        inp->sctp_lport, stcb->rport,
					                                        htonl(stcb->asoc.peer_vtag),
					                                        net->port, so_locked, NULL, NULL))) {
						if (error == ENOBUFS) {
							asoc->ifp_had_enobuf = 1;
							SCTP_STAT_INCR(sctps_lowlevelerr);
						}
						if (from_where == 0) {
							SCTP_STAT_INCR(sctps_lowlevelerrusr);
						}
						if (*now_filled == 0) {
							(void)SCTP_GETTIME_TIMEVAL(&net->last_sent_time);
							*now_filled = 1;
							*now = net->last_sent_time;
						} else {
							net->last_sent_time = *now;
						}
						hbflag = 0;
						/* error, could not output */
						if (error == EHOSTUNREACH) {
							/*
							 * Destination went
							 * unreachable
							 * during this send
							 */
							sctp_move_chunks_from_net(stcb, net);
						}
						*reason_code = 7;
						continue;
					} else
						asoc->ifp_had_enobuf = 0;
					if (*now_filled == 0) {
						(void)SCTP_GETTIME_TIMEVAL(&net->last_sent_time);
						*now_filled = 1;
						*now = net->last_sent_time;
					} else {
						net->last_sent_time = *now;
					}
					hbflag = 0;
					/*
					 * increase the number we sent, if a
					 * cookie is sent we don't tell them
					 * any was sent out.
					 */
					outchain = endoutchain = NULL;
					auth = NULL;
					auth_offset = 0;
					if (!no_out_cnt)
						*num_out += ctl_cnt;
					/* recalc a clean slate and setup */
					if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) {
						mtu = (net->mtu - SCTP_MIN_OVERHEAD);
					} else {
						mtu = (net->mtu - SCTP_MIN_V4_OVERHEAD);
					}
					to_out = 0;
					no_fragmentflg = 1;
				}
			}
		}
		/************************/
		/* Control transmission */
		/************************/
		/* Now first lets go through the control queue */
		TAILQ_FOREACH_SAFE(chk, &asoc->control_send_queue, sctp_next, nchk) {
			if ((sack_goes_to) && 
			    (chk->rec.chunk_id.id == SCTP_ECN_ECHO) && 
			    (chk->whoTo != sack_goes_to)) {
				/* 
				 * if we have a sack in queue, and we are looking at an
				 * ecn echo that is NOT queued to where the sack is going..
				 */
				if (chk->whoTo == net) {
					/* Don't transmit it to where its going (current net) */
					continue;
				} else if (sack_goes_to == net) {
					/* But do transmit it to this address */
					goto skip_net_check;
				}
			}
			if (chk->whoTo != net) {
				/*
				 * No, not sent to the network we are
				 * looking at
				 */
				continue;
			}
		skip_net_check:
			if (chk->data == NULL) {
				continue;
			}
			if (chk->sent != SCTP_DATAGRAM_UNSENT) {
				/*
				 * It must be unsent. Cookies and ASCONF's
				 * hang around but there timers will force
				 * when marked for resend.
				 */
				continue;
			}
			/*
			 * if no AUTH is yet included and this chunk
			 * requires it, make sure to account for it.  We
			 * don't apply the size until the AUTH chunk is
			 * actually added below in case there is no room for
			 * this chunk. NOTE: we overload the use of "omtu"
			 * here
			 */
			if ((auth == NULL) &&
			    sctp_auth_is_required_chunk(chk->rec.chunk_id.id,
							stcb->asoc.peer_auth_chunks)) {
				omtu = sctp_get_auth_chunk_len(stcb->asoc.peer_hmac_id);
			} else
				omtu = 0;
			/* Here we do NOT factor the r_mtu */
			if ((chk->send_size <= (int)(mtu - omtu)) ||
			    (chk->flags & CHUNK_FLAGS_FRAGMENT_OK)) {
				/*
				 * We probably should glom the mbuf chain
				 * from the chk->data for control but the
				 * problem is it becomes yet one more level
				 * of tracking to do if for some reason
				 * output fails. Then I have got to
				 * reconstruct the merged control chain.. el
				 * yucko.. for now we take the easy way and
				 * do the copy
				 */
				/*
				 * Add an AUTH chunk, if chunk requires it
				 * save the offset into the chain for AUTH
				 */
				if ((auth == NULL) &&
				    (sctp_auth_is_required_chunk(chk->rec.chunk_id.id,
								 stcb->asoc.peer_auth_chunks))) {
					outchain = sctp_add_auth_chunk(outchain,
								       &endoutchain,
								       &auth,
								       &auth_offset,
								       stcb,
								       chk->rec.chunk_id.id);
					SCTP_STAT_INCR_COUNTER64(sctps_outcontrolchunks);
				}
				outchain = sctp_copy_mbufchain(chk->data, outchain, &endoutchain,
							       (int)chk->rec.chunk_id.can_take_data,
							       chk->send_size, chk->copy_by_ref);
				if (outchain == NULL) {
					*reason_code = 8;
					SCTP_LTRACE_ERR_RET(inp, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, ENOMEM);
					return (ENOMEM);
				}
				SCTP_STAT_INCR_COUNTER64(sctps_outcontrolchunks);
				/* update our MTU size */
				if (mtu > (chk->send_size + omtu))
					mtu -= (chk->send_size + omtu);
				else
					mtu = 0;
				to_out += (chk->send_size + omtu);
				/* Do clear IP_DF ? */
				if (chk->flags & CHUNK_FLAGS_FRAGMENT_OK) {
					no_fragmentflg = 0;
				}
				if (chk->rec.chunk_id.can_take_data)
					chk->data = NULL;
				/* Mark things to be removed, if needed */
				if ((chk->rec.chunk_id.id == SCTP_SELECTIVE_ACK) ||
				    (chk->rec.chunk_id.id == SCTP_NR_SELECTIVE_ACK) || /* EY */
				    (chk->rec.chunk_id.id == SCTP_HEARTBEAT_REQUEST) ||
				    (chk->rec.chunk_id.id == SCTP_HEARTBEAT_ACK) ||
				    (chk->rec.chunk_id.id == SCTP_SHUTDOWN) ||
				    (chk->rec.chunk_id.id == SCTP_SHUTDOWN_ACK) ||
				    (chk->rec.chunk_id.id == SCTP_OPERATION_ERROR) ||
				    (chk->rec.chunk_id.id == SCTP_COOKIE_ACK) ||
				    (chk->rec.chunk_id.id == SCTP_ECN_CWR) ||
				    (chk->rec.chunk_id.id == SCTP_PACKET_DROPPED) ||
				    (chk->rec.chunk_id.id == SCTP_ASCONF_ACK)) {
					if (chk->rec.chunk_id.id == SCTP_HEARTBEAT_REQUEST) {
						hbflag = 1;
					}
					/* remove these chunks at the end */
					if ((chk->rec.chunk_id.id == SCTP_SELECTIVE_ACK) ||
					    (chk->rec.chunk_id.id == SCTP_NR_SELECTIVE_ACK)) {
						/* turn off the timer */
						if (SCTP_OS_TIMER_PENDING(&stcb->asoc.dack_timer.timer)) {
							sctp_timer_stop(SCTP_TIMER_TYPE_RECV,
									inp, stcb, net, SCTP_FROM_SCTP_OUTPUT+SCTP_LOC_1 );
						}
					}
					ctl_cnt++;
				} else {
					/*
					 * Other chunks, since they have
					 * timers running (i.e. COOKIE)
					 * we just "trust" that it
					 * gets sent or retransmitted.
					 */
					ctl_cnt++;
					if (chk->rec.chunk_id.id == SCTP_COOKIE_ECHO) {
						cookie = 1;
						no_out_cnt = 1;
					} else if (chk->rec.chunk_id.id == SCTP_ECN_ECHO) {
						/* 
						 * Increment ecne send count here 
						 * this means we may be over-zealous in
						 * our counting if the send fails, but its
						 * the best place to do it (we used to do
						 * it in the queue of the chunk, but that did
						 * not tell how many times it was sent.
						 */
						SCTP_STAT_INCR(sctps_sendecne);
					}
					chk->sent = SCTP_DATAGRAM_SENT;
					chk->snd_count++;
				}
				if (mtu == 0) {
					/*
					 * Ok we are out of room but we can
					 * output without effecting the
					 * flight size since this little guy
					 * is a control only packet.
					 */
					if (asconf) {
						sctp_timer_start(SCTP_TIMER_TYPE_ASCONF, inp, stcb, net);
						/*
						 * do NOT clear the asconf
						 * flag as it is used to do
						 * appropriate source address
						 * selection.
						 */
					}
					if (cookie) {
						sctp_timer_start(SCTP_TIMER_TYPE_COOKIE, inp, stcb, net);
						cookie = 0;
					}
					if ((error = sctp_lowlevel_chunk_output(inp, stcb, net,
					                                        (struct sockaddr *)&net->ro._l_addr,
					                                        outchain,
					                                        auth_offset, auth,
					                                        stcb->asoc.authinfo.active_keyid,
					                                        no_fragmentflg, 0, NULL, asconf,
					                                        inp->sctp_lport, stcb->rport,
					                                        htonl(stcb->asoc.peer_vtag),
					                                        net->port, so_locked, NULL, NULL))) {
						if (error == ENOBUFS) {
							asoc->ifp_had_enobuf = 1;
							SCTP_STAT_INCR(sctps_lowlevelerr);
						}
						if (from_where == 0) {
							SCTP_STAT_INCR(sctps_lowlevelerrusr);
						}
						/* error, could not output */
						if (hbflag) {
							if (*now_filled == 0) {
								(void)SCTP_GETTIME_TIMEVAL(&net->last_sent_time);
								*now_filled = 1;
								*now = net->last_sent_time;
							} else {
								net->last_sent_time = *now;
							}
							hbflag = 0;
						}
						if (error == EHOSTUNREACH) {
							/*
							 * Destination went
							 * unreachable
							 * during this send
							 */
							sctp_move_chunks_from_net(stcb, net);
						}
						*reason_code = 7;
						continue;
					} else
						asoc->ifp_had_enobuf = 0;
					/* Only HB or ASCONF advances time */
					if (hbflag) {
						if (*now_filled == 0) {
							(void)SCTP_GETTIME_TIMEVAL(&net->last_sent_time);
							*now_filled = 1;
							*now = net->last_sent_time;
						} else {
							net->last_sent_time = *now;
						}
						hbflag = 0;
					}
					/*
					 * increase the number we sent, if a
					 * cookie is sent we don't tell them
					 * any was sent out.
					 */
					outchain = endoutchain = NULL;
					auth = NULL;
					auth_offset = 0;
					if (!no_out_cnt)
						*num_out += ctl_cnt;
					/* recalc a clean slate and setup */
					if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) {
						mtu = (net->mtu - SCTP_MIN_OVERHEAD);
					} else {
						mtu = (net->mtu - SCTP_MIN_V4_OVERHEAD);
					}
					to_out = 0;
					no_fragmentflg = 1;
				}
			}
		}
		/* JRI: if dest is in PF state, do not send data to it */
		if ((asoc->sctp_cmt_on_off > 0) &&
		    (net != stcb->asoc.alternate) &&
		    (net->dest_state & SCTP_ADDR_PF)) {
			goto no_data_fill;
		}
		if (net->flight_size >= net->cwnd) {
			goto no_data_fill;
		}
		if ((asoc->sctp_cmt_on_off > 0) &&
		    (SCTP_BASE_SYSCTL(sctp_buffer_splitting) & SCTP_RECV_BUFFER_SPLITTING) &&
		    (net->flight_size > max_rwnd_per_dest)) {
			goto no_data_fill;
		}
		/*
		 * We need a specific accounting for the usage of the
		 * send buffer. We also need to check the number of messages
		 * per net. For now, this is better than nothing and it
		 * disabled by default...
		 */
		if ((asoc->sctp_cmt_on_off > 0) &&
		    (SCTP_BASE_SYSCTL(sctp_buffer_splitting) & SCTP_SEND_BUFFER_SPLITTING) &&
		    (max_send_per_dest > 0) &&
		    (net->flight_size > max_send_per_dest)) {
			goto no_data_fill;
		}
		/*********************/
		/* Data transmission */
		/*********************/
		/*
		 * if AUTH for DATA is required and no AUTH has been added
		 * yet, account for this in the mtu now... if no data can be
		 * bundled, this adjustment won't matter anyways since the
		 * packet will be going out...
		 */
		data_auth_reqd = sctp_auth_is_required_chunk(SCTP_DATA,
							     stcb->asoc.peer_auth_chunks);
		if (data_auth_reqd && (auth == NULL)) {
			mtu -= sctp_get_auth_chunk_len(stcb->asoc.peer_hmac_id);
		}
		/* now lets add any data within the MTU constraints */
		switch (((struct sockaddr *)&net->ro._l_addr)->sa_family) {
		case AF_INET:
			if (net->mtu > (sizeof(struct ip) + sizeof(struct sctphdr)))
				omtu = net->mtu - (sizeof(struct ip) + sizeof(struct sctphdr));
			else
				omtu = 0;
			break;
#ifdef INET6		
		case AF_INET6:
			if (net->mtu > (sizeof(struct ip6_hdr) + sizeof(struct sctphdr)))
				omtu = net->mtu - (sizeof(struct ip6_hdr) + sizeof(struct sctphdr));
			else
				omtu = 0;
			break;
#endif
		default:
			/* TSNH */
			omtu = 0;
			break;
		}
		if ((((asoc->state & SCTP_STATE_OPEN) == SCTP_STATE_OPEN) &&
		     (skip_data_for_this_net == 0)) ||
		    (cookie)) {
			TAILQ_FOREACH_SAFE(chk, &asoc->send_queue, sctp_next, nchk) {
				if (no_data_chunks) {
					/* let only control go out */
					*reason_code = 1;
					break;
				}
				if (net->flight_size >= net->cwnd) {
					/* skip this net, no room for data */
					*reason_code = 2;
					break;
				}
				if ((chk->whoTo != NULL) &&
				    (chk->whoTo != net)) {
					/* Don't send the chunk on this net */
					continue;
				}

				if (asoc->sctp_cmt_on_off == 0) {
					if ((asoc->alternate) && 
					    (asoc->alternate != net) &&
					    (chk->whoTo == NULL)) {
						continue;
					} else if ((net != asoc->primary_destination) &&
						   (asoc->alternate == NULL) &&
						   (chk->whoTo == NULL)) {
						continue;
					}
				}
				if ((chk->send_size > omtu) && ((chk->flags & CHUNK_FLAGS_FRAGMENT_OK) == 0)) {
					/*-
					 * strange, we have a chunk that is
					 * to big for its destination and
					 * yet no fragment ok flag.
					 * Something went wrong when the
					 * PMTU changed...we did not mark
					 * this chunk for some reason?? I
					 * will fix it here by letting IP
					 * fragment it for now and printing
					 * a warning. This really should not
					 * happen ...
					 */
					SCTP_PRINTF("Warning chunk of %d bytes > mtu:%d and yet PMTU disc missed\n",
						    chk->send_size, mtu);
					chk->flags |= CHUNK_FLAGS_FRAGMENT_OK;
				}
				if (SCTP_BASE_SYSCTL(sctp_enable_sack_immediately) &&
				    ((asoc->state & SCTP_STATE_SHUTDOWN_PENDING) == SCTP_STATE_SHUTDOWN_PENDING)) {
					struct sctp_data_chunk *dchkh;
					
					dchkh = mtod(chk->data, struct sctp_data_chunk *);
					dchkh->ch.chunk_flags |= SCTP_DATA_SACK_IMMEDIATELY;
				}
				if (((chk->send_size <= mtu) && (chk->send_size <= r_mtu)) ||
				    ((chk->flags & CHUNK_FLAGS_FRAGMENT_OK) && (chk->send_size <= asoc->peers_rwnd))) {
					/* ok we will add this one */

					/*
					 * Add an AUTH chunk, if chunk
					 * requires it, save the offset into
					 * the chain for AUTH
					 */
					if (data_auth_reqd) {
						if (auth == NULL) {
							outchain = sctp_add_auth_chunk(outchain,
										       &endoutchain,
										       &auth,
										       &auth_offset,
										       stcb,
										       SCTP_DATA);
							auth_keyid = chk->auth_keyid;
							override_ok = 0;
							SCTP_STAT_INCR_COUNTER64(sctps_outcontrolchunks);
						} else if (override_ok) {
							/* use this data's keyid */
							auth_keyid = chk->auth_keyid;
							override_ok = 0;
						} else if (auth_keyid != chk->auth_keyid) {
							/* different keyid, so done bundling */
							break;
						}
					}
					outchain = sctp_copy_mbufchain(chk->data, outchain, &endoutchain, 0,
								       chk->send_size, chk->copy_by_ref);
					if (outchain == NULL) {
						SCTPDBG(SCTP_DEBUG_OUTPUT3, "No memory?\n");
						if (!SCTP_OS_TIMER_PENDING(&net->rxt_timer.timer)) {
							sctp_timer_start(SCTP_TIMER_TYPE_SEND, inp, stcb, net);
						}
						*reason_code = 3;
						SCTP_LTRACE_ERR_RET(inp, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, ENOMEM);
						return (ENOMEM);
					}
					/* upate our MTU size */
					/* Do clear IP_DF ? */
					if (chk->flags & CHUNK_FLAGS_FRAGMENT_OK) {
						no_fragmentflg = 0;
					}
					/* unsigned subtraction of mtu */
					if (mtu > chk->send_size)
						mtu -= chk->send_size;
					else
						mtu = 0;
					/* unsigned subtraction of r_mtu */
					if (r_mtu > chk->send_size)
						r_mtu -= chk->send_size;
					else
						r_mtu = 0;

					to_out += chk->send_size;
					if ((to_out > mx_mtu) && no_fragmentflg) {
#ifdef INVARIANTS
						panic("Exceeding mtu of %d out size is %d", mx_mtu, to_out);
#else
						SCTP_PRINTF("Exceeding mtu of %d out size is %d\n",
							    mx_mtu, to_out);
#endif
					}
					chk->window_probe = 0;
					data_list[bundle_at++] = chk;
					if (bundle_at >= SCTP_MAX_DATA_BUNDLING) {
						mtu = 0;
						break;
					}
					if (chk->sent == SCTP_DATAGRAM_UNSENT) {
						if ((chk->rec.data.rcv_flags & SCTP_DATA_UNORDERED) == 0) {
							SCTP_STAT_INCR_COUNTER64(sctps_outorderchunks);
						} else {
							SCTP_STAT_INCR_COUNTER64(sctps_outunorderchunks);
						}
						if (((chk->rec.data.rcv_flags & SCTP_DATA_LAST_FRAG) == SCTP_DATA_LAST_FRAG) &&
						    ((chk->rec.data.rcv_flags & SCTP_DATA_FIRST_FRAG) == 0))
							/* Count number of user msg's that were fragmented
							 * we do this by counting when we see a LAST fragment
							 * only.
							 */
							SCTP_STAT_INCR_COUNTER64(sctps_fragusrmsgs);
					}
					if ((mtu == 0) || (r_mtu == 0) || (one_chunk)) {
						if ((one_chunk) && (stcb->asoc.total_flight == 0)) {
							data_list[0]->window_probe = 1;
							net->window_probe = 1;
						}
						break;
					}
				} else {
					/*
					 * Must be sent in order of the
					 * TSN's (on a network)
					 */
					break;
				}
			}	/* for (chunk gather loop for this net) */
		}		/* if asoc.state OPEN */
	no_data_fill:
		/* Is there something to send for this destination? */
		if (outchain) {
			/* We may need to start a control timer or two */
			if (asconf) {
				sctp_timer_start(SCTP_TIMER_TYPE_ASCONF, inp,
						 stcb, net);
				/*
				 * do NOT clear the asconf flag as it is used
				 * to do appropriate source address selection.
				 */
			}
			if (cookie) {
				sctp_timer_start(SCTP_TIMER_TYPE_COOKIE, inp, stcb, net);
				cookie = 0;
			}
			/* must start a send timer if data is being sent */
			if (bundle_at && (!SCTP_OS_TIMER_PENDING(&net->rxt_timer.timer))) {
				/*
				 * no timer running on this destination
				 * restart it.
				 */
				sctp_timer_start(SCTP_TIMER_TYPE_SEND, inp, stcb, net);
			}
			/* Now send it, if there is anything to send :> */
			if ((error = sctp_lowlevel_chunk_output(inp,
			                                        stcb,
			                                        net,
			                                        (struct sockaddr *)&net->ro._l_addr,
			                                        outchain,
			                                        auth_offset,
			                                        auth,
			                                        auth_keyid,
			                                        no_fragmentflg,
			                                        bundle_at,
			                                        data_list[0],
			                                        asconf,
			                                        inp->sctp_lport, stcb->rport,
			                                        htonl(stcb->asoc.peer_vtag),
			                                        net->port, so_locked, NULL, NULL))) {
				/* error, we could not output */
				if (error == ENOBUFS) {
					SCTP_STAT_INCR(sctps_lowlevelerr);
					asoc->ifp_had_enobuf = 1;
				}
				if (from_where == 0) {
					SCTP_STAT_INCR(sctps_lowlevelerrusr);
				}
				SCTPDBG(SCTP_DEBUG_OUTPUT3, "Gak send error %d\n", error);
				if (hbflag) {
					if (*now_filled == 0) {
						(void)SCTP_GETTIME_TIMEVAL(&net->last_sent_time);
						*now_filled = 1;
						*now = net->last_sent_time;
					} else {
						net->last_sent_time = *now;
					}
					hbflag = 0;
				}
				if (error == EHOSTUNREACH) {
					/*
					 * Destination went unreachable
					 * during this send
					 */
					sctp_move_chunks_from_net(stcb, net);
				}
				*reason_code = 6;
				/*-
				 * I add this line to be paranoid. As far as
				 * I can tell the continue, takes us back to
				 * the top of the for, but just to make sure
				 * I will reset these again here.
				 */
				ctl_cnt = bundle_at = 0;
				continue; /* This takes us back to the for() for the nets. */
			} else {
				asoc->ifp_had_enobuf = 0;
			}
			outchain = endoutchain = NULL;
			auth = NULL;
			auth_offset = 0;
			if (bundle_at || hbflag) {
				/* For data/asconf and hb set time */
				if (*now_filled == 0) {
					(void)SCTP_GETTIME_TIMEVAL(&net->last_sent_time);
					*now_filled = 1;
					*now = net->last_sent_time;
				} else {
					net->last_sent_time = *now;
				}
			}
			if (!no_out_cnt) {
				*num_out += (ctl_cnt + bundle_at);
			}
			if (bundle_at) {
				/* setup for a RTO measurement */
				tsns_sent = data_list[0]->rec.data.TSN_seq;
				/* fill time if not already filled */
				if (*now_filled == 0) {
					(void)SCTP_GETTIME_TIMEVAL(&asoc->time_last_sent);
					*now_filled = 1;
					*now = asoc->time_last_sent;
				} else {
					asoc->time_last_sent = *now;
				}
				if (net->rto_needed) {
					data_list[0]->do_rtt = 1;
					net->rto_needed = 0;
				}
				SCTP_STAT_INCR_BY(sctps_senddata, bundle_at);
				sctp_clean_up_datalist(stcb, asoc, data_list, bundle_at, net);
			}
			if (one_chunk) {
				break;
			}
		}
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_LOGGING_ENABLE) {
			sctp_log_cwnd(stcb, net, tsns_sent, SCTP_CWND_LOG_FROM_SEND);
		}
	}
	if (old_start_at == NULL) {
		old_start_at = start_at;
		start_at = TAILQ_FIRST(&asoc->nets);
		if (old_start_at)
			goto again_one_more_time;
	}

	/*
	 * At the end there should be no NON timed chunks hanging on this
	 * queue.
	 */
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_LOGGING_ENABLE) {
		sctp_log_cwnd(stcb, net, *num_out, SCTP_CWND_LOG_FROM_SEND);
	}
	if ((*num_out == 0) && (*reason_code == 0)) {
		*reason_code = 4;
	} else {
		*reason_code = 5;
	}
	sctp_clean_up_ctl(stcb, asoc, so_locked);
	return (0);
}

void
sctp_queue_op_err(struct sctp_tcb *stcb, struct mbuf *op_err)
{
	/*-
	 * Prepend a OPERATIONAL_ERROR chunk header and put on the end of
	 * the control chunk queue.
	 */
	struct sctp_chunkhdr *hdr;
	struct sctp_tmit_chunk *chk;
	struct mbuf *mat;

	SCTP_TCB_LOCK_ASSERT(stcb);
	sctp_alloc_a_chunk(stcb, chk);
	if (chk == NULL) {
		/* no memory */
		sctp_m_freem(op_err);
		return;
	}
	chk->copy_by_ref = 0;
	SCTP_BUF_PREPEND(op_err, sizeof(struct sctp_chunkhdr), M_DONTWAIT);
	if (op_err == NULL) {
		sctp_free_a_chunk(stcb, chk, SCTP_SO_NOT_LOCKED);
		return;
	}
	chk->send_size = 0;
	mat = op_err;
	while (mat != NULL) {
		chk->send_size += SCTP_BUF_LEN(mat);
		mat = SCTP_BUF_NEXT(mat);
	}
	chk->rec.chunk_id.id = SCTP_OPERATION_ERROR;
	chk->rec.chunk_id.can_take_data = 1;
	chk->sent = SCTP_DATAGRAM_UNSENT;
	chk->snd_count = 0;
	chk->flags = 0;
	chk->asoc = &stcb->asoc;
	chk->data = op_err;
	chk->whoTo = NULL;
	hdr = mtod(op_err, struct sctp_chunkhdr *);
	hdr->chunk_type = SCTP_OPERATION_ERROR;
	hdr->chunk_flags = 0;
	hdr->chunk_length = htons(chk->send_size);
	TAILQ_INSERT_TAIL(&chk->asoc->control_send_queue,
	    chk,
	    sctp_next);
	chk->asoc->ctrl_queue_cnt++;
}

int
sctp_send_cookie_echo(struct mbuf *m,
    int offset,
    struct sctp_tcb *stcb,
    struct sctp_nets *net)
{
	/*-
	 * pull out the cookie and put it at the front of the control chunk
	 * queue.
	 */
	int at;
	struct mbuf *cookie;
	struct sctp_paramhdr parm, *phdr;
	struct sctp_chunkhdr *hdr;
	struct sctp_tmit_chunk *chk;
	uint16_t ptype, plen;

	/* First find the cookie in the param area */
	cookie = NULL;
	at = offset + sizeof(struct sctp_init_chunk);

	SCTP_TCB_LOCK_ASSERT(stcb);
	do {
		phdr = sctp_get_next_param(m, at, &parm, sizeof(parm));
		if (phdr == NULL) {
			return (-3);
		}
		ptype = ntohs(phdr->param_type);
		plen = ntohs(phdr->param_length);
		if (ptype == SCTP_STATE_COOKIE) {
			int pad;

			/* found the cookie */
			if ((pad = (plen % 4))) {
				plen += 4 - pad;
			}
			cookie = SCTP_M_COPYM(m, at, plen, M_DONTWAIT);
			if (cookie == NULL) {
				/* No memory */
				return (-2);
			}
#ifdef SCTP_MBUF_LOGGING
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MBUF_LOGGING_ENABLE) {
				struct mbuf *mat;
				mat = cookie;
				while (mat) {
					if (SCTP_BUF_IS_EXTENDED(mat)) {
						sctp_log_mb(mat, SCTP_MBUF_ICOPY);
					}
					mat = SCTP_BUF_NEXT(mat);
				}
			}
#endif
			break;
		}
		at += SCTP_SIZE32(plen);
	} while (phdr);
	if (cookie == NULL) {
		/* Did not find the cookie */
		return (-3);
	}
	/* ok, we got the cookie lets change it into a cookie echo chunk */

	/* first the change from param to cookie */
	hdr = mtod(cookie, struct sctp_chunkhdr *);
	hdr->chunk_type = SCTP_COOKIE_ECHO;
	hdr->chunk_flags = 0;
	/* get the chunk stuff now and place it in the FRONT of the queue */
	sctp_alloc_a_chunk(stcb, chk);
	if (chk == NULL) {
		/* no memory */
		sctp_m_freem(cookie);
		return (-5);
	}
	chk->copy_by_ref = 0;
	chk->send_size = plen;
	chk->rec.chunk_id.id = SCTP_COOKIE_ECHO;
	chk->rec.chunk_id.can_take_data = 0;
	chk->sent = SCTP_DATAGRAM_UNSENT;
	chk->snd_count = 0;
	chk->flags = CHUNK_FLAGS_FRAGMENT_OK;
	chk->asoc = &stcb->asoc;
	chk->data = cookie;
	chk->whoTo = net;
	atomic_add_int(&chk->whoTo->ref_count, 1);
	TAILQ_INSERT_HEAD(&chk->asoc->control_send_queue, chk, sctp_next);
	chk->asoc->ctrl_queue_cnt++;
	return (0);
}

void
sctp_send_heartbeat_ack(struct sctp_tcb *stcb,
    struct mbuf *m,
    int offset,
    int chk_length,
    struct sctp_nets *net)
{
	/*
	 * take a HB request and make it into a HB ack and send it.
	 */
	struct mbuf *outchain;
	struct sctp_chunkhdr *chdr;
	struct sctp_tmit_chunk *chk;


	if (net == NULL)
		/* must have a net pointer */
		return;

	outchain = SCTP_M_COPYM(m, offset, chk_length, M_DONTWAIT);
	if (outchain == NULL) {
		/* gak out of memory */
		return;
	}
#ifdef SCTP_MBUF_LOGGING
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MBUF_LOGGING_ENABLE) {
		struct mbuf *mat;
		mat = outchain;
		while (mat) {
			if (SCTP_BUF_IS_EXTENDED(mat)) {
				sctp_log_mb(mat, SCTP_MBUF_ICOPY);
			}
			mat = SCTP_BUF_NEXT(mat);
		}
	}
#endif
	chdr = mtod(outchain, struct sctp_chunkhdr *);
	chdr->chunk_type = SCTP_HEARTBEAT_ACK;
	chdr->chunk_flags = 0;
	if (chk_length % 4) {
		/* need pad */
		uint32_t cpthis = 0;
		int padlen;

		padlen = 4 - (chk_length % 4);
		m_copyback(outchain, chk_length, padlen, (caddr_t)&cpthis);
	}
	sctp_alloc_a_chunk(stcb, chk);
	if (chk == NULL) {
		/* no memory */
		sctp_m_freem(outchain);
		return;
	}
	chk->copy_by_ref = 0;
	chk->send_size = chk_length;
	chk->rec.chunk_id.id = SCTP_HEARTBEAT_ACK;
	chk->rec.chunk_id.can_take_data = 1;
	chk->sent = SCTP_DATAGRAM_UNSENT;
	chk->snd_count = 0;
	chk->flags = 0;
	chk->asoc = &stcb->asoc;
	chk->data = outchain;
	chk->whoTo = net;
	atomic_add_int(&chk->whoTo->ref_count, 1);
	TAILQ_INSERT_TAIL(&chk->asoc->control_send_queue, chk, sctp_next);
	chk->asoc->ctrl_queue_cnt++;
}

void
sctp_send_cookie_ack(struct sctp_tcb *stcb)
{
	/* formulate and queue a cookie-ack back to sender */
	struct mbuf *cookie_ack;
	struct sctp_chunkhdr *hdr;
	struct sctp_tmit_chunk *chk;

	cookie_ack = NULL;
	SCTP_TCB_LOCK_ASSERT(stcb);

	cookie_ack = sctp_get_mbuf_for_msg(sizeof(struct sctp_chunkhdr), 0, M_DONTWAIT, 1, MT_HEADER);
	if (cookie_ack == NULL) {
		/* no mbuf's */
		return;
	}
	SCTP_BUF_RESV_UF(cookie_ack, SCTP_MIN_OVERHEAD);
	sctp_alloc_a_chunk(stcb, chk);
	if (chk == NULL) {
		/* no memory */
		sctp_m_freem(cookie_ack);
		return;
	}
	chk->copy_by_ref = 0;
	chk->send_size = sizeof(struct sctp_chunkhdr);
	chk->rec.chunk_id.id = SCTP_COOKIE_ACK;
	chk->rec.chunk_id.can_take_data = 1;
	chk->sent = SCTP_DATAGRAM_UNSENT;
	chk->snd_count = 0;
	chk->flags = 0;
	chk->asoc = &stcb->asoc;
	chk->data = cookie_ack;
	if (chk->asoc->last_control_chunk_from != NULL) {
		chk->whoTo = chk->asoc->last_control_chunk_from;
		atomic_add_int(&chk->whoTo->ref_count, 1);
	} else {
		chk->whoTo = NULL;
	}
	hdr = mtod(cookie_ack, struct sctp_chunkhdr *);
	hdr->chunk_type = SCTP_COOKIE_ACK;
	hdr->chunk_flags = 0;
	hdr->chunk_length = htons(chk->send_size);
	SCTP_BUF_LEN(cookie_ack) = chk->send_size;
	TAILQ_INSERT_TAIL(&chk->asoc->control_send_queue, chk, sctp_next);
	chk->asoc->ctrl_queue_cnt++;
	return;
}


void
sctp_send_shutdown_ack(struct sctp_tcb *stcb, struct sctp_nets *net)
{
	/* formulate and queue a SHUTDOWN-ACK back to the sender */
	struct mbuf *m_shutdown_ack;
	struct sctp_shutdown_ack_chunk *ack_cp;
	struct sctp_tmit_chunk *chk;

	m_shutdown_ack = sctp_get_mbuf_for_msg(sizeof(struct sctp_shutdown_ack_chunk), 0, M_DONTWAIT, 1, MT_HEADER);
	if (m_shutdown_ack == NULL) {
		/* no mbuf's */
		return;
	}
	SCTP_BUF_RESV_UF(m_shutdown_ack, SCTP_MIN_OVERHEAD);
	sctp_alloc_a_chunk(stcb, chk);
	if (chk == NULL) {
		/* no memory */
		sctp_m_freem(m_shutdown_ack);
		return;
	}
	chk->copy_by_ref = 0;
	chk->send_size = sizeof(struct sctp_chunkhdr);
	chk->rec.chunk_id.id = SCTP_SHUTDOWN_ACK;
	chk->rec.chunk_id.can_take_data = 1;
	chk->sent = SCTP_DATAGRAM_UNSENT;
	chk->snd_count = 0;
	chk->flags = 0;
	chk->asoc = &stcb->asoc;
	chk->data = m_shutdown_ack;
	chk->whoTo = net;
	if (chk->whoTo) {
		atomic_add_int(&chk->whoTo->ref_count, 1);
	}
	ack_cp = mtod(m_shutdown_ack, struct sctp_shutdown_ack_chunk *);
	ack_cp->ch.chunk_type = SCTP_SHUTDOWN_ACK;
	ack_cp->ch.chunk_flags = 0;
	ack_cp->ch.chunk_length = htons(chk->send_size);
	SCTP_BUF_LEN(m_shutdown_ack) = chk->send_size;
	TAILQ_INSERT_TAIL(&chk->asoc->control_send_queue, chk, sctp_next);
	chk->asoc->ctrl_queue_cnt++;
	return;
}

void
sctp_send_shutdown(struct sctp_tcb *stcb, struct sctp_nets *net)
{
	/* formulate and queue a SHUTDOWN to the sender */
	struct mbuf *m_shutdown;
	struct sctp_shutdown_chunk *shutdown_cp;
	struct sctp_tmit_chunk *chk;

	m_shutdown = sctp_get_mbuf_for_msg(sizeof(struct sctp_shutdown_chunk), 0, M_DONTWAIT, 1, MT_HEADER);
	if (m_shutdown == NULL) {
		/* no mbuf's */
		return;
	}
	SCTP_BUF_RESV_UF(m_shutdown, SCTP_MIN_OVERHEAD);
	sctp_alloc_a_chunk(stcb, chk);
	if (chk == NULL) {
		/* no memory */
		sctp_m_freem(m_shutdown);
		return;
	}
	chk->copy_by_ref = 0;
	chk->send_size = sizeof(struct sctp_shutdown_chunk);
	chk->rec.chunk_id.id = SCTP_SHUTDOWN;
	chk->rec.chunk_id.can_take_data = 1;
	chk->sent = SCTP_DATAGRAM_UNSENT;
	chk->snd_count = 0;
	chk->flags = 0;
	chk->asoc = &stcb->asoc;
	chk->data = m_shutdown;
	chk->whoTo = net;
	if (chk->whoTo) {
		atomic_add_int(&chk->whoTo->ref_count, 1);
	}
	shutdown_cp = mtod(m_shutdown, struct sctp_shutdown_chunk *);
	shutdown_cp->ch.chunk_type = SCTP_SHUTDOWN;
	shutdown_cp->ch.chunk_flags = 0;
	shutdown_cp->ch.chunk_length = htons(chk->send_size);
	shutdown_cp->cumulative_tsn_ack = htonl(stcb->asoc.cumulative_tsn);
	SCTP_BUF_LEN(m_shutdown) = chk->send_size;
	TAILQ_INSERT_TAIL(&chk->asoc->control_send_queue, chk, sctp_next);
	chk->asoc->ctrl_queue_cnt++;
	return;
}

void
sctp_send_asconf(struct sctp_tcb *stcb, struct sctp_nets *net, int addr_locked)
{
	/*
	 * formulate and queue an ASCONF to the peer.
	 * ASCONF parameters should be queued on the assoc queue.
	 */
	struct sctp_tmit_chunk *chk;
	struct mbuf *m_asconf;
	int len;

	SCTP_TCB_LOCK_ASSERT(stcb);

	if ((!TAILQ_EMPTY(&stcb->asoc.asconf_send_queue)) &&
	    (!sctp_is_feature_on(stcb->sctp_ep, SCTP_PCB_FLAGS_MULTIPLE_ASCONFS))) {
		/* can't send a new one if there is one in flight already */
		return;
	}

	/* compose an ASCONF chunk, maximum length is PMTU */
	m_asconf = sctp_compose_asconf(stcb, &len, addr_locked);
	if (m_asconf == NULL) {
		return;
	}

	sctp_alloc_a_chunk(stcb, chk);
	if (chk == NULL) {
		/* no memory */
		sctp_m_freem(m_asconf);
		return;
	}

	chk->copy_by_ref = 0;
	chk->data = m_asconf;
	chk->send_size = len;
	chk->rec.chunk_id.id = SCTP_ASCONF;
	chk->rec.chunk_id.can_take_data = 0;
	chk->sent = SCTP_DATAGRAM_UNSENT;
	chk->snd_count = 0;
	chk->flags = CHUNK_FLAGS_FRAGMENT_OK;
	chk->asoc = &stcb->asoc;
	chk->whoTo = net;
	if (chk->whoTo) {
		atomic_add_int(&chk->whoTo->ref_count, 1);
	}
	TAILQ_INSERT_TAIL(&chk->asoc->asconf_send_queue, chk, sctp_next);
	chk->asoc->ctrl_queue_cnt++;
	return;
}

void
sctp_send_asconf_ack(struct sctp_tcb *stcb)
{
	/*
	 * formulate and queue a asconf-ack back to sender.
	 * the asconf-ack must be stored in the tcb.
	 */
	struct sctp_tmit_chunk *chk;
	struct sctp_asconf_ack *ack, *latest_ack;
	struct mbuf *m_ack, *m;
	struct sctp_nets *net = NULL;

	SCTP_TCB_LOCK_ASSERT(stcb);
	/* Get the latest ASCONF-ACK */
	latest_ack = TAILQ_LAST(&stcb->asoc.asconf_ack_sent, sctp_asconf_ackhead);
	if (latest_ack == NULL) {
		return;
	}
	if (latest_ack->last_sent_to != NULL &&
	    latest_ack->last_sent_to == stcb->asoc.last_control_chunk_from) {
		/* we're doing a retransmission */
		net = sctp_find_alternate_net(stcb, stcb->asoc.last_control_chunk_from, 0);
		if (net == NULL) {
			/* no alternate */
			if (stcb->asoc.last_control_chunk_from == NULL) {
				if (stcb->asoc.alternate) {
					net = stcb->asoc.alternate;
				} else {
					net = stcb->asoc.primary_destination;
				}
			} else {
				net = stcb->asoc.last_control_chunk_from;
			}
		}
	} else {
		/* normal case */
		if (stcb->asoc.last_control_chunk_from == NULL) {
			if (stcb->asoc.alternate) {
				net = stcb->asoc.alternate;
			} else {
				net = stcb->asoc.primary_destination;
			}
		} else {
			net = stcb->asoc.last_control_chunk_from;
		}
	}
	latest_ack->last_sent_to = net;

	TAILQ_FOREACH(ack, &stcb->asoc.asconf_ack_sent, next) {
		if (ack->data == NULL) {
			continue;
		}

		/* copy the asconf_ack */
		m_ack = SCTP_M_COPYM(ack->data, 0, M_COPYALL, M_DONTWAIT);
		if (m_ack == NULL) {
			/* couldn't copy it */
			return;
		}
#ifdef SCTP_MBUF_LOGGING
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_MBUF_LOGGING_ENABLE) {
			struct mbuf *mat;
			mat = m_ack;
			while (mat) {
				if (SCTP_BUF_IS_EXTENDED(mat)) {
					sctp_log_mb(mat, SCTP_MBUF_ICOPY);
				}
				mat = SCTP_BUF_NEXT(mat);
			}
		}
#endif

		sctp_alloc_a_chunk(stcb, chk);
		if (chk == NULL) {
			/* no memory */
			if (m_ack)
				sctp_m_freem(m_ack);
			return;
		}
		chk->copy_by_ref = 0;

		chk->whoTo = net;
		if (chk->whoTo) {
			atomic_add_int(&chk->whoTo->ref_count, 1);
		}
		chk->data = m_ack;
		chk->send_size = 0;
		/* Get size */
		m = m_ack;
		chk->send_size = ack->len;
		chk->rec.chunk_id.id = SCTP_ASCONF_ACK;
		chk->rec.chunk_id.can_take_data = 1;
		chk->sent = SCTP_DATAGRAM_UNSENT;
		chk->snd_count = 0;
		chk->flags |= CHUNK_FLAGS_FRAGMENT_OK; /* XXX */
		chk->asoc = &stcb->asoc;

		TAILQ_INSERT_TAIL(&chk->asoc->control_send_queue, chk, sctp_next);
		chk->asoc->ctrl_queue_cnt++;
	}
	return;
}


static int
sctp_chunk_retransmission(struct sctp_inpcb *inp,
    struct sctp_tcb *stcb,
    struct sctp_association *asoc,
    int *cnt_out, struct timeval *now, int *now_filled, int *fr_done, int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
    SCTP_UNUSED
#endif
    )
{
	/*-
	 * send out one MTU of retransmission. If fast_retransmit is
	 * happening we ignore the cwnd. Otherwise we obey the cwnd and
	 * rwnd. For a Cookie or Asconf in the control chunk queue we
	 * retransmit them by themselves.
	 *
	 * For data chunks we will pick out the lowest TSN's in the sent_queue
	 * marked for resend and bundle them all together (up to a MTU of
	 * destination). The address to send to should have been
	 * selected/changed where the retransmission was marked (i.e. in FR
	 * or t3-timeout routines).
	 */
	struct sctp_tmit_chunk *data_list[SCTP_MAX_DATA_BUNDLING];
	struct sctp_tmit_chunk *chk, *fwd;
	struct mbuf *m, *endofchain;
	struct sctp_nets *net=NULL;
	uint32_t tsns_sent = 0;
	int no_fragmentflg, bundle_at, cnt_thru;
	unsigned int mtu;
	int error, i, one_chunk, fwd_tsn, ctl_cnt, tmr_started;
	struct sctp_auth_chunk *auth = NULL;
	uint32_t auth_offset = 0;
	uint16_t auth_keyid;
	int override_ok = 1;
	int data_auth_reqd = 0;
	uint32_t dmtu = 0;

#if defined(__APPLE__)
	if (so_locked) {
		sctp_lock_assert(SCTP_INP_SO(inp));
	} else {
		sctp_unlock_assert(SCTP_INP_SO(inp));
	}
#endif
	SCTP_TCB_LOCK_ASSERT(stcb);
	tmr_started = ctl_cnt = bundle_at = error = 0;
	no_fragmentflg = 1;
	fwd_tsn = 0;
	*cnt_out = 0;
	fwd = NULL;
	endofchain = m = NULL;
	auth_keyid = stcb->asoc.authinfo.active_keyid;
#ifdef SCTP_AUDITING_ENABLED
	sctp_audit_log(0xC3, 1);
#endif
	if ((TAILQ_EMPTY(&asoc->sent_queue)) &&
	    (TAILQ_EMPTY(&asoc->control_send_queue))) {
		SCTPDBG(SCTP_DEBUG_OUTPUT1,"SCTP hits empty queue with cnt set to %d?\n",
			asoc->sent_queue_retran_cnt);
		asoc->sent_queue_cnt = 0;
		asoc->sent_queue_cnt_removeable = 0;
		/* send back 0/0 so we enter normal transmission */
		*cnt_out = 0;
		return(0);
	}
	TAILQ_FOREACH(chk, &asoc->control_send_queue, sctp_next) {
		if ((chk->rec.chunk_id.id == SCTP_COOKIE_ECHO) ||
		    (chk->rec.chunk_id.id == SCTP_STREAM_RESET) ||
		    (chk->rec.chunk_id.id == SCTP_FORWARD_CUM_TSN)) {
			if (chk->sent != SCTP_DATAGRAM_RESEND) {
				continue;
			}
			if (chk->rec.chunk_id.id == SCTP_STREAM_RESET) {
				if (chk != asoc->str_reset) {
					/*
					 * not eligible for retran if its
					 * not ours
					 */
					continue;
				}
			}
			ctl_cnt++;
			if (chk->rec.chunk_id.id == SCTP_FORWARD_CUM_TSN) {
				fwd_tsn = 1;
				fwd = chk;
			}
			/*
			 * Add an AUTH chunk, if chunk requires it save the
			 * offset into the chain for AUTH
			 */
			if ((auth == NULL) &&
			    (sctp_auth_is_required_chunk(chk->rec.chunk_id.id,
							 stcb->asoc.peer_auth_chunks))) {
				m = sctp_add_auth_chunk(m, &endofchain,
							&auth, &auth_offset,
							stcb,
							chk->rec.chunk_id.id);
				SCTP_STAT_INCR_COUNTER64(sctps_outcontrolchunks);
			}
			m = sctp_copy_mbufchain(chk->data, m, &endofchain, 0, chk->send_size, chk->copy_by_ref);
			break;
		}
	}
	one_chunk = 0;
	cnt_thru = 0;
	/* do we have control chunks to retransmit? */
	if (m != NULL) {
		/* Start a timer no matter if we suceed or fail */
		if (chk->rec.chunk_id.id == SCTP_COOKIE_ECHO) {
			sctp_timer_start(SCTP_TIMER_TYPE_COOKIE, inp, stcb, chk->whoTo);
		} else if (chk->rec.chunk_id.id == SCTP_ASCONF)
			sctp_timer_start(SCTP_TIMER_TYPE_ASCONF, inp, stcb, chk->whoTo);
		chk->snd_count++;	/* update our count */
		if ((error = sctp_lowlevel_chunk_output(inp, stcb, chk->whoTo,
		                                        (struct sockaddr *)&chk->whoTo->ro._l_addr, m,
		                                        auth_offset, auth, stcb->asoc.authinfo.active_keyid,
		                                        no_fragmentflg, 0, NULL, 0,
		                                        inp->sctp_lport, stcb->rport, htonl(stcb->asoc.peer_vtag),
		                                        chk->whoTo->port, so_locked, NULL, NULL))) {
			SCTP_STAT_INCR(sctps_lowlevelerr);
			return (error);
		}
		m = endofchain = NULL;
		auth = NULL;
		auth_offset = 0;
		/*
		 * We don't want to mark the net->sent time here since this
		 * we use this for HB and retrans cannot measure RTT
		 */
		/* (void)SCTP_GETTIME_TIMEVAL(&chk->whoTo->last_sent_time); */
		*cnt_out += 1;
		chk->sent = SCTP_DATAGRAM_SENT;
		sctp_ucount_decr(stcb->asoc.sent_queue_retran_cnt);
		if (fwd_tsn == 0) {
			return (0);
		} else {
			/* Clean up the fwd-tsn list */
			sctp_clean_up_ctl(stcb, asoc, so_locked);
			return (0);
		}
	}
	/*
	 * Ok, it is just data retransmission we need to do or that and a
	 * fwd-tsn with it all.
	 */
	if (TAILQ_EMPTY(&asoc->sent_queue)) {
		return (SCTP_RETRAN_DONE);
	}
	if ((SCTP_GET_STATE(asoc) == SCTP_STATE_COOKIE_ECHOED) ||
	    (SCTP_GET_STATE(asoc) == SCTP_STATE_COOKIE_WAIT)) {
		/* not yet open, resend the cookie and that is it */
		return (1);
	}
#ifdef SCTP_AUDITING_ENABLED
	sctp_auditing(20, inp, stcb, NULL);
#endif
	data_auth_reqd = sctp_auth_is_required_chunk(SCTP_DATA, stcb->asoc.peer_auth_chunks);
	TAILQ_FOREACH(chk, &asoc->sent_queue, sctp_next) {
		if (chk->sent != SCTP_DATAGRAM_RESEND) {
			/* No, not sent to this net or not ready for rtx */
			continue;
		}
		if (chk->data == NULL) {
			printf("TSN:%x chk->snd_count:%d chk->sent:%d can't retran - no data\n",
			       chk->rec.data.TSN_seq, chk->snd_count, chk->sent);
			continue;
		}
		if ((SCTP_BASE_SYSCTL(sctp_max_retran_chunk)) &&
		    (chk->snd_count >= SCTP_BASE_SYSCTL(sctp_max_retran_chunk))) {
			/* Gak, we have exceeded max unlucky retran, abort! */
			SCTP_PRINTF("Gak, chk->snd_count:%d >= max:%d - send abort\n",
				    chk->snd_count,
				    SCTP_BASE_SYSCTL(sctp_max_retran_chunk));
			atomic_add_int(&stcb->asoc.refcnt, 1);
			sctp_abort_an_association(stcb->sctp_ep, stcb, 0, NULL, so_locked);
			SCTP_TCB_LOCK(stcb);
			atomic_subtract_int(&stcb->asoc.refcnt, 1);
			return (SCTP_RETRAN_EXIT);
		}
		/* pick up the net */
		net = chk->whoTo;
		if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) {
			mtu = (net->mtu - SCTP_MIN_OVERHEAD);
		} else {
			mtu = net->mtu - SCTP_MIN_V4_OVERHEAD;
		}

		if ((asoc->peers_rwnd < mtu) && (asoc->total_flight > 0)) {
			/* No room in peers rwnd */
			uint32_t tsn;

			tsn = asoc->last_acked_seq + 1;
			if (tsn == chk->rec.data.TSN_seq) {
				/*
				 * we make a special exception for this
				 * case. The peer has no rwnd but is missing
				 * the lowest chunk.. which is probably what
				 * is holding up the rwnd.
				 */
				goto one_chunk_around;
			}
			return (1);
		}
	one_chunk_around:
		if (asoc->peers_rwnd < mtu) {
			one_chunk = 1;
			if ((asoc->peers_rwnd == 0) &&
			    (asoc->total_flight == 0)) {
				chk->window_probe = 1;
				chk->whoTo->window_probe = 1;
			}
		}
#ifdef SCTP_AUDITING_ENABLED
		sctp_audit_log(0xC3, 2);
#endif
		bundle_at = 0;
		m = NULL;
		net->fast_retran_ip = 0;
		if (chk->rec.data.doing_fast_retransmit == 0) {
			/*
			 * if no FR in progress skip destination that have
			 * flight_size > cwnd.
			 */
			if (net->flight_size >= net->cwnd) {
				continue;
			}
		} else {
			/*
			 * Mark the destination net to have FR recovery
			 * limits put on it.
			 */
			*fr_done = 1;
			net->fast_retran_ip = 1;
		}

		/*
		 * if no AUTH is yet included and this chunk requires it,
		 * make sure to account for it.  We don't apply the size
		 * until the AUTH chunk is actually added below in case
		 * there is no room for this chunk.
		 */
		if (data_auth_reqd && (auth == NULL)) {
			dmtu = sctp_get_auth_chunk_len(stcb->asoc.peer_hmac_id);
		} else
			dmtu = 0;

		if ((chk->send_size <= (mtu - dmtu)) ||
		    (chk->flags & CHUNK_FLAGS_FRAGMENT_OK)) {
			/* ok we will add this one */
			if (data_auth_reqd) {
				if (auth == NULL) {
					m = sctp_add_auth_chunk(m,
								&endofchain,
								&auth,
								&auth_offset,
								stcb,
								SCTP_DATA);
					auth_keyid = chk->auth_keyid;
					override_ok = 0;
					SCTP_STAT_INCR_COUNTER64(sctps_outcontrolchunks);
				} else if (override_ok) {
					auth_keyid = chk->auth_keyid;
					override_ok = 0;
				} else if (chk->auth_keyid != auth_keyid) {
					/* different keyid, so done bundling */
					break;
				}
			}
			m = sctp_copy_mbufchain(chk->data, m, &endofchain, 0, chk->send_size, chk->copy_by_ref);
			if (m == NULL) {
				SCTP_LTRACE_ERR_RET(inp, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, ENOMEM);
				return (ENOMEM);
			}
			/* Do clear IP_DF ? */
			if (chk->flags & CHUNK_FLAGS_FRAGMENT_OK) {
				no_fragmentflg = 0;
			}
			/* upate our MTU size */
			if (mtu > (chk->send_size + dmtu))
				mtu -= (chk->send_size + dmtu);
			else
				mtu = 0;
			data_list[bundle_at++] = chk;
			if (one_chunk && (asoc->total_flight <= 0)) {
				SCTP_STAT_INCR(sctps_windowprobed);
			}
		}
		if (one_chunk == 0) {
			/*
			 * now are there anymore forward from chk to pick
			 * up?
			 */
			fwd = TAILQ_NEXT(chk, sctp_next);
			while (fwd) {
				if (fwd->sent != SCTP_DATAGRAM_RESEND) {
					/* Nope, not for retran */
					fwd = TAILQ_NEXT(fwd, sctp_next);
					continue;
				}
				if (fwd->whoTo != net) {
					/* Nope, not the net in question */
					fwd = TAILQ_NEXT(fwd, sctp_next);
					continue;
				}
				if (data_auth_reqd && (auth == NULL)) {
					dmtu = sctp_get_auth_chunk_len(stcb->asoc.peer_hmac_id);
				} else
					dmtu = 0;
				if (fwd->send_size <= (mtu - dmtu)) {
					if (data_auth_reqd) {
						if (auth == NULL) {
							m = sctp_add_auth_chunk(m,
										&endofchain,
										&auth,
										&auth_offset,
										stcb,
										SCTP_DATA);
							auth_keyid = fwd->auth_keyid;
							override_ok = 0;
							SCTP_STAT_INCR_COUNTER64(sctps_outcontrolchunks);
						} else if (override_ok) {
							auth_keyid = fwd->auth_keyid;
							override_ok = 0;
						} else if (fwd->auth_keyid != auth_keyid) {
							/* different keyid, so done bundling */
							break;
						}
					}
					m = sctp_copy_mbufchain(fwd->data, m, &endofchain, 0, fwd->send_size, fwd->copy_by_ref);
					if (m == NULL) {
						SCTP_LTRACE_ERR_RET(inp, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, ENOMEM);
						return (ENOMEM);
					}
					/* Do clear IP_DF ? */
					if (fwd->flags & CHUNK_FLAGS_FRAGMENT_OK) {
						no_fragmentflg = 0;
					}
					/* upate our MTU size */
					if (mtu > (fwd->send_size + dmtu))
						mtu -= (fwd->send_size + dmtu);
					else
						mtu = 0;
					data_list[bundle_at++] = fwd;
					if (bundle_at >= SCTP_MAX_DATA_BUNDLING) {
						break;
					}
					fwd = TAILQ_NEXT(fwd, sctp_next);
				} else {
					/* can't fit so we are done */
					break;
				}
			}
		}
		/* Is there something to send for this destination? */
		if (m) {
			/*
			 * No matter if we fail/or suceed we should start a
			 * timer. A failure is like a lost IP packet :-)
			 */
			if (!SCTP_OS_TIMER_PENDING(&net->rxt_timer.timer)) {
				/*
				 * no timer running on this destination
				 * restart it.
				 */
				sctp_timer_start(SCTP_TIMER_TYPE_SEND, inp, stcb, net);
				tmr_started = 1;
			}
			/* Now lets send it, if there is anything to send :> */
			if ((error = sctp_lowlevel_chunk_output(inp, stcb, net,
			                                        (struct sockaddr *)&net->ro._l_addr, m,
			                                        auth_offset, auth, auth_keyid,
			                                        no_fragmentflg, 0, NULL, 0,
			                                        inp->sctp_lport, stcb->rport, htonl(stcb->asoc.peer_vtag),
			                                        net->port, so_locked, NULL, NULL))) {
				/* error, we could not output */
				SCTP_STAT_INCR(sctps_lowlevelerr);
				return (error);
			}
			m = endofchain = NULL;
			auth = NULL;
			auth_offset = 0;
			/* For HB's */
			/*
			 * We don't want to mark the net->sent time here
			 * since this we use this for HB and retrans cannot
			 * measure RTT
			 */
			/* (void)SCTP_GETTIME_TIMEVAL(&net->last_sent_time); */

			/* For auto-close */
			cnt_thru++;
			if (*now_filled == 0) {
				(void)SCTP_GETTIME_TIMEVAL(&asoc->time_last_sent);
				*now = asoc->time_last_sent;
				*now_filled = 1;
			} else {
				asoc->time_last_sent = *now;
			}
			*cnt_out += bundle_at;
#ifdef SCTP_AUDITING_ENABLED
			sctp_audit_log(0xC4, bundle_at);
#endif
			if (bundle_at) {
				tsns_sent = data_list[0]->rec.data.TSN_seq;
			}
			for (i = 0; i < bundle_at; i++) {
				SCTP_STAT_INCR(sctps_sendretransdata);
				data_list[i]->sent = SCTP_DATAGRAM_SENT;
				/*
				 * When we have a revoked data, and we
				 * retransmit it, then we clear the revoked
				 * flag since this flag dictates if we
				 * subtracted from the fs
				 */
				if (data_list[i]->rec.data.chunk_was_revoked) {
					/* Deflate the cwnd */
					data_list[i]->whoTo->cwnd -= data_list[i]->book_size;
					data_list[i]->rec.data.chunk_was_revoked = 0;
				}
				data_list[i]->snd_count++;
				sctp_ucount_decr(asoc->sent_queue_retran_cnt);
				/* record the time */
				data_list[i]->sent_rcv_time = asoc->time_last_sent;
				if (data_list[i]->book_size_scale) {
					/*
					 * need to double the book size on
					 * this one
					 */
					data_list[i]->book_size_scale = 0;
					/* Since we double the booksize, we must
					 * also double the output queue size, since this
					 * get shrunk when we free by this amount.
					 */
					atomic_add_int(&((asoc)->total_output_queue_size),data_list[i]->book_size);
					data_list[i]->book_size *= 2;


				} else {
					if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LOG_RWND_ENABLE) {
						sctp_log_rwnd(SCTP_DECREASE_PEER_RWND,
						      asoc->peers_rwnd, data_list[i]->send_size, SCTP_BASE_SYSCTL(sctp_peer_chunk_oh));
					}
					asoc->peers_rwnd = sctp_sbspace_sub(asoc->peers_rwnd,
									    (uint32_t) (data_list[i]->send_size +
											SCTP_BASE_SYSCTL(sctp_peer_chunk_oh)));
				}
				if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_FLIGHT_LOGGING_ENABLE) {
					sctp_misc_ints(SCTP_FLIGHT_LOG_UP_RSND,
						       data_list[i]->whoTo->flight_size,
						       data_list[i]->book_size,
						       (uintptr_t)data_list[i]->whoTo,
						       data_list[i]->rec.data.TSN_seq);
				}
				sctp_flight_size_increase(data_list[i]);
				sctp_total_flight_increase(stcb, data_list[i]);
				if (asoc->peers_rwnd < stcb->sctp_ep->sctp_ep.sctp_sws_sender) {
					/* SWS sender side engages */
					asoc->peers_rwnd = 0;
				}
				if ((i == 0) &&
				    (data_list[i]->rec.data.doing_fast_retransmit)) {
					SCTP_STAT_INCR(sctps_sendfastretrans);
					if ((data_list[i] == TAILQ_FIRST(&asoc->sent_queue)) &&
					    (tmr_started == 0)) {
						/*-
						 * ok we just fast-retrans'd
						 * the lowest TSN, i.e the
						 * first on the list. In
						 * this case we want to give
						 * some more time to get a
						 * SACK back without a
						 * t3-expiring.
						 */
						sctp_timer_stop(SCTP_TIMER_TYPE_SEND, inp, stcb, net,
								SCTP_FROM_SCTP_OUTPUT+SCTP_LOC_4 );
						sctp_timer_start(SCTP_TIMER_TYPE_SEND, inp, stcb, net);
					}
				}
			}
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_LOGGING_ENABLE) {
				sctp_log_cwnd(stcb, net, tsns_sent, SCTP_CWND_LOG_FROM_RESEND);
			}
#ifdef SCTP_AUDITING_ENABLED
			sctp_auditing(21, inp, stcb, NULL);
#endif
		} else {
			/* None will fit */
			return (1);
		}
		if (asoc->sent_queue_retran_cnt <= 0) {
			/* all done we have no more to retran */
			asoc->sent_queue_retran_cnt = 0;
			break;
		}
		if (one_chunk) {
			/* No more room in rwnd */
			return (1);
		}
		/* stop the for loop here. we sent out a packet */
		break;
	}
	return (0);
}


static int
sctp_timer_validation(struct sctp_inpcb *inp,
    struct sctp_tcb *stcb,
    struct sctp_association *asoc,
    int ret)
{
	struct sctp_nets *net;

	/* Validate that a timer is running somewhere */
	TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
		if (SCTP_OS_TIMER_PENDING(&net->rxt_timer.timer)) {
			/* Here is a timer */
			return (ret);
		}
	}
	SCTP_TCB_LOCK_ASSERT(stcb);
	/* Gak, we did not have a timer somewhere */
	SCTPDBG(SCTP_DEBUG_OUTPUT3, "Deadlock avoided starting timer on a dest at retran\n");
	if (asoc->alternate) {
		sctp_timer_start(SCTP_TIMER_TYPE_SEND, inp, stcb, asoc->alternate);
	} else {
		sctp_timer_start(SCTP_TIMER_TYPE_SEND, inp, stcb, asoc->primary_destination);
	}
	return (ret);
}

void
sctp_chunk_output (struct sctp_inpcb *inp,
    struct sctp_tcb *stcb,
    int from_where,
    int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
    SCTP_UNUSED
#endif
    )
{
	/*-
	 * Ok this is the generic chunk service queue. we must do the
	 * following:
	 * - See if there are retransmits pending, if so we must
	 *   do these first.
	 * - Service the stream queue that is next, moving any
	 *   message (note I must get a complete message i.e.
	 *   FIRST/MIDDLE and LAST to the out queue in one pass) and assigning
	 *   TSN's
	 * - Check to see if the cwnd/rwnd allows any output, if so we
	 *   go ahead and fomulate and send the low level chunks. Making sure
	 *   to combine any control in the control chunk queue also.
	 */
	struct sctp_association *asoc;
	struct sctp_nets *net;
	int error=0, num_out=0, tot_out=0, ret=0, reason_code=0;
	unsigned int burst_cnt=0;
	struct timeval now;
	int now_filled = 0;
	int nagle_on = 0;
	int frag_point = sctp_get_frag_point(stcb, &stcb->asoc);
	int un_sent=0;
	int fr_done;
	unsigned int tot_frs=0;

#if defined(__APPLE__)
	if (so_locked) {
		sctp_lock_assert(SCTP_INP_SO(inp));
	} else {
		sctp_unlock_assert(SCTP_INP_SO(inp));
	}
#endif
	asoc = &stcb->asoc;
	if (from_where == SCTP_OUTPUT_FROM_USR_SEND) {
		if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_NODELAY)) {
			nagle_on = 0;
		} else {
			nagle_on = 1;
		}
	}
	SCTP_TCB_LOCK_ASSERT(stcb);

	un_sent = (stcb->asoc.total_output_queue_size - stcb->asoc.total_flight);

	if ((un_sent <= 0) &&
	    (TAILQ_EMPTY(&asoc->control_send_queue)) &&
	    (TAILQ_EMPTY(&asoc->asconf_send_queue)) &&
	    (asoc->sent_queue_retran_cnt == 0)) {
		/* Nothing to do unless there is something to be sent left */
		return;
	}
	/* Do we have something to send, data or control AND
	 * a sack timer running, if so piggy-back the sack.
	 */
 	if (SCTP_OS_TIMER_PENDING(&stcb->asoc.dack_timer.timer)) {
		sctp_send_sack(stcb, so_locked);
		(void)SCTP_OS_TIMER_STOP(&stcb->asoc.dack_timer.timer);
	}
	while (asoc->sent_queue_retran_cnt) {
		/*-
		 * Ok, it is retransmission time only, we send out only ONE
		 * packet with a single call off to the retran code.
		 */
		if (from_where == SCTP_OUTPUT_FROM_COOKIE_ACK) {
			/*-
			 * Special hook for handling cookiess discarded
			 * by peer that carried data. Send cookie-ack only
			 * and then the next call with get the retran's.
			 */
 			(void)sctp_med_chunk_output(inp, stcb, asoc, &num_out, &reason_code, 1,
						    from_where,
						    &now, &now_filled, frag_point, so_locked);
			return;
		} else if (from_where != SCTP_OUTPUT_FROM_HB_TMR) {
			/* if its not from a HB then do it */
			fr_done = 0;
			ret = sctp_chunk_retransmission(inp, stcb, asoc, &num_out, &now, &now_filled, &fr_done, so_locked);
			if (fr_done) {
				tot_frs++;
			}
		} else {
			/*
			 * its from any other place, we don't allow retran
			 * output (only control)
			 */
			ret = 1;
		}
		if (ret > 0) {
			/* Can't send anymore */
			/*-
			 * now lets push out control by calling med-level
			 * output once. this assures that we WILL send HB's
			 * if queued too.
			 */
			(void)sctp_med_chunk_output(inp, stcb, asoc, &num_out, &reason_code, 1,
						    from_where,
						    &now, &now_filled, frag_point, so_locked);
#ifdef SCTP_AUDITING_ENABLED
			sctp_auditing(8, inp, stcb, NULL);
#endif
			(void)sctp_timer_validation(inp, stcb, asoc, ret);
			return;
		}
		if (ret < 0) {
			/*-
			 * The count was off.. retran is not happening so do
			 * the normal retransmission.
			 */
#ifdef SCTP_AUDITING_ENABLED
			sctp_auditing(9, inp, stcb, NULL);
#endif
			if (ret == SCTP_RETRAN_EXIT) {
				return;
			}
			break;
		}
		if (from_where == SCTP_OUTPUT_FROM_T3) {
			/* Only one transmission allowed out of a timeout */
#ifdef SCTP_AUDITING_ENABLED
			sctp_auditing(10, inp, stcb, NULL);
#endif
			/* Push out any control */
			(void)sctp_med_chunk_output(inp, stcb, asoc, &num_out, &reason_code, 1, from_where,
						    &now, &now_filled, frag_point, so_locked);
			return;
		}
		if ((asoc->fr_max_burst > 0) && (tot_frs >= asoc->fr_max_burst)) {
			/* Hit FR burst limit */
			return;
		}
		if ((num_out == 0) && (ret == 0)) {
			/* No more retrans to send */
			break;
		}
	}
#ifdef SCTP_AUDITING_ENABLED
	sctp_auditing(12, inp, stcb, NULL);
#endif
	/* Check for bad destinations, if they exist move chunks around. */
	TAILQ_FOREACH(net, &asoc->nets, sctp_next) {
		if (!(net->dest_state & SCTP_ADDR_REACHABLE)) {
			/*-
			 * if possible move things off of this address we
			 * still may send below due to the dormant state but
			 * we try to find an alternate address to send to
			 * and if we have one we move all queued data on the
			 * out wheel to this alternate address.
			 */
			if (net->ref_count > 1)
				sctp_move_chunks_from_net(stcb, net);
		} else {
			/*-
			 * if ((asoc->sat_network) || (net->addr_is_local))
			 * { burst_limit = asoc->max_burst *
			 * SCTP_SAT_NETWORK_BURST_INCR; }
			 */
			if (asoc->max_burst > 0) {
				if (SCTP_BASE_SYSCTL(sctp_use_cwnd_based_maxburst)) {
					if ((net->flight_size + (asoc->max_burst * net->mtu)) < net->cwnd) {
						/* JRS - Use the congestion control given in the congestion control module */
						asoc->cc_functions.sctp_cwnd_update_after_output(stcb, net, asoc->max_burst);
						if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LOG_MAXBURST_ENABLE) {
							sctp_log_maxburst(stcb, net, 0, asoc->max_burst, SCTP_MAX_BURST_APPLIED);
						}
						SCTP_STAT_INCR(sctps_maxburstqueued);
					}
					net->fast_retran_ip = 0;
				} else {
					if (net->flight_size == 0) {
						/* Should be decaying the cwnd here */
						;
					}
				}
			}
		}

	}
	burst_cnt = 0;
	do {
		error = sctp_med_chunk_output(inp, stcb, asoc, &num_out,
					      &reason_code, 0, from_where,
					      &now, &now_filled, frag_point, so_locked);
		if (error) {
			SCTPDBG(SCTP_DEBUG_OUTPUT1, "Error %d was returned from med-c-op\n", error);
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LOG_MAXBURST_ENABLE) {
				sctp_log_maxburst(stcb, asoc->primary_destination, error, burst_cnt, SCTP_MAX_BURST_ERROR_STOP);
			}
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_LOGGING_ENABLE) {
				sctp_log_cwnd(stcb, NULL, error, SCTP_SEND_NOW_COMPLETES);
				sctp_log_cwnd(stcb, NULL, 0xdeadbeef, SCTP_SEND_NOW_COMPLETES);
			}
			break;
		}
		SCTPDBG(SCTP_DEBUG_OUTPUT3, "m-c-o put out %d\n", num_out);

		tot_out += num_out;
		burst_cnt++;
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_LOGGING_ENABLE) {
			sctp_log_cwnd(stcb, NULL, num_out, SCTP_SEND_NOW_COMPLETES);
			if (num_out == 0) {
				sctp_log_cwnd(stcb, NULL, reason_code, SCTP_SEND_NOW_COMPLETES);
			}
		}
		if (nagle_on) {
			/*-
			 * When nagle is on, we look at how much is un_sent, then
			 * if its smaller than an MTU and we have data in
			 * flight we stop.
			 */
			un_sent = ((stcb->asoc.total_output_queue_size - stcb->asoc.total_flight) +
					   (stcb->asoc.stream_queue_cnt * sizeof(struct sctp_data_chunk)));
			if ((un_sent < (int)(stcb->asoc.smallest_mtu - SCTP_MIN_OVERHEAD)) &&
			    (stcb->asoc.total_flight > 0)) {
				break;
			}
		}
		if (TAILQ_EMPTY(&asoc->control_send_queue) &&
		    TAILQ_EMPTY(&asoc->send_queue) &&
		    stcb->asoc.ss_functions.sctp_ss_is_empty(stcb, asoc)) {
			/* Nothing left to send */
			break;
		}
		if ((stcb->asoc.total_output_queue_size - stcb->asoc.total_flight) <= 0) {
			/* Nothing left to send */
			break;
		}
	} while (num_out && 
	         ((asoc->max_burst == 0) ||
		  SCTP_BASE_SYSCTL(sctp_use_cwnd_based_maxburst) ||
		  (burst_cnt < asoc->max_burst)));

	if (SCTP_BASE_SYSCTL(sctp_use_cwnd_based_maxburst) == 0) {
		if ((asoc->max_burst > 0) && (burst_cnt >= asoc->max_burst)) {
			SCTP_STAT_INCR(sctps_maxburstqueued);
			asoc->burst_limit_applied = 1;
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LOG_MAXBURST_ENABLE) {
				sctp_log_maxburst(stcb, asoc->primary_destination, 0, burst_cnt, SCTP_MAX_BURST_APPLIED);
			}
		} else {
			asoc->burst_limit_applied = 0;
		}
	}
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_CWND_LOGGING_ENABLE) {
		sctp_log_cwnd(stcb, NULL, tot_out, SCTP_SEND_NOW_COMPLETES);
	}
	SCTPDBG(SCTP_DEBUG_OUTPUT1, "Ok, we have put out %d chunks\n",
		tot_out);

	/*-
	 * Now we need to clean up the control chunk chain if a ECNE is on
	 * it. It must be marked as UNSENT again so next call will continue
	 * to send it until such time that we get a CWR, to remove it.
	 */
	if (stcb->asoc.ecn_echo_cnt_onq)
		sctp_fix_ecn_echo(asoc);
	return;
}


int
sctp_output(inp, m, addr, control, p, flags)
	struct sctp_inpcb *inp;
#if defined(__Panda__)
	pakhandle_type m;
#else
	struct mbuf *m;
#endif
	struct sockaddr *addr;
#if defined(__Panda__)
	pakhandle_type control;
#else
	struct mbuf *control;
#endif
#if defined(__FreeBSD__) && __FreeBSD_version >= 500000
	struct thread *p;
#elif defined(__Windows__)
	PKTHREAD p;
#else
	struct proc *p;
#endif
	int flags;
{
	if (inp == NULL) {
		SCTP_LTRACE_ERR_RET_PKT(m, inp, NULL, NULL, SCTP_FROM_SCTP_OUTPUT, EINVAL);
		return (EINVAL);
	}

	if (inp->sctp_socket == NULL) {
		SCTP_LTRACE_ERR_RET_PKT(m, inp, NULL, NULL, SCTP_FROM_SCTP_OUTPUT, EINVAL);
		return (EINVAL);
	}
	return (sctp_sosend(inp->sctp_socket,
			    addr,
			    (struct uio *)NULL,
			    m,
			    control,
#if defined(__APPLE__) || defined(__Panda__)
			    flags
#else
			    flags, p
#endif
			));
}

void
send_forward_tsn(struct sctp_tcb *stcb,
		 struct sctp_association *asoc)
{
        struct sctp_tmit_chunk *chk;
	struct sctp_forward_tsn_chunk *fwdtsn;
	uint32_t advance_peer_ack_point;

        SCTP_TCB_LOCK_ASSERT(stcb);
	TAILQ_FOREACH(chk, &asoc->control_send_queue, sctp_next) {
		if (chk->rec.chunk_id.id == SCTP_FORWARD_CUM_TSN) {
			/* mark it to unsent */
			chk->sent = SCTP_DATAGRAM_UNSENT;
			chk->snd_count = 0;
			/* Do we correct its output location? */
			if (chk->whoTo) {
				sctp_free_remote_addr(chk->whoTo);
				chk->whoTo = NULL;
			}
			goto sctp_fill_in_rest;
		}
	}
	/* Ok if we reach here we must build one */
	sctp_alloc_a_chunk(stcb, chk);
	if (chk == NULL) {
		return;
	}
	asoc->fwd_tsn_cnt++;
	chk->copy_by_ref = 0;
	chk->rec.chunk_id.id = SCTP_FORWARD_CUM_TSN;
	chk->rec.chunk_id.can_take_data = 0;
	chk->asoc = asoc;
	chk->whoTo = NULL;
	chk->data = sctp_get_mbuf_for_msg(MCLBYTES, 0, M_DONTWAIT, 1, MT_DATA);
	if (chk->data == NULL) {
		sctp_free_a_chunk(stcb, chk, SCTP_SO_NOT_LOCKED);
		return;
	}
	SCTP_BUF_RESV_UF(chk->data, SCTP_MIN_OVERHEAD);
	chk->sent = SCTP_DATAGRAM_UNSENT;
	chk->snd_count = 0;
	TAILQ_INSERT_TAIL(&asoc->control_send_queue, chk, sctp_next);
	asoc->ctrl_queue_cnt++;
sctp_fill_in_rest:
	/*-
	 * Here we go through and fill out the part that deals with
	 * stream/seq of the ones we skip.
	 */
	SCTP_BUF_LEN(chk->data) = 0;
	{
		struct sctp_tmit_chunk *at, *tp1, *last;
		struct sctp_strseq *strseq;
		unsigned int cnt_of_space, i, ovh;
		unsigned int space_needed;
		unsigned int cnt_of_skipped = 0;

		TAILQ_FOREACH(at, &asoc->sent_queue, sctp_next) {
			if (at->sent != SCTP_FORWARD_TSN_SKIP) {
				/* no more to look at */
				break;
			}
			if (at->rec.data.rcv_flags & SCTP_DATA_UNORDERED) {
				/* We don't report these */
				continue;
			}
			cnt_of_skipped++;
		}
		space_needed = (sizeof(struct sctp_forward_tsn_chunk) +
		    (cnt_of_skipped * sizeof(struct sctp_strseq)));

		cnt_of_space = M_TRAILINGSPACE(chk->data);

		if (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) {
			ovh = SCTP_MIN_OVERHEAD;
		} else {
			ovh = SCTP_MIN_V4_OVERHEAD;
		}
		if (cnt_of_space > (asoc->smallest_mtu - ovh)) {
			/* trim to a mtu size */
			cnt_of_space = asoc->smallest_mtu - ovh;
		}
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LOG_TRY_ADVANCE) {
			sctp_misc_ints(SCTP_FWD_TSN_CHECK,
				       0xff, 0, cnt_of_skipped,
				       asoc->advanced_peer_ack_point);
			
		}
		advance_peer_ack_point = asoc->advanced_peer_ack_point;
		if (cnt_of_space < space_needed) {
			/*-
			 * ok we must trim down the chunk by lowering the
			 * advance peer ack point.
			 */
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LOG_TRY_ADVANCE) {
				sctp_misc_ints(SCTP_FWD_TSN_CHECK,
					       0xff, 0xff, cnt_of_space,
					       space_needed);
			}
			cnt_of_skipped = cnt_of_space - sizeof(struct sctp_forward_tsn_chunk);
			cnt_of_skipped /= sizeof(struct sctp_strseq);
			/*-
			 * Go through and find the TSN that will be the one
			 * we report.
			 */
			at = TAILQ_FIRST(&asoc->sent_queue);
			for (i = 0; i < cnt_of_skipped; i++) {
				tp1 = TAILQ_NEXT(at, sctp_next);
				if (tp1 == NULL) {
					break;
				}
				at = tp1;
			}
			if (at && SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LOG_TRY_ADVANCE) {
				sctp_misc_ints(SCTP_FWD_TSN_CHECK,
					       0xff, cnt_of_skipped, at->rec.data.TSN_seq,
					       asoc->advanced_peer_ack_point);
			}
			last = at;
			/*-
			 * last now points to last one I can report, update
			 * peer ack point
			 */
			if (last)
				advance_peer_ack_point = last->rec.data.TSN_seq;
			space_needed = sizeof(struct sctp_forward_tsn_chunk) +
			               cnt_of_skipped * sizeof(struct sctp_strseq);
		}
		chk->send_size = space_needed;
		/* Setup the chunk */
		fwdtsn = mtod(chk->data, struct sctp_forward_tsn_chunk *);
		fwdtsn->ch.chunk_length = htons(chk->send_size);
		fwdtsn->ch.chunk_flags = 0;
		fwdtsn->ch.chunk_type = SCTP_FORWARD_CUM_TSN;
		fwdtsn->new_cumulative_tsn = htonl(advance_peer_ack_point);
		SCTP_BUF_LEN(chk->data) = chk->send_size;
		fwdtsn++;
		/*-
		 * Move pointer to after the fwdtsn and transfer to the
		 * strseq pointer.
		 */
		strseq = (struct sctp_strseq *)fwdtsn;
		/*-
		 * Now populate the strseq list. This is done blindly
		 * without pulling out duplicate stream info. This is
		 * inefficent but won't harm the process since the peer will
		 * look at these in sequence and will thus release anything.
		 * It could mean we exceed the PMTU and chop off some that
		 * we could have included.. but this is unlikely (aka 1432/4
		 * would mean 300+ stream seq's would have to be reported in
		 * one FWD-TSN. With a bit of work we can later FIX this to
		 * optimize and pull out duplcates.. but it does add more
		 * overhead. So for now... not!
		 */
		at = TAILQ_FIRST(&asoc->sent_queue);
		for (i = 0; i < cnt_of_skipped; i++) {
			tp1 = TAILQ_NEXT(at, sctp_next);
			if (tp1 == NULL)
				break;
			if (at->rec.data.rcv_flags & SCTP_DATA_UNORDERED) {
				/* We don't report these */
				i--;
				at = tp1;
				continue;
			}
			if (at->rec.data.TSN_seq == advance_peer_ack_point) {
				at->rec.data.fwd_tsn_cnt = 0;
			}
			strseq->stream = ntohs(at->rec.data.stream_number);
			strseq->sequence = ntohs(at->rec.data.stream_seq);
			strseq++;
			at = tp1;
		}
	}
	return;

}

void
sctp_send_sack(struct sctp_tcb *stcb, int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
	SCTP_UNUSED
#endif
)
{
	/*-
	 * Queue up a SACK or NR-SACK in the control queue.
	 * We must first check to see if a SACK or NR-SACK is
	 * somehow on the control queue.
	 * If so, we will take and and remove the old one.
	 */
	struct sctp_association *asoc;
	struct sctp_tmit_chunk *chk, *a_chk;
	struct sctp_sack_chunk *sack;
	struct sctp_nr_sack_chunk *nr_sack;
	struct sctp_gap_ack_block *gap_descriptor;
	struct sack_track *selector;
	int mergeable = 0;
	int offset;
	caddr_t limit;
	uint32_t *dup;
	int limit_reached = 0;
	unsigned int i, siz, j;
	unsigned int num_gap_blocks = 0, num_nr_gap_blocks = 0, space;
	int num_dups = 0;
	int space_req;
	uint32_t highest_tsn;
	uint8_t flags;
	uint8_t type;
	uint8_t tsn_map;

	if ((stcb->asoc.sctp_nr_sack_on_off == 1) &&
	    (stcb->asoc.peer_supports_nr_sack == 1)) {
		type = SCTP_NR_SELECTIVE_ACK;
	} else {
		type = SCTP_SELECTIVE_ACK;
	}
	a_chk = NULL;
	asoc = &stcb->asoc;
	SCTP_TCB_LOCK_ASSERT(stcb);
	if (asoc->last_data_chunk_from == NULL) {
		/* Hmm we never received anything */
		return;
	}
	sctp_slide_mapping_arrays(stcb);
	sctp_set_rwnd(stcb, asoc);
	TAILQ_FOREACH(chk, &asoc->control_send_queue, sctp_next) {
		if (chk->rec.chunk_id.id == type) {
			/* Hmm, found a sack already on queue, remove it */
			TAILQ_REMOVE(&asoc->control_send_queue, chk, sctp_next);
			asoc->ctrl_queue_cnt--;
			a_chk = chk;
			if (a_chk->data) {
				sctp_m_freem(a_chk->data);
				a_chk->data = NULL;
			}
			if (a_chk->whoTo) {
				sctp_free_remote_addr(a_chk->whoTo);
				a_chk->whoTo = NULL;
			}
			break;
		}
	}
	if (a_chk == NULL) {
		sctp_alloc_a_chunk(stcb, a_chk);
		if (a_chk == NULL) {
			/* No memory so we drop the idea, and set a timer */
			if (stcb->asoc.delayed_ack) {
				sctp_timer_stop(SCTP_TIMER_TYPE_RECV,
				    stcb->sctp_ep, stcb, NULL, SCTP_FROM_SCTP_OUTPUT + SCTP_LOC_5);
				sctp_timer_start(SCTP_TIMER_TYPE_RECV,
				    stcb->sctp_ep, stcb, NULL);
			} else {
				stcb->asoc.send_sack = 1;
			}
			return;
		}
		a_chk->copy_by_ref = 0;
		a_chk->rec.chunk_id.id = type;
		a_chk->rec.chunk_id.can_take_data = 1;
	}
	/* Clear our pkt counts */
	asoc->data_pkts_seen = 0;

	a_chk->asoc = asoc;
	a_chk->snd_count = 0;
	a_chk->send_size = 0;	/* fill in later */
	a_chk->sent = SCTP_DATAGRAM_UNSENT;
	a_chk->whoTo = NULL;

	if ((asoc->numduptsns) ||
	    (!(asoc->last_data_chunk_from->dest_state & SCTP_ADDR_REACHABLE))) {
		/*-
		 * Ok, we have some duplicates or the destination for the
		 * sack is unreachable, lets see if we can select an
		 * alternate than asoc->last_data_chunk_from
		 */
		if ((asoc->last_data_chunk_from->dest_state & SCTP_ADDR_REACHABLE) &&
		    (asoc->used_alt_onsack > asoc->numnets)) {
			/* We used an alt last time, don't this time */
			a_chk->whoTo = NULL;
		} else {
			asoc->used_alt_onsack++;
			a_chk->whoTo = sctp_find_alternate_net(stcb, asoc->last_data_chunk_from, 0);
		}
		if (a_chk->whoTo == NULL) {
			/* Nope, no alternate */
			a_chk->whoTo = asoc->last_data_chunk_from;
			asoc->used_alt_onsack = 0;
		}
	} else {
		/*
		 * No duplicates so we use the last place we received data
		 * from.
		 */
		asoc->used_alt_onsack = 0;
		a_chk->whoTo = asoc->last_data_chunk_from;
	}
	if (a_chk->whoTo) {
		atomic_add_int(&a_chk->whoTo->ref_count, 1);
	}
	if (SCTP_TSN_GT(asoc->highest_tsn_inside_map, asoc->highest_tsn_inside_nr_map)) {
		highest_tsn = asoc->highest_tsn_inside_map;
	} else {
		highest_tsn = asoc->highest_tsn_inside_nr_map;
	}
	if (highest_tsn == asoc->cumulative_tsn) {
		/* no gaps */
		if (type == SCTP_SELECTIVE_ACK) {
			space_req = sizeof(struct sctp_sack_chunk);
		} else {
			space_req = sizeof(struct sctp_nr_sack_chunk);
		}
	} else {
		/* gaps get a cluster */
		space_req = MCLBYTES;
	}
	/* Ok now lets formulate a MBUF with our sack */
	a_chk->data = sctp_get_mbuf_for_msg(space_req, 0, M_DONTWAIT, 1, MT_DATA);
	if ((a_chk->data == NULL) ||
	    (a_chk->whoTo == NULL)) {
		/* rats, no mbuf memory */
		if (a_chk->data) {
			/* was a problem with the destination */
			sctp_m_freem(a_chk->data);
			a_chk->data = NULL;
		}
		sctp_free_a_chunk(stcb, a_chk, so_locked);
		/* sa_ignore NO_NULL_CHK */
		if (stcb->asoc.delayed_ack) {
			sctp_timer_stop(SCTP_TIMER_TYPE_RECV,
			    stcb->sctp_ep, stcb, NULL, SCTP_FROM_SCTP_OUTPUT + SCTP_LOC_6);
			sctp_timer_start(SCTP_TIMER_TYPE_RECV,
			    stcb->sctp_ep, stcb, NULL);
		} else {
			stcb->asoc.send_sack = 1;
		}
		return;
	}
	/* ok, lets go through and fill it in */
	SCTP_BUF_RESV_UF(a_chk->data, SCTP_MIN_OVERHEAD);
	space = M_TRAILINGSPACE(a_chk->data);
	if (space > (a_chk->whoTo->mtu - SCTP_MIN_OVERHEAD)) {
		space = (a_chk->whoTo->mtu - SCTP_MIN_OVERHEAD);
	}
	limit = mtod(a_chk->data, caddr_t);
	limit += space;

	flags = 0;

	if ((asoc->sctp_cmt_on_off > 0) &&
	    SCTP_BASE_SYSCTL(sctp_cmt_use_dac)) {
		/*-
		 * CMT DAC algorithm: If 2 (i.e., 0x10) packets have been
		 * received, then set high bit to 1, else 0. Reset
		 * pkts_rcvd.
		 */
		flags |= (asoc->cmt_dac_pkts_rcvd << 6);
		asoc->cmt_dac_pkts_rcvd = 0;
	}
#ifdef SCTP_ASOCLOG_OF_TSNS
	stcb->asoc.cumack_logsnt[stcb->asoc.cumack_log_atsnt] = asoc->cumulative_tsn;
	stcb->asoc.cumack_log_atsnt++;
	if (stcb->asoc.cumack_log_atsnt >= SCTP_TSN_LOG_SIZE) {
		stcb->asoc.cumack_log_atsnt = 0;
	}
#endif
	/* reset the readers interpretation */
	stcb->freed_by_sorcv_sincelast = 0;

	if (type == SCTP_SELECTIVE_ACK) {
		sack = mtod(a_chk->data, struct sctp_sack_chunk *);
		nr_sack = NULL;
		gap_descriptor = (struct sctp_gap_ack_block *)((caddr_t)sack + sizeof(struct sctp_sack_chunk));
		if (highest_tsn > asoc->mapping_array_base_tsn) {
			siz = (((highest_tsn - asoc->mapping_array_base_tsn) + 1) + 7) / 8;
		} else {
			siz = (((MAX_TSN - highest_tsn) + 1) + highest_tsn + 7) / 8;
		}
	} else {
		sack = NULL;
		nr_sack = mtod(a_chk->data, struct sctp_nr_sack_chunk *);
		gap_descriptor = (struct sctp_gap_ack_block *)((caddr_t)nr_sack + sizeof(struct sctp_nr_sack_chunk));
		if (asoc->highest_tsn_inside_map > asoc->mapping_array_base_tsn) {
			siz = (((asoc->highest_tsn_inside_map - asoc->mapping_array_base_tsn) + 1) + 7) / 8;
		} else {
			siz = (((MAX_TSN - asoc->mapping_array_base_tsn) + 1) + asoc->highest_tsn_inside_map + 7) / 8;
		}
	}

	if (SCTP_TSN_GT(asoc->mapping_array_base_tsn, asoc->cumulative_tsn)) {
		offset = 1;
	} else {
		offset = asoc->mapping_array_base_tsn - asoc->cumulative_tsn;
	}
	if (((type == SCTP_SELECTIVE_ACK) &&
	     SCTP_TSN_GT(highest_tsn, asoc->cumulative_tsn)) ||
	    ((type == SCTP_NR_SELECTIVE_ACK) &&
	     SCTP_TSN_GT(asoc->highest_tsn_inside_map, asoc->cumulative_tsn))) {
		/* we have a gap .. maybe */
		for (i = 0; i < siz; i++) {
			tsn_map = asoc->mapping_array[i];
			if (type == SCTP_SELECTIVE_ACK) {
				tsn_map |= asoc->nr_mapping_array[i];
			}
			if (i == 0) {
				/*
				 * Clear all bits corresponding to TSNs 
				 * smaller or equal to the cumulative TSN.
				 */
				tsn_map &= (~0 << (1 - offset));
			}
			selector = &sack_array[tsn_map];
			if (mergeable && selector->right_edge) {
				/*
				 * Backup, left and right edges were ok to
				 * merge.
				 */
				num_gap_blocks--;
				gap_descriptor--;
			}
			if (selector->num_entries == 0)
				mergeable = 0;
			else {
				for (j = 0; j < selector->num_entries; j++) {
					if (mergeable && selector->right_edge) {
						/*
						 * do a merge by NOT setting
						 * the left side
						 */
						mergeable = 0;
					} else {
						/*
						 * no merge, set the left
						 * side
						 */
						mergeable = 0;
						gap_descriptor->start = htons((selector->gaps[j].start + offset));
					}
					gap_descriptor->end = htons((selector->gaps[j].end + offset));
					num_gap_blocks++;
					gap_descriptor++;
					if (((caddr_t)gap_descriptor + sizeof(struct sctp_gap_ack_block)) > limit) {
						/* no more room */
						limit_reached = 1;
						break;
					}
				}
				if (selector->left_edge) {
					mergeable = 1;
				}
			}
			if (limit_reached) {
				/* Reached the limit stop */
				break;
			}
			offset += 8;
		}
	}
	if ((type == SCTP_NR_SELECTIVE_ACK) &&
	    (limit_reached == 0)) {

		mergeable = 0;

		if (asoc->highest_tsn_inside_nr_map > asoc->mapping_array_base_tsn) {
			siz = (((asoc->highest_tsn_inside_nr_map - asoc->mapping_array_base_tsn) + 1) + 7) / 8;
		} else {
			siz = (((MAX_TSN - asoc->mapping_array_base_tsn) + 1) + asoc->highest_tsn_inside_nr_map + 7) / 8;
		}

		if (SCTP_TSN_GT(asoc->mapping_array_base_tsn, asoc->cumulative_tsn)) {
			offset = 1;
		} else {
			offset = asoc->mapping_array_base_tsn - asoc->cumulative_tsn;
		}
		if (SCTP_TSN_GT(asoc->highest_tsn_inside_nr_map, asoc->cumulative_tsn)) {
			/* we have a gap .. maybe */
			for (i = 0; i < siz; i++) {
				tsn_map = asoc->nr_mapping_array[i];
				if (i == 0) {
					/*
					 * Clear all bits corresponding to TSNs 
					 * smaller or equal to the cumulative TSN.
					 */
					tsn_map &= (~0 << (1 - offset));
				}
				selector = &sack_array[tsn_map];
				if (mergeable && selector->right_edge) {
					/*
					* Backup, left and right edges were ok to
					* merge.
					*/
					num_nr_gap_blocks--;
					gap_descriptor--;
				}
				if (selector->num_entries == 0)
					mergeable = 0;
				else {
					for (j = 0; j < selector->num_entries; j++) {
						if (mergeable && selector->right_edge) {
							/*
							* do a merge by NOT setting
							* the left side
							*/
							mergeable = 0;
						} else {
							/*
							* no merge, set the left
							* side
							*/
							mergeable = 0;
							gap_descriptor->start = htons((selector->gaps[j].start + offset));
						}
						gap_descriptor->end = htons((selector->gaps[j].end + offset));
						num_nr_gap_blocks++;
						gap_descriptor++;
						if (((caddr_t)gap_descriptor + sizeof(struct sctp_gap_ack_block)) > limit) {
							/* no more room */
							limit_reached = 1;
							break;
						}
					}
					if (selector->left_edge) {
						mergeable = 1;
					}
				}
				if (limit_reached) {
					/* Reached the limit stop */
					break;
				}
				offset += 8;
			}
		}
	}
	/* now we must add any dups we are going to report. */
	if ((limit_reached == 0) && (asoc->numduptsns)) {
		dup = (uint32_t *) gap_descriptor;
		for (i = 0; i < asoc->numduptsns; i++) {
			*dup = htonl(asoc->dup_tsns[i]);
			dup++;
			num_dups++;
			if (((caddr_t)dup + sizeof(uint32_t)) > limit) {
				/* no more room */
				break;
			}
		}
		asoc->numduptsns = 0;
	}
	/*
	 * now that the chunk is prepared queue it to the control chunk
	 * queue.
	 */
	if (type == SCTP_SELECTIVE_ACK) {
		a_chk->send_size = sizeof(struct sctp_sack_chunk) +
		                   (num_gap_blocks + num_nr_gap_blocks) * sizeof(struct sctp_gap_ack_block) +
		                   num_dups * sizeof(int32_t);
		SCTP_BUF_LEN(a_chk->data) = a_chk->send_size;
		sack->sack.cum_tsn_ack = htonl(asoc->cumulative_tsn);
		sack->sack.a_rwnd = htonl(asoc->my_rwnd);
		sack->sack.num_gap_ack_blks = htons(num_gap_blocks);
		sack->sack.num_dup_tsns = htons(num_dups);
		sack->ch.chunk_type = type;
		sack->ch.chunk_flags = flags;
		sack->ch.chunk_length = htons(a_chk->send_size);
	} else {
		a_chk->send_size = sizeof(struct sctp_nr_sack_chunk) +
		                   (num_gap_blocks + num_nr_gap_blocks) * sizeof(struct sctp_gap_ack_block) +
		                   num_dups * sizeof(int32_t);
		SCTP_BUF_LEN(a_chk->data) = a_chk->send_size;
		nr_sack->nr_sack.cum_tsn_ack = htonl(asoc->cumulative_tsn);
		nr_sack->nr_sack.a_rwnd = htonl(asoc->my_rwnd);
		nr_sack->nr_sack.num_gap_ack_blks = htons(num_gap_blocks);
		nr_sack->nr_sack.num_nr_gap_ack_blks = htons(num_nr_gap_blocks);
		nr_sack->nr_sack.num_dup_tsns = htons(num_dups);
		nr_sack->nr_sack.reserved = 0;
		nr_sack->ch.chunk_type = type;
		nr_sack->ch.chunk_flags = flags;
		nr_sack->ch.chunk_length = htons(a_chk->send_size);
	}
	TAILQ_INSERT_TAIL(&asoc->control_send_queue, a_chk, sctp_next);
	asoc->my_last_reported_rwnd = asoc->my_rwnd;
	asoc->ctrl_queue_cnt++;
	asoc->send_sack = 0;
	SCTP_STAT_INCR(sctps_sendsacks);
	return;
}

void
sctp_send_abort_tcb(struct sctp_tcb *stcb, struct mbuf *operr, int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
    SCTP_UNUSED
#endif
    )
{
	struct mbuf *m_abort;
	struct mbuf *m_out = NULL, *m_end = NULL;
	struct sctp_abort_chunk *abort = NULL;
	int sz;
	uint32_t auth_offset = 0;
	struct sctp_auth_chunk *auth = NULL;
	struct sctp_nets *net;

#if defined(__APPLE__)
	if (so_locked) {
		sctp_lock_assert(SCTP_INP_SO(stcb->sctp_ep));
	} else {
		sctp_unlock_assert(SCTP_INP_SO(stcb->sctp_ep));
	}
#endif
	/*-
	 * Add an AUTH chunk, if chunk requires it and save the offset into
	 * the chain for AUTH
	 */
	if (sctp_auth_is_required_chunk(SCTP_ABORT_ASSOCIATION,
				       stcb->asoc.peer_auth_chunks)) {
		m_out = sctp_add_auth_chunk(m_out, &m_end, &auth, &auth_offset,
					    stcb, SCTP_ABORT_ASSOCIATION);
		SCTP_STAT_INCR_COUNTER64(sctps_outcontrolchunks);
	}
	SCTP_TCB_LOCK_ASSERT(stcb);
	m_abort = sctp_get_mbuf_for_msg(sizeof(struct sctp_abort_chunk), 0, M_DONTWAIT, 1, MT_HEADER);
	if (m_abort == NULL) {
		/* no mbuf's */
		if (m_out)
			sctp_m_freem(m_out);
		return;
	}
	/* link in any error */
	SCTP_BUF_NEXT(m_abort) = operr;
	sz = 0;
	if (operr) {
		struct mbuf *n;

		n = operr;
		while (n) {
			sz += SCTP_BUF_LEN(n);
			n = SCTP_BUF_NEXT(n);
		}
	}
	SCTP_BUF_LEN(m_abort) = sizeof(*abort);
	if (m_out == NULL) {
		/* NO Auth chunk prepended, so reserve space in front */
		SCTP_BUF_RESV_UF(m_abort, SCTP_MIN_OVERHEAD);
		m_out = m_abort;
	} else {
		/* Put AUTH chunk at the front of the chain */
		SCTP_BUF_NEXT(m_end) = m_abort;
	}
	if (stcb->asoc.alternate) {
		net = stcb->asoc.alternate;
	} else {
		net = stcb->asoc.primary_destination;
	}
	/* fill in the ABORT chunk */
	abort = mtod(m_abort, struct sctp_abort_chunk *);
	abort->ch.chunk_type = SCTP_ABORT_ASSOCIATION;
	abort->ch.chunk_flags = 0;
	abort->ch.chunk_length = htons(sizeof(*abort) + sz);

	(void)sctp_lowlevel_chunk_output(stcb->sctp_ep, stcb, net,
	                                 (struct sockaddr *)&net->ro._l_addr,
	                                 m_out, auth_offset, auth, stcb->asoc.authinfo.active_keyid, 1, 0, NULL, 0,
	                                 stcb->sctp_ep->sctp_lport, stcb->rport, htonl(stcb->asoc.peer_vtag),
	                                 stcb->asoc.primary_destination->port, so_locked, NULL, NULL);
	SCTP_STAT_INCR_COUNTER64(sctps_outcontrolchunks);
}

void
sctp_send_shutdown_complete(struct sctp_tcb *stcb,
                            struct sctp_nets *net,
                            int reflect_vtag)
{
	/* formulate and SEND a SHUTDOWN-COMPLETE */
	struct mbuf *m_shutdown_comp;
	struct sctp_shutdown_complete_chunk *shutdown_complete;
	uint32_t vtag;
	uint8_t flags;

	m_shutdown_comp = sctp_get_mbuf_for_msg(sizeof(struct sctp_chunkhdr), 0, M_DONTWAIT, 1, MT_HEADER);
	if (m_shutdown_comp == NULL) {
		/* no mbuf's */
		return;
	}
	if (reflect_vtag) {
		flags = SCTP_HAD_NO_TCB;
		vtag = stcb->asoc.my_vtag;
	} else {
		flags = 0;
		vtag = stcb->asoc.peer_vtag;
	}
	shutdown_complete = mtod(m_shutdown_comp, struct sctp_shutdown_complete_chunk *);
	shutdown_complete->ch.chunk_type = SCTP_SHUTDOWN_COMPLETE;
	shutdown_complete->ch.chunk_flags = flags;
	shutdown_complete->ch.chunk_length = htons(sizeof(struct sctp_shutdown_complete_chunk));
	SCTP_BUF_LEN(m_shutdown_comp) = sizeof(struct sctp_shutdown_complete_chunk);
	(void)sctp_lowlevel_chunk_output(stcb->sctp_ep, stcb, net,
	                                 (struct sockaddr *)&net->ro._l_addr,
	                                 m_shutdown_comp, 0, NULL, 0, 1, 0, NULL, 0,
	                                 stcb->sctp_ep->sctp_lport, stcb->rport,
	                                 htonl(vtag),
	                                 net->port, SCTP_SO_NOT_LOCKED, NULL, NULL);
	SCTP_STAT_INCR_COUNTER64(sctps_outcontrolchunks);
	return;
}

void
sctp_send_shutdown_complete2(struct mbuf *m, int iphlen, struct sctphdr *sh,
			     uint32_t vrf_id, uint16_t port)
{
	/* formulate and SEND a SHUTDOWN-COMPLETE */
#ifdef __Panda__
	pakhandle_type o_pak;
#else
	struct mbuf *o_pak;
#endif
	struct mbuf *mout;
	struct ip *iph;
	struct udphdr *udp = NULL;
	int offset_out, len, mlen;
	struct sctp_shutdown_complete_msg *comp_cp;
#ifdef INET
	struct ip *iph_out;
#endif
#ifdef INET6
	struct ip6_hdr *ip6, *ip6_out;
#endif

	iph = mtod(m, struct ip *);
	switch (iph->ip_v) {
#ifdef INET
	case IPVERSION:
		len = (sizeof(struct ip) + sizeof(struct sctp_shutdown_complete_msg));
		break;
#endif
#ifdef INET6
	case IPV6_VERSION >> 4:
		len = (sizeof(struct ip6_hdr) + sizeof(struct sctp_shutdown_complete_msg));
		break;
#endif
	default:
		return;
	}
	if (port) {
		len += sizeof(struct udphdr);
	}
#if defined(APPLE_LION)
	mout = sctp_get_mbuf_for_msg(len + SCTP_MAX_LINKHDR, 1, M_DONTWAIT, 1, MT_DATA);
#else
	mout = sctp_get_mbuf_for_msg(len + max_linkhdr, 1, M_DONTWAIT, 1, MT_DATA);
#endif
	if (mout == NULL) {
		return;
	}
#if defined(APPLE_LION)
	SCTP_BUF_RESV_UF(mout, SCTP_MAX_LINKHDR);
#else
	SCTP_BUF_RESV_UF(mout, max_linkhdr);
#endif
	SCTP_BUF_LEN(mout) = len;
	SCTP_BUF_NEXT(mout) = NULL;
#if defined(__FreeBSD__)
	if (m->m_flags & M_FLOWID) {
		mout->m_pkthdr.flowid = m->m_pkthdr.flowid;
		mout->m_flags |= M_FLOWID;
	}
#endif
#ifdef INET
	iph_out = NULL;
#endif
#ifdef INET6
	ip6_out = NULL;
#endif
	offset_out = 0;

	switch (iph->ip_v) {
#ifdef INET
	case IPVERSION:
		iph_out = mtod(mout, struct ip *);

		/* Fill in the IP header for the ABORT */
		iph_out->ip_v = IPVERSION;
		iph_out->ip_hl = (sizeof(struct ip) / 4);
		iph_out->ip_tos = (u_char)0;
		iph_out->ip_id = 0;
		iph_out->ip_off = 0;
		iph_out->ip_ttl = MAXTTL;
		if (port) {
			iph_out->ip_p = IPPROTO_UDP;
		} else {
			iph_out->ip_p = IPPROTO_SCTP;
		}
		iph_out->ip_src.s_addr = iph->ip_dst.s_addr;
		iph_out->ip_dst.s_addr = iph->ip_src.s_addr;

		/* let IP layer calculate this */
		iph_out->ip_sum = 0;
		offset_out += sizeof(*iph_out);
		comp_cp = (struct sctp_shutdown_complete_msg *)(
		    (caddr_t)iph_out + offset_out);
		break;
#endif
#ifdef INET6
#if !defined(__Userspace__) /* TODO declare ip6_defhlim */
	case IPV6_VERSION >> 4:
		ip6 = (struct ip6_hdr *)iph;
		ip6_out = mtod(mout, struct ip6_hdr *);

		/* Fill in the IPv6 header for the ABORT */
		ip6_out->ip6_flow = ip6->ip6_flow;
		ip6_out->ip6_hlim = MODULE_GLOBAL(ip6_defhlim);
		if (port) {
			ip6_out->ip6_nxt = IPPROTO_UDP;
		} else {
			ip6_out->ip6_nxt = IPPROTO_SCTP;
		}
		ip6_out->ip6_src = ip6->ip6_dst;
		ip6_out->ip6_dst = ip6->ip6_src;
		/* ?? The old code had both the iph len + payload, I think
		 * this is wrong and would never have worked
		 */
 		ip6_out->ip6_plen = sizeof(struct sctp_shutdown_complete_msg);
		offset_out += sizeof(*ip6_out);
		comp_cp = (struct sctp_shutdown_complete_msg *)(
		    (caddr_t)ip6_out + offset_out);
#endif /* ! __Userspace__ */
		break;
#endif /* INET6 */
	default:
		/* Currently not supported. */
		sctp_m_freem(mout);
		return;
	}
	if (port) {
		udp = (struct udphdr *)comp_cp;
		udp->uh_sport = htons(SCTP_BASE_SYSCTL(sctp_udp_tunneling_port));
		udp->uh_dport = port;
		udp->uh_ulen = htons(sizeof(struct sctp_shutdown_complete_msg) + sizeof(struct udphdr));
#if !defined(__Windows__) && !defined(__Userspace__)
#ifdef INET
		if (iph_out) 
			udp->uh_sum = in_pseudo(iph_out->ip_src.s_addr, iph_out->ip_dst.s_addr, udp->uh_ulen + htons(IPPROTO_UDP));
#endif
#else
		udp->uh_sum = 0;
#endif
		offset_out += sizeof(struct udphdr);
		comp_cp = (struct sctp_shutdown_complete_msg *)((caddr_t)comp_cp + sizeof(struct udphdr));
	}
	if (SCTP_GET_HEADER_FOR_OUTPUT(o_pak)) {
		/* no mbuf's */
		sctp_m_freem(mout);
		return;
	}
	/* Now copy in and fill in the ABORT tags etc. */
	comp_cp->sh.src_port = sh->dest_port;
	comp_cp->sh.dest_port = sh->src_port;
	comp_cp->sh.checksum = 0;
	comp_cp->sh.v_tag = sh->v_tag;
	comp_cp->shut_cmp.ch.chunk_flags = SCTP_HAD_NO_TCB;
	comp_cp->shut_cmp.ch.chunk_type = SCTP_SHUTDOWN_COMPLETE;
	comp_cp->shut_cmp.ch.chunk_length = htons(sizeof(struct sctp_shutdown_complete_chunk));

#ifdef INET
	if (iph_out != NULL) {
		sctp_route_t ro;
		int ret;

		mlen = SCTP_BUF_LEN(mout);
		bzero(&ro, sizeof ro);
#if defined(__Panda__)
		ro._l_addr.sa.sa_family = AF_INET;
#endif
		/* set IPv4 length */
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Userspace__)
		iph_out->ip_len = mlen;
#else
		iph_out->ip_len = htons(mlen);
#endif
#ifdef  SCTP_PACKET_LOGGING
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LAST_PACKET_TRACING)
			sctp_packet_log(mout, mlen);
#endif
		if (port) {
#if defined(SCTP_WITH_NO_CSUM)
			SCTP_STAT_INCR(sctps_sendnocrc);
#else
			comp_cp->sh.checksum = sctp_calculate_cksum(mout, offset_out);
			SCTP_STAT_INCR(sctps_sendswcrc);
#endif
			SCTP_ENABLE_UDP_CSUM(mout);
		} else {
#if defined(SCTP_WITH_NO_CSUM)
			SCTP_STAT_INCR(sctps_sendnocrc);
#else
#if defined(__FreeBSD__) && __FreeBSD_version >= 800000
			mout->m_pkthdr.csum_flags = CSUM_SCTP;
			mout->m_pkthdr.csum_data = 0;
			SCTP_STAT_INCR(sctps_sendhwcrc);
#else
			comp_cp->sh.checksum = sctp_calculate_cksum(mout, offset_out);
			SCTP_STAT_INCR(sctps_sendswcrc);
#endif
#endif
		}
		SCTP_ATTACH_CHAIN(o_pak, mout, mlen);
		/* out it goes */
		SCTP_IP_OUTPUT(ret, o_pak, &ro, NULL, vrf_id);

		/* Free the route if we got one back */
		if (ro.ro_rt)
			RTFREE(ro.ro_rt);
	}
#endif
#ifdef INET6
	if (ip6_out != NULL) {
#if defined(__Userspace__)
		sctp_route_t ro;
#else
		struct route_in6 ro;
#endif
		int ret;
#if !defined(__Panda__)
		struct ifnet *ifp = NULL;
#endif

		bzero(&ro, sizeof(ro));
#if defined(__Panda__)
		ro._l_addr.sa.sa_family = AF_INET6;
#endif
		mlen = SCTP_BUF_LEN(mout);
#ifdef  SCTP_PACKET_LOGGING
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LAST_PACKET_TRACING)
			sctp_packet_log(mout, mlen);
#endif
		SCTP_ATTACH_CHAIN(o_pak, mout, mlen);
		if (port) {
#if defined(SCTP_WITH_NO_CSUM)
			SCTP_STAT_INCR(sctps_sendnocrc);
#else
			comp_cp->sh.checksum = sctp_calculate_cksum(mout, sizeof(struct ip6_hdr) + sizeof(struct udphdr));
			SCTP_STAT_INCR(sctps_sendswcrc);
#endif
#if defined(__Windows__)
			udp->uh_sum = 0;
#elif !defined(__Userspace__)
			if ((udp->uh_sum = in6_cksum(o_pak, IPPROTO_UDP, sizeof(struct ip6_hdr), mlen - sizeof(struct ip6_hdr))) == 0) {
				udp->uh_sum = 0xffff;
			}
#endif
		} else {
#if defined(SCTP_WITH_NO_CSUM)
			SCTP_STAT_INCR(sctps_sendnocrc);
#else
#if defined(__FreeBSD__) && __FreeBSD_version >= 800000
			mout->m_pkthdr.csum_flags = CSUM_SCTP;
			mout->m_pkthdr.csum_data = 0;
			SCTP_STAT_INCR(sctps_sendhwcrc);
#else
			comp_cp->sh.checksum = sctp_calculate_cksum(mout, sizeof(struct ip6_hdr));
			SCTP_STAT_INCR(sctps_sendswcrc);
#endif
#endif
		}
		SCTP_IP6_OUTPUT(ret, o_pak, &ro, &ifp, NULL, vrf_id);

		/* Free the route if we got one back */
		if (ro.ro_rt)
			RTFREE(ro.ro_rt);
	}
#endif
	SCTP_STAT_INCR(sctps_sendpackets);
	SCTP_STAT_INCR_COUNTER64(sctps_outpackets);
	SCTP_STAT_INCR_COUNTER64(sctps_outcontrolchunks);
	return;

}

void
sctp_send_hb(struct sctp_tcb *stcb, struct sctp_nets *net,int so_locked
#if !defined(__APPLE__) && !defined(SCTP_SO_LOCK_TESTING)
	SCTP_UNUSED
#endif
)
{
	struct sctp_tmit_chunk *chk;
	struct sctp_heartbeat_chunk *hb;
	struct timeval now;

	SCTP_TCB_LOCK_ASSERT(stcb);
	if (net == NULL) {
		return;
	}
	(void)SCTP_GETTIME_TIMEVAL(&now);
	switch (net->ro._l_addr.sa.sa_family) {
#ifdef INET
	case AF_INET:
		break;
#endif
#ifdef INET6
	case AF_INET6:
		break;
#endif
	default:
		return;
	}
	sctp_alloc_a_chunk(stcb, chk);
	if (chk == NULL) {
		SCTPDBG(SCTP_DEBUG_OUTPUT4, "Gak, can't get a chunk for hb\n");
		return;
	}

	chk->copy_by_ref = 0;
	chk->rec.chunk_id.id = SCTP_HEARTBEAT_REQUEST;
	chk->rec.chunk_id.can_take_data = 1;
	chk->asoc = &stcb->asoc;
	chk->send_size = sizeof(struct sctp_heartbeat_chunk);

	chk->data = sctp_get_mbuf_for_msg(chk->send_size, 0, M_DONTWAIT, 1, MT_HEADER);
	if (chk->data == NULL) {
		sctp_free_a_chunk(stcb, chk, so_locked);
		return;
	}
	SCTP_BUF_RESV_UF(chk->data, SCTP_MIN_OVERHEAD);
	SCTP_BUF_LEN(chk->data) = chk->send_size;
	chk->sent = SCTP_DATAGRAM_UNSENT;
	chk->snd_count = 0;
	chk->whoTo = net;
	atomic_add_int(&chk->whoTo->ref_count, 1);
	/* Now we have a mbuf that we can fill in with the details */
	hb = mtod(chk->data, struct sctp_heartbeat_chunk *);
	memset(hb, 0, sizeof(struct sctp_heartbeat_chunk));
	/* fill out chunk header */
	hb->ch.chunk_type = SCTP_HEARTBEAT_REQUEST;
	hb->ch.chunk_flags = 0;
	hb->ch.chunk_length = htons(chk->send_size);
	/* Fill out hb parameter */
	hb->heartbeat.hb_info.ph.param_type = htons(SCTP_HEARTBEAT_INFO);
	hb->heartbeat.hb_info.ph.param_length = htons(sizeof(struct sctp_heartbeat_info_param));
	hb->heartbeat.hb_info.time_value_1 = now.tv_sec;
	hb->heartbeat.hb_info.time_value_2 = now.tv_usec;
	/* Did our user request this one, put it in */
	hb->heartbeat.hb_info.addr_family = net->ro._l_addr.sa.sa_family;
#if !(defined(__Windows__) ||  defined(__Userspace_os_Linux)) /*__Userspace__ */
	hb->heartbeat.hb_info.addr_len = net->ro._l_addr.sa.sa_len;
#else
	hb->heartbeat.hb_info.addr_len = sizeof(struct sockaddr_in);
#endif
	if (net->dest_state & SCTP_ADDR_UNCONFIRMED) {
		/*
		 * we only take from the entropy pool if the address is not
		 * confirmed.
		 */
		net->heartbeat_random1 = hb->heartbeat.hb_info.random_value1 = sctp_select_initial_TSN(&stcb->sctp_ep->sctp_ep);
		net->heartbeat_random2 = hb->heartbeat.hb_info.random_value2 = sctp_select_initial_TSN(&stcb->sctp_ep->sctp_ep);
	} else {
		net->heartbeat_random1 = hb->heartbeat.hb_info.random_value1 = 0;
		net->heartbeat_random2 = hb->heartbeat.hb_info.random_value2 = 0;
	}
	switch (net->ro._l_addr.sa.sa_family) {
#ifdef INET
	case AF_INET:
		memcpy(hb->heartbeat.hb_info.address,
		       &net->ro._l_addr.sin.sin_addr,
		       sizeof(net->ro._l_addr.sin.sin_addr));
		break;
#endif
#ifdef INET6
	case AF_INET6:
		memcpy(hb->heartbeat.hb_info.address,
		       &net->ro._l_addr.sin6.sin6_addr,
		       sizeof(net->ro._l_addr.sin6.sin6_addr));
		break;
#endif
	default:
		return;
		break;
	}
	net->hb_responded = 0;
	TAILQ_INSERT_TAIL(&stcb->asoc.control_send_queue, chk, sctp_next);
	stcb->asoc.ctrl_queue_cnt++;
	SCTP_STAT_INCR(sctps_sendheartbeat);
	return;
}

void
sctp_send_ecn_echo(struct sctp_tcb *stcb, struct sctp_nets *net,
		   uint32_t high_tsn)
{
	struct sctp_association *asoc;
	struct sctp_ecne_chunk *ecne;
	struct sctp_tmit_chunk *chk;

	if (net == NULL) {
		return;
	}
	asoc = &stcb->asoc;
	SCTP_TCB_LOCK_ASSERT(stcb);
	TAILQ_FOREACH(chk, &asoc->control_send_queue, sctp_next) {
		if ((chk->rec.chunk_id.id == SCTP_ECN_ECHO) && (net == chk->whoTo)) {
			/* found a previous ECN_ECHO update it if needed */
			uint32_t cnt, ctsn;
			ecne = mtod(chk->data, struct sctp_ecne_chunk *);
			ctsn = ntohl(ecne->tsn);
			if (SCTP_TSN_GT(high_tsn, ctsn)) {
				ecne->tsn = htonl(high_tsn);
				SCTP_STAT_INCR(sctps_queue_upd_ecne);
			}
			cnt = ntohl(ecne->num_pkts_since_cwr);
			cnt++;
			ecne->num_pkts_since_cwr = htonl(cnt);
			return;
		}
	}
	/* nope could not find one to update so we must build one */
	sctp_alloc_a_chunk(stcb, chk);
	if (chk == NULL) {
		return;
	}
	chk->copy_by_ref = 0;
	SCTP_STAT_INCR(sctps_queue_upd_ecne);
	chk->rec.chunk_id.id = SCTP_ECN_ECHO;
	chk->rec.chunk_id.can_take_data = 0;
	chk->asoc = &stcb->asoc;
	chk->send_size = sizeof(struct sctp_ecne_chunk);
	chk->data = sctp_get_mbuf_for_msg(chk->send_size, 0, M_DONTWAIT, 1, MT_HEADER);
	if (chk->data == NULL) {
		sctp_free_a_chunk(stcb, chk, SCTP_SO_NOT_LOCKED);
		return;
	}
	SCTP_BUF_RESV_UF(chk->data, SCTP_MIN_OVERHEAD);
	SCTP_BUF_LEN(chk->data) = chk->send_size;
	chk->sent = SCTP_DATAGRAM_UNSENT;
	chk->snd_count = 0;
	chk->whoTo = net;
	atomic_add_int(&chk->whoTo->ref_count, 1);

	stcb->asoc.ecn_echo_cnt_onq++;
	ecne = mtod(chk->data, struct sctp_ecne_chunk *);
	ecne->ch.chunk_type = SCTP_ECN_ECHO;
	ecne->ch.chunk_flags = 0;
	ecne->ch.chunk_length = htons(sizeof(struct sctp_ecne_chunk));
	ecne->tsn = htonl(high_tsn);
	ecne->num_pkts_since_cwr = htonl(1);
	TAILQ_INSERT_HEAD(&stcb->asoc.control_send_queue, chk, sctp_next);
	asoc->ctrl_queue_cnt++;
}

void
sctp_send_packet_dropped(struct sctp_tcb *stcb, struct sctp_nets *net,
    struct mbuf *m, int iphlen, int bad_crc)
{
	struct sctp_association *asoc;
	struct sctp_pktdrop_chunk *drp;
	struct sctp_tmit_chunk *chk;
	uint8_t *datap;
	int len;
	int was_trunc=0;
	struct ip *iph;
#ifdef INET6
	struct ip6_hdr *ip6h;
#endif
	int fullsz=0, extra=0;
	long spc;
	int offset;
	struct sctp_chunkhdr *ch, chunk_buf;
	unsigned int chk_length;

        if (!stcb) {
            return;
        }
	asoc = &stcb->asoc;
	SCTP_TCB_LOCK_ASSERT(stcb);
	if (asoc->peer_supports_pktdrop == 0) {
		/*-
		 * peer must declare support before I send one.
		 */
		return;
	}
	if (stcb->sctp_socket == NULL) {
		return;
	}
	sctp_alloc_a_chunk(stcb, chk);
	if (chk == NULL) {
		return;
	}
	chk->copy_by_ref = 0;
	iph = mtod(m, struct ip *);
	if (iph == NULL) {
		sctp_free_a_chunk(stcb, chk, SCTP_SO_NOT_LOCKED);
		return;
	}
	switch (iph->ip_v) {
#ifdef INET
	case IPVERSION:
		/* IPv4 */
#if defined(__FreeBSD__) || defined(__APPLE__)
		len = chk->send_size = iph->ip_len;
#elif defined(__Userspace__)
#if defined(__Userspace_os_FreeBSD)
                len = chk->send_size = SCTP_GET_IPV4_LENGTH(iph);
#else
                len = chk->send_size = (SCTP_GET_IPV4_LENGTH(iph) - iphlen);
                extra = iphlen;
#endif
#else
		len = chk->send_size = (iph->ip_len - iphlen);
		extra = iphlen;
#endif
		break;
#endif
#ifdef INET6
	case IPV6_VERSION >> 4:
		/* IPv6 */
		ip6h = mtod(m, struct ip6_hdr *);
		len = chk->send_size = htons(ip6h->ip6_plen);
		break;
#endif
	default:
		return;
	}
        /* Validate that we do not have an ABORT in here. */
	offset = iphlen + sizeof(struct sctphdr);
	ch = (struct sctp_chunkhdr *)sctp_m_getptr(m, offset,
						   sizeof(*ch), (uint8_t *) & chunk_buf);
	while (ch != NULL) {
		chk_length = ntohs(ch->chunk_length);
		if (chk_length < sizeof(*ch)) {
			/* break to abort land */
			break;
		}
		switch (ch->chunk_type) {
		case SCTP_PACKET_DROPPED:
		case SCTP_ABORT_ASSOCIATION:
		case SCTP_INITIATION_ACK:
			/**
			 * We don't respond with an PKT-DROP to an ABORT
			 * or PKT-DROP. We also do not respond to an
			 * INIT-ACK, because we can't know if the initiation
			 * tag is correct or not.
			 */
			sctp_free_a_chunk(stcb, chk, SCTP_SO_NOT_LOCKED);
			return;
		default:
			break;
		}
		offset += SCTP_SIZE32(chk_length);
		ch = (struct sctp_chunkhdr *)sctp_m_getptr(m, offset,
		    sizeof(*ch), (uint8_t *) & chunk_buf);
	}

	if ((len + SCTP_MAX_OVERHEAD + sizeof(struct sctp_pktdrop_chunk)) >
	    min(stcb->asoc.smallest_mtu, MCLBYTES)) {
		/* only send 1 mtu worth, trim off the
		 * excess on the end.
		 */
		fullsz = len - extra;
		len = min(stcb->asoc.smallest_mtu,MCLBYTES) - SCTP_MAX_OVERHEAD;
		was_trunc = 1;
	}
	chk->asoc = &stcb->asoc;
	chk->data = sctp_get_mbuf_for_msg(MCLBYTES, 0, M_DONTWAIT, 1, MT_DATA);
	if (chk->data == NULL) {
jump_out:
		sctp_free_a_chunk(stcb, chk, SCTP_SO_NOT_LOCKED);
		return;
	}
	SCTP_BUF_RESV_UF(chk->data, SCTP_MIN_OVERHEAD);
	drp = mtod(chk->data, struct sctp_pktdrop_chunk *);
	if (drp == NULL) {
		sctp_m_freem(chk->data);
		chk->data = NULL;
		goto jump_out;
	}
	chk->book_size = SCTP_SIZE32((chk->send_size + sizeof(struct sctp_pktdrop_chunk) +
	    sizeof(struct sctphdr) + SCTP_MED_OVERHEAD));
	chk->book_size_scale = 0;
	if (was_trunc) {
		drp->ch.chunk_flags = SCTP_PACKET_TRUNCATED;
		drp->trunc_len = htons(fullsz);
		/* Len is already adjusted to size minus overhead above
		 * take out the pkt_drop chunk itself from it.
		 */
		chk->send_size = len - sizeof(struct sctp_pktdrop_chunk);
		len = chk->send_size;
	} else {
		/* no truncation needed */
		drp->ch.chunk_flags = 0;
		drp->trunc_len = htons(0);
	}
	if (bad_crc) {
		drp->ch.chunk_flags |= SCTP_BADCRC;
	}
	chk->send_size += sizeof(struct sctp_pktdrop_chunk);
	SCTP_BUF_LEN(chk->data) = chk->send_size;
	chk->sent = SCTP_DATAGRAM_UNSENT;
	chk->snd_count = 0;
	if (net) {
		/* we should hit here */
		chk->whoTo = net;
		atomic_add_int(&chk->whoTo->ref_count, 1);
	} else {
		chk->whoTo = NULL;
	}
	chk->rec.chunk_id.id = SCTP_PACKET_DROPPED;
	chk->rec.chunk_id.can_take_data = 1;
	drp->ch.chunk_type = SCTP_PACKET_DROPPED;
	drp->ch.chunk_length = htons(chk->send_size);
	spc = SCTP_SB_LIMIT_RCV(stcb->sctp_socket);
	if (spc < 0) {
		spc = 0;
	}
	drp->bottle_bw = htonl(spc);
	if (asoc->my_rwnd) {
		drp->current_onq = htonl(asoc->size_on_reasm_queue +
		    asoc->size_on_all_streams +
		    asoc->my_rwnd_control_len +
		    stcb->sctp_socket->so_rcv.sb_cc);
	} else {
		/*-
		 * If my rwnd is 0, possibly from mbuf depletion as well as
		 * space used, tell the peer there is NO space aka onq == bw
		 */
		drp->current_onq = htonl(spc);
	}
	drp->reserved = 0;
	datap = drp->data;
	m_copydata(m, iphlen, len, (caddr_t)datap);
	TAILQ_INSERT_TAIL(&stcb->asoc.control_send_queue, chk, sctp_next);
	asoc->ctrl_queue_cnt++;
}

void
sctp_send_cwr(struct sctp_tcb *stcb, struct sctp_nets *net, uint32_t high_tsn, uint8_t override)
{
	struct sctp_association *asoc;
	struct sctp_cwr_chunk *cwr;
	struct sctp_tmit_chunk *chk;

	asoc = &stcb->asoc;
	SCTP_TCB_LOCK_ASSERT(stcb);
	if (net == NULL) {
		return;
	}
	TAILQ_FOREACH(chk, &asoc->control_send_queue, sctp_next) {
		if ((chk->rec.chunk_id.id == SCTP_ECN_CWR) && (net == chk->whoTo)) {
			/* found a previous CWR queued to same destination update it if needed */
			uint32_t ctsn;
			cwr = mtod(chk->data, struct sctp_cwr_chunk *);
			ctsn = ntohl(cwr->tsn);
			if (SCTP_TSN_GT(high_tsn, ctsn)) {
				cwr->tsn = htonl(high_tsn);
			}
			if (override & SCTP_CWR_REDUCE_OVERRIDE) {
				/* Make sure override is carried */
				cwr->ch.chunk_flags |= SCTP_CWR_REDUCE_OVERRIDE;
			}
			return;
		}
	}
	sctp_alloc_a_chunk(stcb, chk);
	if (chk == NULL) {
		return;
	}
	chk->copy_by_ref = 0;
	chk->rec.chunk_id.id = SCTP_ECN_CWR;
	chk->rec.chunk_id.can_take_data = 1;
	chk->asoc = &stcb->asoc;
	chk->send_size = sizeof(struct sctp_cwr_chunk);
	chk->data = sctp_get_mbuf_for_msg(chk->send_size, 0, M_DONTWAIT, 1, MT_HEADER);
	if (chk->data == NULL) {
		sctp_free_a_chunk(stcb, chk, SCTP_SO_NOT_LOCKED);
		return;
	}
	SCTP_BUF_RESV_UF(chk->data, SCTP_MIN_OVERHEAD);
	SCTP_BUF_LEN(chk->data) = chk->send_size;
	chk->sent = SCTP_DATAGRAM_UNSENT;
	chk->snd_count = 0;
	chk->whoTo = net;
	atomic_add_int(&chk->whoTo->ref_count, 1);
	cwr = mtod(chk->data, struct sctp_cwr_chunk *);
	cwr->ch.chunk_type = SCTP_ECN_CWR;
	cwr->ch.chunk_flags = override;
	cwr->ch.chunk_length = htons(sizeof(struct sctp_cwr_chunk));
	cwr->tsn = htonl(high_tsn);
	TAILQ_INSERT_TAIL(&stcb->asoc.control_send_queue, chk, sctp_next);
	asoc->ctrl_queue_cnt++;
}

void
sctp_add_stream_reset_out(struct sctp_tmit_chunk *chk,
    int number_entries, uint16_t * list,
    uint32_t seq, uint32_t resp_seq, uint32_t last_sent)
{
	int len, old_len, i;
	struct sctp_stream_reset_out_request *req_out;
	struct sctp_chunkhdr *ch;

	ch = mtod(chk->data, struct sctp_chunkhdr *);


	old_len = len = SCTP_SIZE32(ntohs(ch->chunk_length));

	/* get to new offset for the param. */
	req_out = (struct sctp_stream_reset_out_request *)((caddr_t)ch + len);
	/* now how long will this param be? */
	len = (sizeof(struct sctp_stream_reset_out_request) + (sizeof(uint16_t) * number_entries));
	req_out->ph.param_type = htons(SCTP_STR_RESET_OUT_REQUEST);
	req_out->ph.param_length = htons(len);
	req_out->request_seq = htonl(seq);
	req_out->response_seq = htonl(resp_seq);
	req_out->send_reset_at_tsn = htonl(last_sent);
	if (number_entries) {
		for (i = 0; i < number_entries; i++) {
			req_out->list_of_streams[i] = htons(list[i]);
		}
	}
	if (SCTP_SIZE32(len) > len) {
		/*-
		 * Need to worry about the pad we may end up adding to the
		 * end. This is easy since the struct is either aligned to 4
		 * bytes or 2 bytes off.
		 */
		req_out->list_of_streams[number_entries] = 0;
	}
	/* now fix the chunk length */
	ch->chunk_length = htons(len + old_len);
	chk->book_size = len + old_len;
	chk->book_size_scale = 0;
	chk->send_size = SCTP_SIZE32(chk->book_size);
	SCTP_BUF_LEN(chk->data) = chk->send_size;
	return;
}


void
sctp_add_stream_reset_in(struct sctp_tmit_chunk *chk,
    int number_entries, uint16_t * list,
    uint32_t seq)
{
	int len, old_len, i;
	struct sctp_stream_reset_in_request *req_in;
	struct sctp_chunkhdr *ch;

	ch = mtod(chk->data, struct sctp_chunkhdr *);


	old_len = len = SCTP_SIZE32(ntohs(ch->chunk_length));

	/* get to new offset for the param. */
	req_in = (struct sctp_stream_reset_in_request *)((caddr_t)ch + len);
	/* now how long will this param be? */
	len = (sizeof(struct sctp_stream_reset_in_request) + (sizeof(uint16_t) * number_entries));
	req_in->ph.param_type = htons(SCTP_STR_RESET_IN_REQUEST);
	req_in->ph.param_length = htons(len);
	req_in->request_seq = htonl(seq);
	if (number_entries) {
		for (i = 0; i < number_entries; i++) {
			req_in->list_of_streams[i] = htons(list[i]);
		}
	}
	if (SCTP_SIZE32(len) > len) {
		/*-
		 * Need to worry about the pad we may end up adding to the
		 * end. This is easy since the struct is either aligned to 4
		 * bytes or 2 bytes off.
		 */
		req_in->list_of_streams[number_entries] = 0;
	}
	/* now fix the chunk length */
	ch->chunk_length = htons(len + old_len);
	chk->book_size = len + old_len;
	chk->book_size_scale = 0;
	chk->send_size = SCTP_SIZE32(chk->book_size);
	SCTP_BUF_LEN(chk->data) = chk->send_size;
	return;
}


void
sctp_add_stream_reset_tsn(struct sctp_tmit_chunk *chk,
    uint32_t seq)
{
	int len, old_len;
	struct sctp_stream_reset_tsn_request *req_tsn;
	struct sctp_chunkhdr *ch;

	ch = mtod(chk->data, struct sctp_chunkhdr *);


	old_len = len = SCTP_SIZE32(ntohs(ch->chunk_length));

	/* get to new offset for the param. */
	req_tsn = (struct sctp_stream_reset_tsn_request *)((caddr_t)ch + len);
	/* now how long will this param be? */
	len = sizeof(struct sctp_stream_reset_tsn_request);
	req_tsn->ph.param_type = htons(SCTP_STR_RESET_TSN_REQUEST);
	req_tsn->ph.param_length = htons(len);
	req_tsn->request_seq = htonl(seq);

	/* now fix the chunk length */
	ch->chunk_length = htons(len + old_len);
	chk->send_size = len + old_len;
	chk->book_size = SCTP_SIZE32(chk->send_size);
	chk->book_size_scale = 0;
	SCTP_BUF_LEN(chk->data) = SCTP_SIZE32(chk->send_size);
	return;
}

void
sctp_add_stream_reset_result(struct sctp_tmit_chunk *chk,
    uint32_t resp_seq, uint32_t result)
{
	int len, old_len;
	struct sctp_stream_reset_response *resp;
	struct sctp_chunkhdr *ch;

	ch = mtod(chk->data, struct sctp_chunkhdr *);


	old_len = len = SCTP_SIZE32(ntohs(ch->chunk_length));

	/* get to new offset for the param. */
	resp = (struct sctp_stream_reset_response *)((caddr_t)ch + len);
	/* now how long will this param be? */
	len = sizeof(struct sctp_stream_reset_response);
	resp->ph.param_type = htons(SCTP_STR_RESET_RESPONSE);
	resp->ph.param_length = htons(len);
	resp->response_seq = htonl(resp_seq);
	resp->result = ntohl(result);

	/* now fix the chunk length */
	ch->chunk_length = htons(len + old_len);
	chk->book_size = len + old_len;
	chk->book_size_scale = 0;
	chk->send_size = SCTP_SIZE32(chk->book_size);
	SCTP_BUF_LEN(chk->data) = chk->send_size;
	return;

}


void
sctp_add_stream_reset_result_tsn(struct sctp_tmit_chunk *chk,
    uint32_t resp_seq, uint32_t result,
    uint32_t send_una, uint32_t recv_next)
{
	int len, old_len;
	struct sctp_stream_reset_response_tsn *resp;
	struct sctp_chunkhdr *ch;

	ch = mtod(chk->data, struct sctp_chunkhdr *);


	old_len = len = SCTP_SIZE32(ntohs(ch->chunk_length));

	/* get to new offset for the param. */
	resp = (struct sctp_stream_reset_response_tsn *)((caddr_t)ch + len);
	/* now how long will this param be? */
	len = sizeof(struct sctp_stream_reset_response_tsn);
	resp->ph.param_type = htons(SCTP_STR_RESET_RESPONSE);
	resp->ph.param_length = htons(len);
	resp->response_seq = htonl(resp_seq);
	resp->result = htonl(result);
	resp->senders_next_tsn = htonl(send_una);
	resp->receivers_next_tsn = htonl(recv_next);

	/* now fix the chunk length */
	ch->chunk_length = htons(len + old_len);
	chk->book_size = len + old_len;
	chk->send_size = SCTP_SIZE32(chk->book_size);
	chk->book_size_scale = 0;
	SCTP_BUF_LEN(chk->data) = chk->send_size;
	return;
}

static void
sctp_add_a_stream(struct sctp_tmit_chunk *chk,
				  uint32_t seq,
				  uint16_t adding)
{
	int len, old_len;
	struct sctp_chunkhdr *ch;
	struct sctp_stream_reset_add_strm *addstr;
	ch = mtod(chk->data, struct sctp_chunkhdr *);
	old_len = len = SCTP_SIZE32(ntohs(ch->chunk_length));

	/* get to new offset for the param. */
	addstr = (struct sctp_stream_reset_add_strm *)((caddr_t)ch + len);
	/* now how long will this param be? */
	len = sizeof(struct sctp_stream_reset_add_strm);

	/* Fill it out. */
	addstr->ph.param_type = htons(SCTP_STR_RESET_ADD_STREAMS);
	addstr->ph.param_length = htons(len);
	addstr->request_seq = htonl(seq);
	addstr->number_of_streams = htons(adding);
	addstr->reserved = 0;

	/* now fix the chunk length */
	ch->chunk_length = htons(len + old_len);
	chk->send_size = len + old_len;
	chk->book_size = SCTP_SIZE32(chk->send_size);
	chk->book_size_scale = 0;
	SCTP_BUF_LEN(chk->data) = SCTP_SIZE32(chk->send_size);
	return;
}

int
sctp_send_str_reset_req(struct sctp_tcb *stcb,
			int number_entries, uint16_t * list,
			uint8_t send_out_req,
			uint32_t resp_seq,
			uint8_t send_in_req,
			uint8_t send_tsn_req,
			uint8_t add_stream,
			uint16_t adding
	)
{

	struct sctp_association *asoc;
	struct sctp_tmit_chunk *chk;
	struct sctp_chunkhdr *ch;
	uint32_t seq;

	asoc = &stcb->asoc;
	if (asoc->stream_reset_outstanding) {
		/*-
		 * Already one pending, must get ACK back to clear the flag.
		 */
		SCTP_LTRACE_ERR_RET(NULL, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, EBUSY);
		return (EBUSY);
	}
	if ((send_out_req == 0) && (send_in_req == 0) && (send_tsn_req == 0) &&
		(add_stream== 0)) {
		/* nothing to do */
		SCTP_LTRACE_ERR_RET(NULL, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, EINVAL);	
		return (EINVAL);
	}
	if (send_tsn_req && (send_out_req || send_in_req)) {
		/* error, can't do that */
		SCTP_LTRACE_ERR_RET(NULL, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, EINVAL);
		return (EINVAL);
	}
	sctp_alloc_a_chunk(stcb, chk);
	if (chk == NULL) {
		SCTP_LTRACE_ERR_RET(NULL, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, ENOMEM);
		return (ENOMEM);
	}
	chk->copy_by_ref = 0;
	chk->rec.chunk_id.id = SCTP_STREAM_RESET;
	chk->rec.chunk_id.can_take_data = 0;
	chk->asoc = &stcb->asoc;
	chk->book_size = sizeof(struct sctp_chunkhdr);
	chk->send_size = SCTP_SIZE32(chk->book_size);
	chk->book_size_scale = 0;

	chk->data = sctp_get_mbuf_for_msg(MCLBYTES, 0, M_DONTWAIT, 1, MT_DATA);
	if (chk->data == NULL) {
		sctp_free_a_chunk(stcb, chk, SCTP_SO_LOCKED);
		SCTP_LTRACE_ERR_RET(NULL, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, ENOMEM);
		return (ENOMEM);
	}
	SCTP_BUF_RESV_UF(chk->data, SCTP_MIN_OVERHEAD);

	/* setup chunk parameters */
	chk->sent = SCTP_DATAGRAM_UNSENT;
	chk->snd_count = 0;
	if (stcb->asoc.alternate) {
		chk->whoTo = stcb->asoc.alternate;
	} else {
		chk->whoTo = stcb->asoc.primary_destination;
	}
	atomic_add_int(&chk->whoTo->ref_count, 1);
	ch = mtod(chk->data, struct sctp_chunkhdr *);
	ch->chunk_type = SCTP_STREAM_RESET;
	ch->chunk_flags = 0;
	ch->chunk_length = htons(chk->book_size);
	SCTP_BUF_LEN(chk->data) = chk->send_size;

	seq = stcb->asoc.str_reset_seq_out;
	if (send_out_req) {
		sctp_add_stream_reset_out(chk, number_entries, list,
		    seq, resp_seq, (stcb->asoc.sending_seq - 1));
		asoc->stream_reset_out_is_outstanding = 1;
		seq++;
		asoc->stream_reset_outstanding++;
	}
	if (add_stream) {
	  sctp_add_a_stream(chk, seq, adding);
	  seq++;
	  asoc->stream_reset_outstanding++;
	}

	if (send_in_req) {
		sctp_add_stream_reset_in(chk, number_entries, list, seq);
		asoc->stream_reset_outstanding++;
	}
	if (send_tsn_req) {
		sctp_add_stream_reset_tsn(chk, seq);
		asoc->stream_reset_outstanding++;
	}
	asoc->str_reset = chk;

	/* insert the chunk for sending */
	TAILQ_INSERT_TAIL(&asoc->control_send_queue,
	    chk,
	    sctp_next);
	asoc->ctrl_queue_cnt++;
	sctp_timer_start(SCTP_TIMER_TYPE_STRRESET, stcb->sctp_ep, stcb, chk->whoTo);
	return (0);
}

void
sctp_send_abort(struct mbuf *m, int iphlen, struct sctphdr *sh, uint32_t vtag,
    struct mbuf *err_cause, uint32_t vrf_id, uint16_t port)
{
	/*-
	 * Formulate the abort message, and send it back down.
	 */
#ifdef __Panda__
	pakhandle_type o_pak;
#else
	struct mbuf *o_pak;
#endif
	struct mbuf *mout;
	struct sctp_abort_msg *abm;
	struct ip *iph;
	struct udphdr *udp;
	int iphlen_out, len;
#ifdef INET
	struct ip *iph_out;
#endif
#ifdef INET6
	struct ip6_hdr *ip6, *ip6_out;
#endif

	/* don't respond to ABORT with ABORT */
	if (sctp_is_there_an_abort_here(m, iphlen, &vtag)) {
		if (err_cause)
			sctp_m_freem(err_cause);
		return;
	}

	iph = mtod(m, struct ip *);
	switch (iph->ip_v) {
#ifdef INET
	case IPVERSION:
		len = (sizeof(struct ip) + sizeof(struct sctp_abort_msg));
		break;
#endif
#ifdef INET6
	case IPV6_VERSION >> 4:
		len = (sizeof(struct ip6_hdr) + sizeof(struct sctp_abort_msg));
		break;
#endif
	default:
		if (err_cause) {
			sctp_m_freem(err_cause);
		}
		return;
	}
	if (port) {
		len += sizeof(struct udphdr);
	}
#if defined(APPLE_LION)
	mout = sctp_get_mbuf_for_msg(len + SCTP_MAX_LINKHDR, 1, M_DONTWAIT, 1, MT_DATA);
#else
	mout = sctp_get_mbuf_for_msg(len + max_linkhdr, 1, M_DONTWAIT, 1, MT_DATA);
#endif
	if (mout == NULL) {
		if (err_cause) {
			sctp_m_freem(err_cause);
		}
		return;
	}
#if defined(APPLE_LION)
	SCTP_BUF_RESV_UF(mout, SCTP_MAX_LINKHDR);
#else
	SCTP_BUF_RESV_UF(mout, max_linkhdr);
#endif
	SCTP_BUF_LEN(mout) = len;
	SCTP_BUF_NEXT(mout) = err_cause;
#if defined(__FreeBSD__)
	if (m->m_flags & M_FLOWID) {
		mout->m_pkthdr.flowid = m->m_pkthdr.flowid;
		mout->m_flags |= M_FLOWID;
	}
#endif
#ifdef INET
	iph_out = NULL;
#endif
#ifdef INET6
	ip6_out = NULL;
#endif
	switch (iph->ip_v) {
#ifdef INET
	case IPVERSION:
		iph_out = mtod(mout, struct ip *);

		/* Fill in the IP header for the ABORT */
		iph_out->ip_v = IPVERSION;
		iph_out->ip_hl = (sizeof(struct ip) / 4);
		iph_out->ip_tos = (u_char)0;
		iph_out->ip_id = 0;
		iph_out->ip_off = 0;
		iph_out->ip_ttl = MAXTTL;
		if (port) {
			iph_out->ip_p = IPPROTO_UDP;
		} else {
			iph_out->ip_p = IPPROTO_SCTP;
		}
		iph_out->ip_src.s_addr = iph->ip_dst.s_addr;
		iph_out->ip_dst.s_addr = iph->ip_src.s_addr;
		/* let IP layer calculate this */
		iph_out->ip_sum = 0;

		iphlen_out = sizeof(*iph_out);
		abm = (struct sctp_abort_msg *)((caddr_t)iph_out + iphlen_out);
		break;
#endif
#ifdef INET6
#if !defined(__Userspace__) /* TODO declare ip6_defhlim */
	case IPV6_VERSION >> 4:
		ip6 = (struct ip6_hdr *)iph;
		ip6_out = mtod(mout, struct ip6_hdr *);

		/* Fill in the IP6 header for the ABORT */
		ip6_out->ip6_flow = ip6->ip6_flow;
		ip6_out->ip6_hlim = MODULE_GLOBAL(ip6_defhlim);
		if (port) {
			ip6_out->ip6_nxt = IPPROTO_UDP;
		} else {
			ip6_out->ip6_nxt = IPPROTO_SCTP;
		}
		ip6_out->ip6_src = ip6->ip6_dst;
		ip6_out->ip6_dst = ip6->ip6_src;

		iphlen_out = sizeof(*ip6_out);
		abm = (struct sctp_abort_msg *)((caddr_t)ip6_out + iphlen_out);
		break;
#endif /* ! __Userspace__ */
#endif /* INET6 */
	default:
		/* Currently not supported */
		sctp_m_freem(mout);
		return;
	}

	udp = (struct udphdr *)abm;
	if (port) {
		udp->uh_sport = htons(SCTP_BASE_SYSCTL(sctp_udp_tunneling_port));
		udp->uh_dport = port;
		/* set udp->uh_ulen later */	
		udp->uh_sum = 0;
		iphlen_out += sizeof(struct udphdr);
		abm = (struct sctp_abort_msg *)((caddr_t)abm + sizeof(struct udphdr));
	}

	abm->sh.src_port = sh->dest_port;
	abm->sh.dest_port = sh->src_port;
	abm->sh.checksum = 0;
	if (vtag == 0) {
		abm->sh.v_tag = sh->v_tag;
		abm->msg.ch.chunk_flags = SCTP_HAD_NO_TCB;
	} else {
		abm->sh.v_tag = htonl(vtag);
		abm->msg.ch.chunk_flags = 0;
	}
	abm->msg.ch.chunk_type = SCTP_ABORT_ASSOCIATION;

	if (err_cause) {
		struct mbuf *m_tmp = err_cause;
		int err_len = 0;

		/* get length of the err_cause chain */
		while (m_tmp != NULL) {
			err_len += SCTP_BUF_LEN(m_tmp);
			m_tmp = SCTP_BUF_NEXT(m_tmp);
		}
		len = SCTP_BUF_LEN(mout) + err_len;
		if (err_len % 4) {
			/* need pad at end of chunk */
			uint32_t cpthis = 0;
			int padlen;

			padlen = 4 - (len % 4);
			m_copyback(mout, len, padlen, (caddr_t)&cpthis);
			len += padlen;
		}
		abm->msg.ch.chunk_length = htons(sizeof(abm->msg.ch) + err_len);
	} else {
		len = SCTP_BUF_LEN(mout);
		abm->msg.ch.chunk_length = htons(sizeof(abm->msg.ch));
	}

	if (SCTP_GET_HEADER_FOR_OUTPUT(o_pak)) {
		/* no mbuf's */
		sctp_m_freem(mout);
		return;
	}
#ifdef INET
	if (iph_out != NULL) {
		sctp_route_t ro;
		int ret;

		/* zap the stack pointer to the route */
		bzero(&ro, sizeof ro);
#if defined(__Panda__)
		ro._l_addr.sa.sa_family = AF_INET;
#endif
		if (port) {
			udp->uh_ulen = htons(len - sizeof(struct ip));
#if !defined(__Windows__) && !defined(__Userspace__)
			udp->uh_sum = in_pseudo(iph_out->ip_src.s_addr, iph_out->ip_dst.s_addr, udp->uh_ulen + htons(IPPROTO_UDP));
#else
			udp->uh_sum = 0;
#endif
		}
		SCTPDBG(SCTP_DEBUG_OUTPUT2, "sctp_send_abort calling ip_output:\n");
		SCTPDBG_PKT(SCTP_DEBUG_OUTPUT2, iph_out, &abm->sh);
		/* set IPv4 length */
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Userspace__)
		iph_out->ip_len = len;
#else
		iph_out->ip_len = htons(len);
#endif
		/* out it goes */
#ifdef  SCTP_PACKET_LOGGING
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LAST_PACKET_TRACING)
			sctp_packet_log(mout, len);
#endif
		SCTP_ATTACH_CHAIN(o_pak, mout, len);
		if (port) {
#if defined(SCTP_WITH_NO_CSUM)
			SCTP_STAT_INCR(sctps_sendnocrc);
#else
			abm->sh.checksum = sctp_calculate_cksum(mout, iphlen_out);
			SCTP_STAT_INCR(sctps_sendswcrc);
#endif
			SCTP_ENABLE_UDP_CSUM(o_pak);
		} else {
#if defined(SCTP_WITH_NO_CSUM)
			SCTP_STAT_INCR(sctps_sendnocrc);
#else
#if defined(__FreeBSD__) && __FreeBSD_version >= 800000
			mout->m_pkthdr.csum_flags = CSUM_SCTP;
			mout->m_pkthdr.csum_data = 0;
			SCTP_STAT_INCR(sctps_sendhwcrc);
#else
			abm->sh.checksum = sctp_calculate_cksum(mout, iphlen_out);
			SCTP_STAT_INCR(sctps_sendswcrc);
#endif
#endif
		}
		SCTP_IP_OUTPUT(ret, o_pak, &ro, NULL, vrf_id);

		/* Free the route if we got one back */
		if (ro.ro_rt)
			RTFREE(ro.ro_rt);
	}
#endif
#ifdef INET6
	if (ip6_out != NULL) {
#if defined(__Userspace__)
		sctp_route_t ro;
#else
		struct route_in6 ro;
#endif
		int ret;
#if !defined(__Panda__)
		struct ifnet *ifp = NULL;
#endif
		/* zap the stack pointer to the route */
		bzero(&ro, sizeof(ro));
#if defined(__Panda__)
		ro._l_addr.sa.sa_family = AF_INET6;
#endif
#if !defined(__Userspace__)
		if (port) {
			udp->uh_ulen = htons(len - sizeof(struct ip6_hdr));
		}
#endif
		SCTPDBG(SCTP_DEBUG_OUTPUT2, "sctp_send_abort calling ip6_output:\n");
		SCTPDBG_PKT(SCTP_DEBUG_OUTPUT2, (struct ip *)ip6_out, &abm->sh);
		ip6_out->ip6_plen = len - sizeof(*ip6_out);
#ifdef  SCTP_PACKET_LOGGING
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LAST_PACKET_TRACING)
			sctp_packet_log(mout, len);
#endif
		SCTP_ATTACH_CHAIN(o_pak, mout, len);
		if (port) {
#if defined(SCTP_WITH_NO_CSUM)
			SCTP_STAT_INCR(sctps_sendnocrc);
#else
			abm->sh.checksum = sctp_calculate_cksum(mout, sizeof(struct ip6_hdr) + sizeof(struct udphdr));
			SCTP_STAT_INCR(sctps_sendswcrc);
#endif
#if defined(__Windows__)
			udp->uh_sum = 0;
#elif !defined(__Userspace__)
			if ((udp->uh_sum = in6_cksum(o_pak, IPPROTO_UDP, sizeof(struct ip6_hdr), len - sizeof(struct ip6_hdr))) == 0) {
				udp->uh_sum = 0xffff;
			}
#endif
		} else {
#if defined(SCTP_WITH_NO_CSUM)
			SCTP_STAT_INCR(sctps_sendnocrc);
#else
#if defined(__FreeBSD__) && __FreeBSD_version >= 800000
			mout->m_pkthdr.csum_flags = CSUM_SCTP;
			mout->m_pkthdr.csum_data = 0;
			SCTP_STAT_INCR(sctps_sendhwcrc);
#else
			abm->sh.checksum = sctp_calculate_cksum(mout, sizeof(struct ip6_hdr));
			SCTP_STAT_INCR(sctps_sendswcrc);
#endif
#endif
		}
		SCTP_IP6_OUTPUT(ret, o_pak, &ro, &ifp, NULL, vrf_id);

		/* Free the route if we got one back */
		if (ro.ro_rt)
			RTFREE(ro.ro_rt);
	}
#endif
	SCTP_STAT_INCR(sctps_sendpackets);
	SCTP_STAT_INCR_COUNTER64(sctps_outpackets);
	SCTP_STAT_INCR_COUNTER64(sctps_outcontrolchunks);
}

void
sctp_send_operr_to(struct mbuf *m, int iphlen, struct mbuf *scm, uint32_t vtag,
		   uint32_t vrf_id, uint16_t port)
{
#ifdef __Panda__
	pakhandle_type o_pak;
#else
	struct mbuf *o_pak;
#endif
	struct sctphdr *sh, *sh_out;
	struct sctp_chunkhdr *ch;
	struct ip *iph;
	struct udphdr *udp = NULL;
	struct mbuf *mout;
	int iphlen_out, len;
#ifdef INET
	struct ip *iph_out;
#endif
#ifdef INET6
	struct ip6_hdr *ip6, *ip6_out;
#endif

	iph = mtod(m, struct ip *);
	sh = (struct sctphdr *)((caddr_t)iph + iphlen);
	switch (iph->ip_v) {
#ifdef INET
	case IPVERSION:
		len = (sizeof(struct ip) + sizeof(struct sctphdr) + sizeof(struct sctp_chunkhdr));
		break;
#endif
#ifdef INET6
	case IPV6_VERSION >> 4:
		len = (sizeof(struct ip6_hdr) + sizeof(struct sctphdr) + sizeof(struct sctp_chunkhdr));
		break;
#endif
	default:
		if (scm) {
			sctp_m_freem(scm);
		}
		return;
	}
	if (port) {
		len += sizeof(struct udphdr);
	}
#if defined(APPLE_LION)
	mout = sctp_get_mbuf_for_msg(len + SCTP_MAX_LINKHDR, 1, M_DONTWAIT, 1, MT_DATA);
#else
	mout = sctp_get_mbuf_for_msg(len + max_linkhdr, 1, M_DONTWAIT, 1, MT_DATA);
#endif
	if (mout == NULL) {
		if (scm) {
			sctp_m_freem(scm);
		}
		return;
	}
#if defined(APPLE_LION)
	SCTP_BUF_RESV_UF(mout, SCTP_MAX_LINKHDR);
#else
	SCTP_BUF_RESV_UF(mout, max_linkhdr);
#endif
	SCTP_BUF_LEN(mout) = len;
	SCTP_BUF_NEXT(mout) = scm;
#if defined(__FreeBSD__)
	if (m->m_flags & M_FLOWID) {
		mout->m_pkthdr.flowid = m->m_pkthdr.flowid;
		mout->m_flags |= M_FLOWID;
	}
#endif
#ifdef INET
	iph_out = NULL;
#endif
#ifdef INET6
	ip6_out = NULL;
#endif
	switch (iph->ip_v) {
#ifdef INET
	case IPVERSION:
		iph_out = mtod(mout, struct ip *);

		/* Fill in the IP header for the ABORT */
		iph_out->ip_v = IPVERSION;
		iph_out->ip_hl = (sizeof(struct ip) / 4);
		iph_out->ip_tos = (u_char)0;
		iph_out->ip_id = 0;
		iph_out->ip_off = 0;
		iph_out->ip_ttl = MAXTTL;
		if (port) {
			iph_out->ip_p = IPPROTO_UDP;
		} else {
			iph_out->ip_p = IPPROTO_SCTP;
		}
		iph_out->ip_src.s_addr = iph->ip_dst.s_addr;
		iph_out->ip_dst.s_addr = iph->ip_src.s_addr;
		/* let IP layer calculate this */
		iph_out->ip_sum = 0;

		iphlen_out = sizeof(struct ip);
		sh_out = (struct sctphdr *)((caddr_t)iph_out + iphlen_out);
		break;
#endif
#ifdef INET6
#if !defined(__Userspace__) /* TODO declare ip6_defhlim */
	case IPV6_VERSION >> 4:
		ip6 = (struct ip6_hdr *)iph;
		ip6_out = mtod(mout, struct ip6_hdr *);

		/* Fill in the IP6 header for the ABORT */
		ip6_out->ip6_flow = ip6->ip6_flow;
		ip6_out->ip6_hlim = MODULE_GLOBAL(ip6_defhlim);
		if (port) {
			ip6_out->ip6_nxt = IPPROTO_UDP;
		} else {
			ip6_out->ip6_nxt = IPPROTO_SCTP;
		}
		ip6_out->ip6_src = ip6->ip6_dst;
		ip6_out->ip6_dst = ip6->ip6_src;

		iphlen_out = sizeof(struct ip6_hdr);
		sh_out = (struct sctphdr *)((caddr_t)ip6_out + iphlen_out);
		break;
#endif /* ! __Userspace__ */
#endif /* INET6 */
	default:
		/* Currently not supported */
		sctp_m_freem(mout);
		return;
	}

	udp = (struct udphdr *)sh_out;
	if (port) {
		udp->uh_sport = htons(SCTP_BASE_SYSCTL(sctp_udp_tunneling_port));
		udp->uh_dport = port;
		/* set udp->uh_ulen later */	
		udp->uh_sum = 0;
		iphlen_out += sizeof(struct udphdr);
		sh_out = (struct sctphdr *)((caddr_t)udp + sizeof(struct udphdr));
	}

	sh_out->src_port = sh->dest_port;
	sh_out->dest_port = sh->src_port;
	sh_out->v_tag = vtag;
	sh_out->checksum = 0;

	ch = (struct sctp_chunkhdr *)((caddr_t)sh_out + sizeof(struct sctphdr));
	ch->chunk_type = SCTP_OPERATION_ERROR;
	ch->chunk_flags = 0;

	if (scm) {
		struct mbuf *m_tmp = scm;
		int cause_len = 0;

		/* get length of the err_cause chain */
		while (m_tmp != NULL) {
			cause_len += SCTP_BUF_LEN(m_tmp);
			m_tmp = SCTP_BUF_NEXT(m_tmp);
		}
		len = SCTP_BUF_LEN(mout) + cause_len;
		if (cause_len % 4) {
			/* need pad at end of chunk */
			uint32_t cpthis = 0;
			int padlen;

			padlen = 4 - (len % 4);
			m_copyback(mout, len, padlen, (caddr_t)&cpthis);
			len += padlen;
		}
		ch->chunk_length = htons(sizeof(struct sctp_chunkhdr) + cause_len);
	} else {
		len = SCTP_BUF_LEN(mout);
		ch->chunk_length = htons(sizeof(struct sctp_chunkhdr));
	}

	if (SCTP_GET_HEADER_FOR_OUTPUT(o_pak)) {
		/* no mbuf's */
		sctp_m_freem(mout);
		return;
	}

#ifdef INET
	if (iph_out != NULL) {
		sctp_route_t ro;
		int ret;

		/* zap the stack pointer to the route */
		bzero(&ro, sizeof ro);
#if defined(__Panda__)
		ro._l_addr.sa.sa_family = AF_INET;
#endif
		if (port) {
			udp->uh_ulen = htons(len - sizeof(struct ip));
#if !defined(__Windows__) && !defined(__Userspace__)
			udp->uh_sum = in_pseudo(iph_out->ip_src.s_addr, iph_out->ip_dst.s_addr, udp->uh_ulen + htons(IPPROTO_UDP));
#else
			udp->uh_sum = 0;
#endif
		}
		/* set IPv4 length */
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__Userspace__)
		iph_out->ip_len = len;
#else
		iph_out->ip_len = htons(len);
#endif
		/* out it goes */
#ifdef  SCTP_PACKET_LOGGING
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LAST_PACKET_TRACING)
			sctp_packet_log(mout, len);
#endif
		SCTP_ATTACH_CHAIN(o_pak, mout, len);
		if (port) {
#if defined(SCTP_WITH_NO_CSUM)
			SCTP_STAT_INCR(sctps_sendnocrc);
#else
			sh_out->checksum = sctp_calculate_cksum(mout, iphlen_out);
			SCTP_STAT_INCR(sctps_sendswcrc);
#endif
			SCTP_ENABLE_UDP_CSUM(o_pak);
		} else {
#if defined(SCTP_WITH_NO_CSUM)
			SCTP_STAT_INCR(sctps_sendnocrc);
#else
#if defined(__FreeBSD__) && __FreeBSD_version >= 800000
			mout->m_pkthdr.csum_flags = CSUM_SCTP;
			mout->m_pkthdr.csum_data = 0;
			SCTP_STAT_INCR(sctps_sendhwcrc);
#else
			sh_out->checksum = sctp_calculate_cksum(mout, iphlen_out);
			SCTP_STAT_INCR(sctps_sendswcrc);
#endif
#endif
		}
		SCTP_IP_OUTPUT(ret, o_pak, &ro, NULL, vrf_id);

		/* Free the route if we got one back */
		if (ro.ro_rt)
			RTFREE(ro.ro_rt);
	}
#endif
#ifdef INET6
	if (ip6_out != NULL) {
#if defined(__Userspace__)
		sctp_route_t ro;
#else
		struct route_in6 ro;
#endif
		int ret;
#if !defined(__Panda__)
		struct ifnet *ifp = NULL;
#endif
		/* zap the stack pointer to the route */
		bzero(&ro, sizeof(ro));
#if defined(__Panda__)
		ro._l_addr.sa.sa_family = AF_INET6;
#endif
#if !defined(__Userspace__)
		if (port) {
			udp->uh_ulen = htons(len - sizeof(struct ip6_hdr));
		}
#endif
		ip6_out->ip6_plen = len - sizeof(*ip6_out);
#ifdef  SCTP_PACKET_LOGGING
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LAST_PACKET_TRACING)
			sctp_packet_log(mout, len);
#endif
		SCTP_ATTACH_CHAIN(o_pak, mout, len);
		if (port) {
#if defined(SCTP_WITH_NO_CSUM)
			SCTP_STAT_INCR(sctps_sendnocrc);
#else
			sh_out->checksum = sctp_calculate_cksum(mout, sizeof(struct ip6_hdr) + sizeof(struct udphdr));
			SCTP_STAT_INCR(sctps_sendswcrc);
#endif
#if defined(__Windows__)
			udp->uh_sum = 0;
#elif !defined(__Userspace__)
			if ((udp->uh_sum = in6_cksum(o_pak, IPPROTO_UDP, sizeof(struct ip6_hdr), len - sizeof(struct ip6_hdr))) == 0) {
				udp->uh_sum = 0xffff;
			}
#endif
		} else {
#if defined(SCTP_WITH_NO_CSUM)
			SCTP_STAT_INCR(sctps_sendnocrc);
#else
#if defined(__FreeBSD__) && __FreeBSD_version >= 800000
			mout->m_pkthdr.csum_flags = CSUM_SCTP;
			mout->m_pkthdr.csum_data = 0;
			SCTP_STAT_INCR(sctps_sendhwcrc);
#else
			sh_out->checksum = sctp_calculate_cksum(mout, sizeof(struct ip6_hdr));
			SCTP_STAT_INCR(sctps_sendswcrc);
#endif
#endif
		}
		SCTP_IP6_OUTPUT(ret, o_pak, &ro, &ifp, NULL, vrf_id);

		/* Free the route if we got one back */
		if (ro.ro_rt)
			RTFREE(ro.ro_rt);
	}
#endif
	SCTP_STAT_INCR(sctps_sendpackets);
	SCTP_STAT_INCR_COUNTER64(sctps_outpackets);
	SCTP_STAT_INCR_COUNTER64(sctps_outcontrolchunks);
}

static struct mbuf *
sctp_copy_resume(struct sctp_stream_queue_pending *sp,
		 struct uio *uio,
		 struct sctp_sndrcvinfo *srcv,
		 int max_send_len,
		 int user_marks_eor,
		 int *error,
		 uint32_t *sndout,
		 struct mbuf **new_tail)
{
#if defined(__Panda__)
	struct mbuf *m;
	m = m_uiotombuf(uio, M_WAITOK, max_send_len, 0,
			(user_marks_eor ? M_EOR : 0));
	if (m == NULL) {
		SCTP_LTRACE_ERR_RET(NULL, NULL, NULL, SCTP_FROM_SCTP_OUTPUT, ENOMEM);
		*error = ENOMEM;
	}
	else {
		*sndout = m_length(m, NULL);
		*new_tail = m_last(m);
	}
	return (m);
#elif defined(__FreeBSD__) && __FreeBSD_version > 602000
	struct mbuf *m;
	m = m_uiotombuf(uio, M_WAITOK, max_send_len, 0,
		(M_PKTHDR | (user_marks_eor ? M_EOR : 0)));
	if (m == NULL) {
		SCTP_LTRACE_ERR_RET(NULL, NULL, NULL, SCTP_FROM_SCTP_OUTPUT, ENOMEM);
		*error = ENOMEM;
	}
	else {
		*sndout = m_length(m, NULL);
		*new_tail = m_last(m);
	}
	return (m);
#else
	int left, cancpy, willcpy;
	struct mbuf *m,*prev, *head;

#if defined(__APPLE__)
#if defined(APPLE_SNOWLEOPARD) || defined(APPLE_LION)
        left = min(uio_resid(uio), max_send_len);
#else
        left = min(uio->uio_resid, max_send_len);
#endif
#else
        left = min(uio->uio_resid, max_send_len);
#endif
	/* Always get a header just in case */
	head = sctp_get_mbuf_for_msg(left, 0, M_WAIT, 0, MT_DATA);
	if (head == NULL) {
		SCTP_LTRACE_ERR_RET(NULL, NULL, NULL, SCTP_FROM_SCTP_OUTPUT, ENOMEM);
		*error = ENOMEM;
		return (NULL);
	}
	cancpy = M_TRAILINGSPACE(head);
	willcpy = min(cancpy, left);
	*error = uiomove(mtod(head, caddr_t), willcpy, uio);
	if (*error) {
		sctp_m_freem(head);
		return(NULL);
	}
	*sndout += willcpy;
	left -= willcpy;
	SCTP_BUF_LEN(head) = willcpy;
	m = head;
	*new_tail = head;
	while (left > 0) {
		/* move in user data */
		SCTP_BUF_NEXT(m) = sctp_get_mbuf_for_msg(left, 0, M_WAIT, 0, MT_DATA);
		if (SCTP_BUF_NEXT(m) == NULL) {
			sctp_m_freem(head);
			*new_tail = NULL;
			SCTP_LTRACE_ERR_RET(NULL, NULL, NULL, SCTP_FROM_SCTP_OUTPUT, ENOMEM);
			*error = ENOMEM;
			return(NULL);
		}
		prev = m;
		m = SCTP_BUF_NEXT(m);
		cancpy = M_TRAILINGSPACE(m);
		willcpy = min(cancpy, left);
		*error = uiomove(mtod(m, caddr_t), willcpy, uio);
		if (*error) {
			sctp_m_freem(head);
			*new_tail = NULL;
			SCTP_LTRACE_ERR_RET(NULL, NULL, NULL, SCTP_FROM_SCTP_OUTPUT, EFAULT);
			*error = EFAULT;
			return (NULL);
		}
		SCTP_BUF_LEN(m) = willcpy;
		left -= willcpy;
		*sndout += willcpy;
		*new_tail = m;
		if (left == 0) {
			SCTP_BUF_NEXT(m) = NULL;
		}
	}
	return(head);
#endif
}

static int
sctp_copy_one(struct sctp_stream_queue_pending *sp,
	      struct uio *uio,
	      int resv_upfront)
{
	int left;
#if defined(__Panda__)
	left = sp->length;
	sp->data = m_uiotombuf(uio, M_WAITOK, sp->length,
			       resv_upfront, 0);
	if (sp->data == NULL) {
		SCTP_LTRACE_ERR_RET(NULL, NULL, NULL, SCTP_FROM_SCTP_OUTPUT, ENOMEM);
		return (ENOMEM);
	}

	sp->tail_mbuf = m_last(sp->data);
	return (0);

#elif defined(__FreeBSD__) && __FreeBSD_version > 602000
	left = sp->length;
	sp->data = m_uiotombuf(uio, M_WAITOK, sp->length,
			       resv_upfront, 0);
	if (sp->data == NULL) {
		SCTP_LTRACE_ERR_RET(NULL, NULL, NULL, SCTP_FROM_SCTP_OUTPUT, ENOMEM);
		return (ENOMEM);
	}

	sp->tail_mbuf = m_last(sp->data);
	return (0);
#else
	int cancpy, willcpy, error;
	struct mbuf *m, *head;
	int cpsz=0;

	/* First one gets a header */
	left = sp->length;
	head = m = sctp_get_mbuf_for_msg((left + resv_upfront), 0, M_WAIT, 0, MT_DATA);
	if (m == NULL) {
		SCTP_LTRACE_ERR_RET(NULL, NULL, NULL, SCTP_FROM_SCTP_OUTPUT, ENOMEM);
		return (ENOMEM);
	}
	/*-
	 * Add this one for m in now, that way if the alloc fails we won't
	 * have a bad cnt.
	 */
	SCTP_BUF_RESV_UF(m, resv_upfront);
	cancpy = M_TRAILINGSPACE(m);
	willcpy = min(cancpy, left);
	while (left > 0) {
		/* move in user data */
		error = uiomove(mtod(m, caddr_t), willcpy, uio);
		if (error) {
			sctp_m_freem(head);
			return (error);
		}
		SCTP_BUF_LEN(m) = willcpy;
		left -= willcpy;
		cpsz += willcpy;
		if (left > 0) {
			SCTP_BUF_NEXT(m) = sctp_get_mbuf_for_msg(left, 0, M_WAIT, 0, MT_DATA);
			if (SCTP_BUF_NEXT(m) == NULL) {
				/*
				 * the head goes back to caller, he can free
				 * the rest
				 */
				sctp_m_freem(head);
				SCTP_LTRACE_ERR_RET(NULL, NULL, NULL, SCTP_FROM_SCTP_OUTPUT, ENOMEM);
				return (ENOMEM);
			}
			m = SCTP_BUF_NEXT(m);
			cancpy = M_TRAILINGSPACE(m);
			willcpy = min(cancpy, left);
		} else {
			sp->tail_mbuf = m;
			SCTP_BUF_NEXT(m) = NULL;
		}
	}
	sp->data = head;
	sp->length = cpsz;
	return (0);
#endif
}



static struct sctp_stream_queue_pending *
sctp_copy_it_in(struct sctp_tcb *stcb,
    struct sctp_association *asoc,
    struct sctp_sndrcvinfo *srcv,
    struct uio *uio,
    struct sctp_nets *net,
    int max_send_len,
    int user_marks_eor,
    int *error,
    int non_blocking)

{
	/*-
	 * This routine must be very careful in its work. Protocol
	 * processing is up and running so care must be taken to spl...()
	 * when you need to do something that may effect the stcb/asoc. The
	 * sb is locked however. When data is copied the protocol processing
	 * should be enabled since this is a slower operation...
	 */
	struct sctp_stream_queue_pending *sp = NULL;
	int resv_in_first;

	*error = 0;
	/* Now can we send this? */
	if ((SCTP_GET_STATE(asoc) == SCTP_STATE_SHUTDOWN_SENT) ||
	    (SCTP_GET_STATE(asoc) == SCTP_STATE_SHUTDOWN_ACK_SENT) ||
	    (SCTP_GET_STATE(asoc) == SCTP_STATE_SHUTDOWN_RECEIVED) ||
	    (asoc->state & SCTP_STATE_SHUTDOWN_PENDING)) {
		/* got data while shutting down */
		SCTP_LTRACE_ERR_RET(NULL, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, ECONNRESET);
		*error = ECONNRESET;
		goto out_now;
	}
	sctp_alloc_a_strmoq(stcb, sp);
	if (sp == NULL) {
		SCTP_LTRACE_ERR_RET(NULL, stcb, net, SCTP_FROM_SCTP_OUTPUT, ENOMEM);
		*error = ENOMEM;
		goto out_now;
	}
	sp->act_flags = 0;
	sp->sender_all_done = 0;
	sp->sinfo_flags = srcv->sinfo_flags;
	sp->timetolive = srcv->sinfo_timetolive;
	sp->ppid = srcv->sinfo_ppid;
	sp->context = srcv->sinfo_context;
	sp->strseq = 0;
	(void)SCTP_GETTIME_TIMEVAL(&sp->ts);

	sp->stream = srcv->sinfo_stream;
#if defined(__APPLE__)
#if defined(APPLE_SNOWLEOPARD) || defined(APPLE_LION)
	sp->length = min(uio_resid(uio), max_send_len);
#else
	sp->length = min(uio->uio_resid, max_send_len);
#endif
#else
	sp->length = min(uio->uio_resid, max_send_len);
#endif
#if defined(__APPLE__)
#if defined(APPLE_SNOWLEOPARD) || defined(APPLE_LION)
	if ((sp->length == (uint32_t)uio_resid(uio)) &&
#else
	if ((sp->length == (uint32_t)uio->uio_resid) &&
#endif
#else
	if ((sp->length == (uint32_t)uio->uio_resid) &&
#endif
	    ((user_marks_eor == 0) ||
	     (srcv->sinfo_flags & SCTP_EOF) ||
	     (user_marks_eor && (srcv->sinfo_flags & SCTP_EOR)))) {
		sp->msg_is_complete = 1;
	} else {
		sp->msg_is_complete = 0;
	}
	sp->sender_all_done = 0;
	sp->some_taken = 0;
	sp->put_last_out = 0;
	resv_in_first = sizeof(struct sctp_data_chunk);
	sp->data = sp->tail_mbuf = NULL;
	if (sp->length == 0) {
		*error = 0;
		goto skip_copy;
	}
	if (srcv->sinfo_keynumber_valid) {
		sp->auth_keyid = srcv->sinfo_keynumber;
	} else {
		sp->auth_keyid = stcb->asoc.authinfo.active_keyid;
	}
	if (sctp_auth_is_required_chunk(SCTP_DATA, stcb->asoc.peer_auth_chunks)) {
		sctp_auth_key_acquire(stcb, sp->auth_keyid);
		sp->holds_key_ref = 1;
	}
#if defined(__APPLE__)
	SCTP_SOCKET_UNLOCK(SCTP_INP_SO(stcb->sctp_ep), 0);
#endif
	*error = sctp_copy_one(sp, uio, resv_in_first);
#if defined(__APPLE__)
	SCTP_SOCKET_LOCK(SCTP_INP_SO(stcb->sctp_ep), 0);
#endif
 skip_copy:
	if (*error) {
		sctp_free_a_strmoq(stcb, sp, SCTP_SO_LOCKED);
		sp = NULL;
	} else {
		if (sp->sinfo_flags & SCTP_ADDR_OVER) {
			sp->net = net;
			atomic_add_int(&sp->net->ref_count, 1);
		} else {
			sp->net = NULL;
		}
		sctp_set_prsctp_policy(sp);
	}
out_now:
	return (sp);
}


int
sctp_sosend(struct socket *so,
            struct sockaddr *addr,
            struct uio *uio,
#ifdef __Panda__
            pakhandle_type top,
            pakhandle_type icontrol,
#else
            struct mbuf *top,
            struct mbuf *control,
#endif
#if defined(__APPLE__) || defined(__Panda__)
            int flags
#else
            int flags,
#if defined(__FreeBSD__) && __FreeBSD_version >= 500000
            struct thread *p
#elif defined(__Windows__)
            PKTHREAD p
#else
#if defined(__Userspace__)
            /*
	     * proc is a dummy in __Userspace__ and will not be passed
	     * to sctp_lower_sosend
	     */
#endif
            struct proc *p
#endif
#endif
)
{
#ifdef __Panda__
	struct mbuf *control=NULL;
#endif
#if defined(__APPLE__)
	struct proc *p = current_proc();
#endif
	int error, use_sndinfo = 0;
	struct sctp_sndrcvinfo sndrcvninfo;
	struct sockaddr *addr_to_use;
#if defined(INET) && defined(INET6)
	struct sockaddr_in sin;
#endif

#if defined(__APPLE__)
	SCTP_SOCKET_LOCK(so, 1);
#endif
#ifdef __Panda__
	control = SCTP_HEADER_TO_CHAIN(icontrol);
#endif
	if (control) {
		/* process cmsg snd/rcv info (maybe a assoc-id) */
		if (sctp_find_cmsg(SCTP_SNDRCV, (void *)&sndrcvninfo, control,
		    sizeof(sndrcvninfo))) {
			/* got one */
			use_sndinfo = 1;
		}
	}
	addr_to_use = addr;
#if defined(__Userspace__)
	/* TODO port in6_sin6_2_sin */
#else
#if defined(INET) && defined(INET6)
	if ((addr) && (addr->sa_family == AF_INET6)) {
		struct sockaddr_in6 *sin6;

		sin6 = (struct sockaddr_in6 *)addr;
		if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
			in6_sin6_2_sin(&sin, sin6);
			addr_to_use = (struct sockaddr *)&sin;
		}
	}
#endif
#endif
	error = sctp_lower_sosend(so, addr_to_use, uio, top,
#ifdef __Panda__
				  icontrol,
#else
				  control,
#endif
				  flags,
				  use_sndinfo ? &sndrcvninfo: NULL
#if !( defined(__Panda__) || defined(__Userspace__) )
				  , p
#endif
		);
#if defined(__APPLE__)
	SCTP_SOCKET_UNLOCK(so, 1);
#endif
	return (error);
}


int
sctp_lower_sosend(struct socket *so,
                  struct sockaddr *addr,
                  struct uio *uio,
#ifdef __Panda__
                  pakhandle_type i_pak,
                  pakhandle_type i_control,
#else
                  struct mbuf *i_pak,
                  struct mbuf *control,
#endif
                  int flags,
                  struct sctp_sndrcvinfo *srcv
#if !(defined( __Panda__) || defined(__Userspace__))
                  ,
#if defined(__FreeBSD__) && __FreeBSD_version >= 500000
                  struct thread *p
#elif defined(__Windows__)
                  PKTHREAD p
#else
                  struct proc *p
#endif
#endif
	)
{
	unsigned int sndlen = 0, max_len;
	int error, len;
	struct mbuf *top = NULL;
#ifdef __Panda__
	struct mbuf *control = NULL;
#endif
	int queue_only = 0, queue_only_for_init = 0;
	int free_cnt_applied = 0;
	int un_sent;
	int now_filled = 0;
	unsigned int inqueue_bytes = 0;
	struct sctp_block_entry be;
	struct sctp_inpcb *inp;
	struct sctp_tcb *stcb = NULL;
	struct timeval now;
	struct sctp_nets *net;
	struct sctp_association *asoc;
	struct sctp_inpcb *t_inp;
	int user_marks_eor;
	int create_lock_applied = 0;
	int nagle_applies = 0;
	int some_on_control = 0;
	int got_all_of_the_send = 0;
	int hold_tcblock = 0;
	int non_blocking = 0;
	uint32_t local_add_more, local_soresv = 0;
	uint16_t port;
	uint16_t sinfo_flags;
	sctp_assoc_t sinfo_assoc_id;

	error = 0;
	net = NULL;
	stcb = NULL;
	asoc = NULL;
	
#if defined(__APPLE__)
	sctp_lock_assert(so);
#endif
	t_inp = inp = (struct sctp_inpcb *)so->so_pcb;
	if (inp == NULL) {
		SCTP_LTRACE_ERR_RET(NULL, NULL, NULL, SCTP_FROM_SCTP_OUTPUT, EINVAL);
		error = EINVAL;
		if (i_pak) {
			SCTP_RELEASE_PKT(i_pak);
		}
		return (error);
	}
	if ((uio == NULL) && (i_pak == NULL)) {
		SCTP_LTRACE_ERR_RET(inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, EINVAL);
		return (EINVAL);
	}
	user_marks_eor = sctp_is_feature_on(inp, SCTP_PCB_FLAGS_EXPLICIT_EOR);
	atomic_add_int(&inp->total_sends, 1);
	if (uio) {
#if defined(__APPLE__)
#if defined(APPLE_SNOWLEOPARD) || defined(APPLE_LION)
		if (uio_resid(uio) < 0) {
#else
		if (uio->uio_resid < 0) {
#endif
#else
		if (uio->uio_resid < 0) {
#endif
			SCTP_LTRACE_ERR_RET(inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, EINVAL);
			return (EINVAL);
		}
#if defined(__APPLE__)
#if defined(APPLE_SNOWLEOPARD) || defined(APPLE_LION)
		sndlen = uio_resid(uio);
#else
		sndlen = uio->uio_resid;
#endif
#else
		sndlen = uio->uio_resid;
#endif
	} else {
		top = SCTP_HEADER_TO_CHAIN(i_pak);
#ifdef __Panda__
		/*-
		 * app len indicates the datalen, dgsize for cases
		 * of SCTP_EOF/ABORT will not have the right len
		 */
		sndlen = SCTP_APP_DATA_LEN(i_pak);
		/*-
		 * Set the particle len also to zero to match
		 * up with app len. We only have one particle
		 * if app len is zero for Panda. This is ensured
		 * in the socket lib
		 */
		if (sndlen == 0) {
			SCTP_BUF_LEN(top)  = 0;
		}
		/*-
		 * We delink the chain from header, but keep
		 * the header around as we will need it in
		 * EAGAIN case
		 */
		SCTP_DETACH_HEADER_FROM_CHAIN(i_pak);
#else
		sndlen = SCTP_HEADER_LEN(i_pak);
#endif
	}
	SCTPDBG(SCTP_DEBUG_OUTPUT1, "Send called addr:%p send length %d\n",
		addr,
	        sndlen);
#ifdef __Panda__
	if (i_control) {
		control = SCTP_HEADER_TO_CHAIN(i_control);
	}
#endif
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) &&
	    (inp->sctp_socket->so_qlimit)) {
		/* The listener can NOT send */
		SCTP_LTRACE_ERR_RET(NULL, NULL, NULL, SCTP_FROM_SCTP_OUTPUT, ENOTCONN);
		error = ENOTCONN;
		goto out_unlocked;
	}	
	/**
	 * Pre-screen address, if one is given the sin-len
	 * must be set correctly!
	 */
	if (addr) {
		union sctp_sockstore *raddr = (union sctp_sockstore *)addr;
		switch (raddr->sa.sa_family) {
#if defined(INET)
		case AF_INET:
#if defined(__FreeBSD__) || defined(__APPLE__)
			if (raddr->sin.sin_len != sizeof(struct sockaddr_in)) {
				SCTP_LTRACE_ERR_RET(inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, EINVAL);
				error = EINVAL;
				goto out_unlocked;
			}
#endif
			port = raddr->sin.sin_port;
			break;
#endif
#if defined(INET6)
		case AF_INET6:
#if defined(__FreeBSD__) || defined(__APPLE__)
			if (raddr->sin6.sin6_len != sizeof(struct sockaddr_in6)) {
				SCTP_LTRACE_ERR_RET(inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, EINVAL);
				error = EINVAL;
				goto out_unlocked;
			}
#endif
			port = raddr->sin6.sin6_port;
			break;
#endif
		default:
			SCTP_LTRACE_ERR_RET(inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, EAFNOSUPPORT);
			error = EAFNOSUPPORT;
			goto out_unlocked;
		}
	} else
		port = 0;

	if (srcv) {
		sinfo_flags = srcv->sinfo_flags;
		sinfo_assoc_id = srcv->sinfo_assoc_id;
		if (INVALID_SINFO_FLAG(sinfo_flags) ||
		    PR_SCTP_INVALID_POLICY(sinfo_flags)) {
			SCTP_LTRACE_ERR_RET(inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, EINVAL);
			error = EINVAL;
			goto out_unlocked;
		}
		if (srcv->sinfo_flags)
			SCTP_STAT_INCR(sctps_sends_with_flags);
	} else {
		sinfo_flags = inp->def_send.sinfo_flags;
		sinfo_assoc_id = inp->def_send.sinfo_assoc_id;
	}
	if (sinfo_flags & SCTP_SENDALL) {
		/* its a sendall */
		error = sctp_sendall(inp, uio, top, srcv);
		top = NULL;
		goto out_unlocked;
	}
	if ((sinfo_flags & SCTP_ADDR_OVER) && (addr == NULL)) {
		SCTP_LTRACE_ERR_RET(inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, EINVAL);
		error = EINVAL;
		goto out_unlocked;
	}
	/* now we must find the assoc */
	if ((inp->sctp_flags & SCTP_PCB_FLAGS_CONNECTED) ||
	    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL)) {
		SCTP_INP_RLOCK(inp);
		stcb = LIST_FIRST(&inp->sctp_asoc_list);
		if (stcb == NULL) {
			SCTP_INP_RUNLOCK(inp);
			SCTP_LTRACE_ERR_RET(inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, ENOTCONN);
			error = ENOTCONN;
			goto out_unlocked;
		}
		SCTP_TCB_LOCK(stcb);
		hold_tcblock = 1;
		SCTP_INP_RUNLOCK(inp);
	} else if (sinfo_assoc_id) {
		stcb = sctp_findassociation_ep_asocid(inp, sinfo_assoc_id, 0);
	} else if (addr) {
		/*-
		 * Since we did not use findep we must
		 * increment it, and if we don't find a tcb
		 * decrement it.
		 */
		SCTP_INP_WLOCK(inp);
		SCTP_INP_INCR_REF(inp);
		SCTP_INP_WUNLOCK(inp);
		stcb = sctp_findassociation_ep_addr(&t_inp, addr, &net, NULL, NULL);
		if (stcb == NULL) {
			SCTP_INP_WLOCK(inp);
			SCTP_INP_DECR_REF(inp);
			SCTP_INP_WUNLOCK(inp);
		} else {
			hold_tcblock = 1;
		}
	}
	if ((stcb == NULL) && (addr)) {
		/* Possible implicit send? */
		SCTP_ASOC_CREATE_LOCK(inp);
		create_lock_applied = 1;
		if ((inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_GONE) ||
		    (inp->sctp_flags & SCTP_PCB_FLAGS_SOCKET_ALLGONE)) {
			/* Should I really unlock ? */
			SCTP_LTRACE_ERR_RET(NULL, NULL, NULL, SCTP_FROM_SCTP_OUTPUT, EINVAL);
			error = EINVAL;
			goto out_unlocked;

		}
		if (((inp->sctp_flags & SCTP_PCB_FLAGS_BOUND_V6) == 0) &&
		    (addr->sa_family == AF_INET6)) {
			SCTP_LTRACE_ERR_RET(inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, EINVAL);
			error = EINVAL;
			goto out_unlocked;
		}
		SCTP_INP_WLOCK(inp);
		SCTP_INP_INCR_REF(inp);
		SCTP_INP_WUNLOCK(inp);
		/* With the lock applied look again */
		stcb = sctp_findassociation_ep_addr(&t_inp, addr, &net, NULL, NULL);
		if ((stcb == NULL) && (control != NULL) && (port > 0)) {
			stcb = sctp_findassociation_cmsgs(&t_inp, port, control, &net, &error);
		}
		if (stcb == NULL) {
			SCTP_INP_WLOCK(inp);
			SCTP_INP_DECR_REF(inp);
			SCTP_INP_WUNLOCK(inp);
		} else {
			hold_tcblock = 1;
		}
		if (error) {
			goto out_unlocked;
		}
		if (t_inp != inp) {
			SCTP_LTRACE_ERR_RET(inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, ENOTCONN);
			error = ENOTCONN;
			goto out_unlocked;
		}
	}
	if (stcb == NULL) {
		if ((inp->sctp_flags & SCTP_PCB_FLAGS_TCPTYPE) ||
		    (inp->sctp_flags & SCTP_PCB_FLAGS_IN_TCPPOOL)) {
			SCTP_LTRACE_ERR_RET(inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, ENOTCONN);
			error = ENOTCONN;
			goto out_unlocked;
		}
		if (addr == NULL) {
			SCTP_LTRACE_ERR_RET(inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, ENOENT);
			error = ENOENT;
			goto out_unlocked;
		} else {
			/* UDP style, we must go ahead and start the INIT process */
			uint32_t vrf_id;

			if ((sinfo_flags & SCTP_ABORT) ||
			    ((sinfo_flags & SCTP_EOF) && (sndlen == 0))) {
				/*-
				 * User asks to abort a non-existant assoc,
				 * or EOF a non-existant assoc with no data
				 */
				SCTP_LTRACE_ERR_RET(inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, ENOENT);
				error = ENOENT;
				goto out_unlocked;
			}
			/* get an asoc/stcb struct */
			vrf_id = inp->def_vrf_id;
#ifdef INVARIANTS
			if (create_lock_applied == 0) {
				panic("Error, should hold create lock and I don't?");
			}
#endif
			stcb = sctp_aloc_assoc(inp, addr, &error, 0, vrf_id,
#if !( defined( __Panda__) || defined(__Userspace__) )
					       p
#else
					       (struct proc *)NULL
#endif
				);
			if (stcb == NULL) {
				/* Error is setup for us in the call */
				goto out_unlocked;
			}
			hold_tcblock = 1;
			if (create_lock_applied) {
				SCTP_ASOC_CREATE_UNLOCK(inp);
				create_lock_applied = 0;
			} else {
				SCTP_PRINTF("Huh-3? create lock should have been on??\n");
			}
			/* Turn on queue only flag to prevent data from being sent */
			queue_only = 1;
			asoc = &stcb->asoc;
			SCTP_SET_STATE(asoc, SCTP_STATE_COOKIE_WAIT);
			(void)SCTP_GETTIME_TIMEVAL(&asoc->time_entered);

			/* initialize authentication params for the assoc */
			sctp_initialize_auth_params(inp, stcb);

			if (control) {
				if (sctp_process_cmsgs_for_init(stcb, control, &error)) {
					sctp_free_assoc(inp, stcb, SCTP_PCBFREE_FORCE, SCTP_FROM_SCTP_OUTPUT + SCTP_LOC_7);
					hold_tcblock = 0;
					stcb = NULL;
					goto out_unlocked;
				}
			}
			/* out with the INIT */
			queue_only_for_init = 1;
			/*-
			 * we may want to dig in after this call and adjust the MTU
			 * value. It defaulted to 1500 (constant) but the ro
			 * structure may now have an update and thus we may need to
			 * change it BEFORE we append the message.
			 */
		}
	} else
		asoc = &stcb->asoc;
	if (srcv == NULL)
		srcv = (struct sctp_sndrcvinfo *)&asoc->def_send;
	if (srcv->sinfo_flags & SCTP_ADDR_OVER) {
		if (addr)
			net = sctp_findnet(stcb, addr);
		else
			net = NULL;
		if ((net == NULL) ||
		    ((port != 0) && (port != stcb->rport))) {
			SCTP_LTRACE_ERR_RET(inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, EINVAL);
			error = EINVAL;
			goto out_unlocked;
		}
	} else {
		if (stcb->asoc.alternate) {
			net = stcb->asoc.alternate;
		} else {
			net = stcb->asoc.primary_destination;
		}
	}
	atomic_add_int(&stcb->total_sends, 1);
	/* Keep the stcb from being freed under our feet */
	atomic_add_int(&asoc->refcnt, 1);
	free_cnt_applied = 1;

	if (sctp_is_feature_on(inp, SCTP_PCB_FLAGS_NO_FRAGMENT)) {
		if (sndlen > asoc->smallest_mtu) {
			SCTP_LTRACE_ERR_RET(inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, EMSGSIZE);
			error = EMSGSIZE;
			goto out_unlocked;
		}
	}
	if ((SCTP_SO_IS_NBIO(so)
#if defined(__FreeBSD__) && __FreeBSD_version >= 500000
	     || (flags & MSG_NBIO)
#endif
		    )) {
		non_blocking = 1;
	}
	/* would we block? */
	if (non_blocking) {
		if (hold_tcblock == 0) {
			SCTP_TCB_LOCK(stcb);
			hold_tcblock = 1;
		}
		inqueue_bytes = stcb->asoc.total_output_queue_size - (stcb->asoc.chunks_on_out_queue * sizeof(struct sctp_data_chunk));
		if ((SCTP_SB_LIMIT_SND(so) <  (sndlen + inqueue_bytes + stcb->asoc.sb_send_resv)) ||
		    (stcb->asoc.chunks_on_out_queue >= SCTP_BASE_SYSCTL(sctp_max_chunks_on_queue))) {
			SCTP_LTRACE_ERR_RET(inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, EWOULDBLOCK);
			if (sndlen > SCTP_SB_LIMIT_SND(so))
				error = EMSGSIZE;
			else
				error = EWOULDBLOCK;
			goto out_unlocked;
		}
		stcb->asoc.sb_send_resv += sndlen;
		SCTP_TCB_UNLOCK(stcb);
		hold_tcblock = 0;
	} else {
		atomic_add_int(&stcb->asoc.sb_send_resv, sndlen);
	}
	local_soresv = sndlen;
	if (stcb->asoc.state & SCTP_STATE_ABOUT_TO_BE_FREED) {
		SCTP_LTRACE_ERR_RET(NULL, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, ECONNRESET);
		error = ECONNRESET;
		goto out_unlocked;
	}
	if (create_lock_applied) {
		SCTP_ASOC_CREATE_UNLOCK(inp);
		create_lock_applied = 0;
	}
	if (asoc->stream_reset_outstanding) {
		/*
		 * Can't queue any data while stream reset is underway.
		 */
		SCTP_LTRACE_ERR_RET(inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, EAGAIN);
		error = EAGAIN;
		goto out_unlocked;
	}
	if ((SCTP_GET_STATE(asoc) == SCTP_STATE_COOKIE_WAIT) ||
	    (SCTP_GET_STATE(asoc) == SCTP_STATE_COOKIE_ECHOED)) {
		queue_only = 1;
	}
	/* we are now done with all control */
	if (control) {
		sctp_m_freem(control);
		control = NULL;
	}
	if ((SCTP_GET_STATE(asoc) == SCTP_STATE_SHUTDOWN_SENT) ||
	    (SCTP_GET_STATE(asoc) == SCTP_STATE_SHUTDOWN_RECEIVED) ||
	    (SCTP_GET_STATE(asoc) == SCTP_STATE_SHUTDOWN_ACK_SENT) ||
	    (asoc->state & SCTP_STATE_SHUTDOWN_PENDING)) {
		if (srcv->sinfo_flags & SCTP_ABORT) {
			;
		} else {
			SCTP_LTRACE_ERR_RET(NULL, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, ECONNRESET);
			error = ECONNRESET;
			goto out_unlocked;
		}
	}
	/* Ok, we will attempt a msgsnd :> */
#if !(defined(__Panda__) || defined(__Windows__) || defined(__Userspace__))
	if (p) {
#if defined(__FreeBSD__) && __FreeBSD_version >= 603000
		p->td_ru.ru_msgsnd++;
#elif defined(__FreeBSD__) && __FreeBSD_version >= 500000
		p->td_proc->p_stats->p_ru.ru_msgsnd++;
#else
		p->p_stats->p_ru.ru_msgsnd++;
#endif
	}
#endif
	/* Are we aborting? */
	if (srcv->sinfo_flags & SCTP_ABORT) {
		struct mbuf *mm;
		int tot_demand, tot_out=0, max_out;

		SCTP_STAT_INCR(sctps_sends_with_abort);
		if ((SCTP_GET_STATE(asoc) == SCTP_STATE_COOKIE_WAIT) ||
		    (SCTP_GET_STATE(asoc) == SCTP_STATE_COOKIE_ECHOED)) {
			/* It has to be up before we abort */
			/* how big is the user initiated abort? */
			SCTP_LTRACE_ERR_RET(inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, EINVAL);
			error = EINVAL;
			goto out;
		}
		if (hold_tcblock) {
			SCTP_TCB_UNLOCK(stcb);
			hold_tcblock = 0;
		}
		if (top) {
			struct mbuf *cntm = NULL;
			mm = sctp_get_mbuf_for_msg(1, 0, M_WAIT, 1, MT_DATA);
			if (sndlen != 0) {
				cntm = top;
				while(cntm) {
					tot_out += SCTP_BUF_LEN(cntm);
					cntm = SCTP_BUF_NEXT(cntm);
				}
			}
			tot_demand = (tot_out + sizeof(struct sctp_paramhdr));
		} else {
			/* Must fit in a MTU */
			tot_out = sndlen;
			tot_demand = (tot_out + sizeof(struct sctp_paramhdr));
			if (tot_demand > SCTP_DEFAULT_ADD_MORE) {
				/* To big */
				SCTP_LTRACE_ERR_RET(NULL, stcb, net, SCTP_FROM_SCTP_OUTPUT, EMSGSIZE);
				error = EMSGSIZE;
				goto out;
			}
			mm = sctp_get_mbuf_for_msg(tot_demand, 0, M_WAIT, 1, MT_DATA);
		}
		if (mm == NULL) {
			SCTP_LTRACE_ERR_RET(NULL, stcb, net, SCTP_FROM_SCTP_OUTPUT, ENOMEM);
			error = ENOMEM;
			goto out;
		}
		max_out = asoc->smallest_mtu - sizeof(struct sctp_paramhdr);
		max_out -= sizeof(struct sctp_abort_msg);
		if (tot_out > max_out) {
			tot_out = max_out;
		}
		if (mm) {
			struct sctp_paramhdr *ph;
			/* now move forward the data pointer */
			ph = mtod(mm, struct sctp_paramhdr *);
			ph->param_type = htons(SCTP_CAUSE_USER_INITIATED_ABT);
			ph->param_length = htons((sizeof(struct sctp_paramhdr) + tot_out));
			ph++;
			SCTP_BUF_LEN(mm) = tot_out + sizeof(struct sctp_paramhdr);
			if (top == NULL) {
#if defined(__APPLE__)
				SCTP_SOCKET_UNLOCK(so, 0);
#endif
				error = uiomove((caddr_t)ph, (int)tot_out, uio);
#if defined(__APPLE__)
				SCTP_SOCKET_LOCK(so, 0);
#endif
				if (error) {
					/*-
					 * Here if we can't get his data we
					 * still abort we just don't get to
					 * send the users note :-0
					 */
					sctp_m_freem(mm);
					mm = NULL;
				}
			} else {
				if (sndlen != 0) {
					SCTP_BUF_NEXT(mm) = top;
				}
			}
		}
		if (hold_tcblock == 0) {
			SCTP_TCB_LOCK(stcb);
			hold_tcblock = 1;
		}
		atomic_add_int(&stcb->asoc.refcnt, -1);
		free_cnt_applied = 0;
		/* release this lock, otherwise we hang on ourselves */
		sctp_abort_an_association(stcb->sctp_ep, stcb,
		                          SCTP_RESPONSE_TO_USER_REQ,
		                          mm, SCTP_SO_LOCKED);
		/* now relock the stcb so everything is sane */
		hold_tcblock = 0;
		stcb = NULL;
		/* In this case top is already chained to mm
		 * avoid double free, since we free it below if
		 * top != NULL and driver would free it after sending
		 * the packet out
		 */
		if (sndlen != 0) {
			top = NULL;
		}
		goto out_unlocked;
	}
	/* Calculate the maximum we can send */
	inqueue_bytes = stcb->asoc.total_output_queue_size - (stcb->asoc.chunks_on_out_queue * sizeof(struct sctp_data_chunk));
	if (SCTP_SB_LIMIT_SND(so) > inqueue_bytes) {
		if (non_blocking) {
			/* we already checked for non-blocking above. */
			max_len = sndlen;
		} else {
			max_len = SCTP_SB_LIMIT_SND(so) - inqueue_bytes;
		}
	} else {
		max_len = 0;
	}
	if (hold_tcblock) {
		SCTP_TCB_UNLOCK(stcb);
		hold_tcblock = 0;
	}
	/* Is the stream no. valid? */
	if (srcv->sinfo_stream >= asoc->streamoutcnt) {
		/* Invalid stream number */
		SCTP_LTRACE_ERR_RET(inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, EINVAL);
		error = EINVAL;
		goto out_unlocked;
	}
	if (asoc->strmout == NULL) {
		/* huh? software error */
		SCTP_LTRACE_ERR_RET(inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, EFAULT);
		error = EFAULT;
		goto out_unlocked;
	}

	/* Unless E_EOR mode is on, we must make a send FIT in one call. */
	if ((user_marks_eor == 0) &&
	    (sndlen > SCTP_SB_LIMIT_SND(stcb->sctp_socket))) {
		/* It will NEVER fit */
		SCTP_LTRACE_ERR_RET(NULL, stcb, net, SCTP_FROM_SCTP_OUTPUT, EMSGSIZE);
		error = EMSGSIZE;
		goto out_unlocked;
	}
	if ((uio == NULL) && user_marks_eor) {
		/*-
		 * We do not support eeor mode for
		 * sending with mbuf chains (like sendfile).
		 */
		SCTP_LTRACE_ERR_RET(NULL, stcb, net, SCTP_FROM_SCTP_OUTPUT, EINVAL);
		error = EINVAL;
		goto out_unlocked;
	}

	if (user_marks_eor) {
		local_add_more = min(SCTP_SB_LIMIT_SND(so), SCTP_BASE_SYSCTL(sctp_add_more_threshold));
	} else {
		/*-
		 * For non-eeor the whole message must fit in
		 * the socket send buffer.
		 */
		local_add_more = sndlen;
	}
	len = 0;
	if (non_blocking) {
		goto skip_preblock;
	}
	if (((max_len <= local_add_more) &&
	     (SCTP_SB_LIMIT_SND(so) >= local_add_more)) ||
	    (max_len == 0) ||
	    ((stcb->asoc.chunks_on_out_queue+stcb->asoc.stream_queue_cnt) >= SCTP_BASE_SYSCTL(sctp_max_chunks_on_queue))) {
		/* No room right now ! */
		SOCKBUF_LOCK(&so->so_snd);
		inqueue_bytes = stcb->asoc.total_output_queue_size - (stcb->asoc.chunks_on_out_queue * sizeof(struct sctp_data_chunk));
		while ((SCTP_SB_LIMIT_SND(so) < (inqueue_bytes + local_add_more)) ||
		       ((stcb->asoc.stream_queue_cnt+stcb->asoc.chunks_on_out_queue) >= SCTP_BASE_SYSCTL(sctp_max_chunks_on_queue))) {
			SCTPDBG(SCTP_DEBUG_OUTPUT1,"pre_block limit:%u <(inq:%d + %d) || (%d+%d > %d)\n",
			        (unsigned int)SCTP_SB_LIMIT_SND(so),
			        inqueue_bytes,
			        local_add_more,
			        stcb->asoc.stream_queue_cnt,
			        stcb->asoc.chunks_on_out_queue,
			        SCTP_BASE_SYSCTL(sctp_max_chunks_on_queue));
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_BLK_LOGGING_ENABLE) {
				sctp_log_block(SCTP_BLOCK_LOG_INTO_BLKA, so, asoc, sndlen);
			}
			be.error = 0;
#if !defined(__Panda__) && !defined(__Windows__)
			stcb->block_entry = &be;
#endif
			error = sbwait(&so->so_snd);
			stcb->block_entry = NULL;
			if (error || so->so_error || be.error) {
				if (error == 0) {
					if (so->so_error)
						error = so->so_error;
					if (be.error) {
						error = be.error;
					}
				}
				SOCKBUF_UNLOCK(&so->so_snd);
				goto out_unlocked;
			}
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_BLK_LOGGING_ENABLE) {
				sctp_log_block(SCTP_BLOCK_LOG_OUTOF_BLK,
				               so, asoc, stcb->asoc.total_output_queue_size);
			}
			if (stcb->asoc.state & SCTP_STATE_ABOUT_TO_BE_FREED) {
				goto out_unlocked;
			}
			inqueue_bytes = stcb->asoc.total_output_queue_size - (stcb->asoc.chunks_on_out_queue * sizeof(struct sctp_data_chunk));
		}
		if (SCTP_SB_LIMIT_SND(so) > inqueue_bytes) {
			max_len = SCTP_SB_LIMIT_SND(so) -  inqueue_bytes;
		} else {
			max_len = 0;
		}
		SOCKBUF_UNLOCK(&so->so_snd);
	}

skip_preblock:
	if (stcb->asoc.state & SCTP_STATE_ABOUT_TO_BE_FREED) {
		goto out_unlocked;
	}
#if defined(__APPLE__)
	error = sblock(&so->so_snd, SBLOCKWAIT(flags));
#endif
	/* sndlen covers for mbuf case
	 * uio_resid covers for the non-mbuf case
	 * NOTE: uio will be null when top/mbuf is passed
	 */
	if (sndlen == 0) {
		if (srcv->sinfo_flags & SCTP_EOF) {
			got_all_of_the_send = 1;
			goto dataless_eof;
		} else {
			SCTP_LTRACE_ERR_RET(inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, EINVAL);
			error = EINVAL;
			goto out;
		}
	}
	if (top == NULL) {
		struct sctp_stream_queue_pending *sp;
		struct sctp_stream_out *strm;
		uint32_t sndout;

		SCTP_TCB_SEND_LOCK(stcb);
		if ((asoc->stream_locked) &&
		    (asoc->stream_locked_on  != srcv->sinfo_stream)) {
			SCTP_TCB_SEND_UNLOCK(stcb);
			SCTP_LTRACE_ERR_RET(inp, stcb, net, SCTP_FROM_SCTP_OUTPUT, EINVAL);
			error = EINVAL;
			goto out;
		}
		SCTP_TCB_SEND_UNLOCK(stcb);

		strm = &stcb->asoc.strmout[srcv->sinfo_stream];
		if (strm->last_msg_incomplete == 0) {
		do_a_copy_in:
			sp = sctp_copy_it_in(stcb, asoc, srcv, uio, net, max_len, user_marks_eor, &error, non_blocking);
			if ((sp == NULL) || (error)) {
				goto out;
			}
			SCTP_TCB_SEND_LOCK(stcb);
			if (sp->msg_is_complete) {
				strm->last_msg_incomplete = 0;
				asoc->stream_locked = 0;
			} else {
				/* Just got locked to this guy in
				 * case of an interrupt.
				 */
				strm->last_msg_incomplete = 1;
				asoc->stream_locked = 1;
				asoc->stream_locked_on  = srcv->sinfo_stream;
				sp->sender_all_done = 0;
			}
			sctp_snd_sb_alloc(stcb, sp->length);
			atomic_add_int(&asoc->stream_queue_cnt, 1);
			if ((srcv->sinfo_flags & SCTP_UNORDERED) == 0) {
				sp->strseq = strm->next_sequence_sent;
				if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_LOG_AT_SEND_2_SCTP) {
					sctp_misc_ints(SCTP_STRMOUT_LOG_ASSIGN,
					               (uintptr_t)stcb, sp->length,
					               (uint32_t)((srcv->sinfo_stream << 16) | sp->strseq), 0);
				}
				strm->next_sequence_sent++;
			} else {
				SCTP_STAT_INCR(sctps_sends_with_unord);
			}
			TAILQ_INSERT_TAIL(&strm->outqueue, sp, next);
			stcb->asoc.ss_functions.sctp_ss_add_to_stream(stcb, asoc, strm, sp, 1);
			SCTP_TCB_SEND_UNLOCK(stcb);
		} else {
			SCTP_TCB_SEND_LOCK(stcb);
			sp = TAILQ_LAST(&strm->outqueue, sctp_streamhead);
			SCTP_TCB_SEND_UNLOCK(stcb);
			if (sp == NULL) {
				/* ???? Huh ??? last msg is gone */
#ifdef INVARIANTS
				panic("Warning: Last msg marked incomplete, yet nothing left?");
#else
				SCTP_PRINTF("Warning: Last msg marked incomplete, yet nothing left?\n");
				strm->last_msg_incomplete = 0;
#endif
				goto do_a_copy_in;

			}
		}
#if defined(__APPLE__)
#if defined(APPLE_SNOWLEOPARD) || defined(APPLE_LION)
		while (uio_resid(uio) > 0) {
#else
		while (uio->uio_resid > 0) {
#endif
#else
		while (uio->uio_resid > 0) {
#endif
			/* How much room do we have? */
			struct mbuf *new_tail, *mm;

			if (SCTP_SB_LIMIT_SND(so) > stcb->asoc.total_output_queue_size)
				max_len = SCTP_SB_LIMIT_SND(so) - stcb->asoc.total_output_queue_size;
			else
				max_len = 0;

			if ((max_len > SCTP_BASE_SYSCTL(sctp_add_more_threshold)) ||
			    (max_len && (SCTP_SB_LIMIT_SND(so) < SCTP_BASE_SYSCTL(sctp_add_more_threshold))) ||
#if defined(__APPLE__)
#if defined(APPLE_SNOWLEOPARD) || defined(APPLE_LION)
			    (uio_resid(uio) && (uio_resid(uio) <= (int)max_len))) {
#else
			    (uio->uio_resid && (uio->uio_resid <= (int)max_len))) {
#endif
#else
			    (uio->uio_resid && (uio->uio_resid <= (int)max_len))) {
#endif
				sndout = 0;
				new_tail = NULL;
				if (hold_tcblock) {
					SCTP_TCB_UNLOCK(stcb);
					hold_tcblock = 0;
				}
#if defined(__APPLE__)
				SCTP_SOCKET_UNLOCK(so, 0);
#endif
				mm = sctp_copy_resume(sp, uio, srcv, max_len, user_marks_eor, &error, &sndout, &new_tail);
#if defined(__APPLE__)
				SCTP_SOCKET_LOCK(so, 0);
#endif
				if ((mm == NULL) || error) {
					if (mm) {
						sctp_m_freem(mm);
					}
					goto out;
				}
				/* Update the mbuf and count */
				SCTP_TCB_SEND_LOCK(stcb);
				if (stcb->asoc.state & SCTP_STATE_ABOUT_TO_BE_FREED) {
					/* we need to get out.
					 * Peer probably aborted.
					 */
					sctp_m_freem(mm);
					if (stcb->asoc.state & SCTP_PCB_FLAGS_WAS_ABORTED) {
						SCTP_LTRACE_ERR_RET(NULL, stcb, NULL, SCTP_FROM_SCTP_OUTPUT, ECONNRESET);
						error = ECONNRESET;
					}
					SCTP_TCB_SEND_UNLOCK(stcb);
					goto out;
				}
				if (sp->tail_mbuf) {
					/* tack it to the end */
					SCTP_BUF_NEXT(sp->tail_mbuf) = mm;
					sp->tail_mbuf = new_tail;
				} else {
					/* A stolen mbuf */
					sp->data = mm;
					sp->tail_mbuf = new_tail;
				}
				sctp_snd_sb_alloc(stcb, sndout);
				atomic_add_int(&sp->length,sndout);
				len += sndout;

				/* Did we reach EOR? */
#if defined(__APPLE__)
#if defined(APPLE_SNOWLEOPARD) || defined(APPLE_LION)
				if ((uio_resid(uio) == 0) &&
#else
				if ((uio->uio_resid == 0) &&
#endif
#else
				if ((uio->uio_resid == 0) &&
#endif
				    ((user_marks_eor == 0) ||
				     (srcv->sinfo_flags & SCTP_EOF) ||
				     (user_marks_eor && (srcv->sinfo_flags & SCTP_EOR)))) {
					sp->msg_is_complete = 1;
				} else {
					sp->msg_is_complete = 0;
				}
				SCTP_TCB_SEND_UNLOCK(stcb);
			}
#if defined(__APPLE__)
#if defined(APPLE_SNOWLEOPARD) || defined(APPLE_LION)
			if (uio_resid(uio) == 0) {
#else
			if (uio->uio_resid == 0) {
#endif
#else
			if (uio->uio_resid == 0) {
#endif
				/* got it all? */
				continue;
			}
			/* PR-SCTP? */
			if ((asoc->peer_supports_prsctp) && (asoc->sent_queue_cnt_removeable > 0)) {
				/* This is ugly but we must assure locking order */
				if (hold_tcblock == 0) {
					SCTP_TCB_LOCK(stcb);
					hold_tcblock = 1;
				}
				sctp_prune_prsctp(stcb, asoc, srcv, sndlen);
				inqueue_bytes = stcb->asoc.total_output_queue_size - (stcb->asoc.chunks_on_out_queue * sizeof(struct sctp_data_chunk));
				if (SCTP_SB_LIMIT_SND(so) > stcb->asoc.total_output_queue_size)
					max_len = SCTP_SB_LIMIT_SND(so) - inqueue_bytes;
				else
					max_len = 0;
				if (max_len > 0) {
					continue;
				}
				SCTP_TCB_UNLOCK(stcb);
				hold_tcblock = 0;
			}
			/* wait for space now */
			if (non_blocking) {
				/* Non-blocking io in place out */
				goto skip_out_eof;
			}
			/* What about the INIT, send it maybe */
			if (queue_only_for_init) {
				if (hold_tcblock == 0) {
					SCTP_TCB_LOCK(stcb);
					hold_tcblock = 1;
				}
				if (SCTP_GET_STATE(&stcb->asoc) == SCTP_STATE_OPEN) {
					/* a collision took us forward? */
					queue_only = 0;
				} else {
					sctp_send_initiate(inp, stcb, SCTP_SO_LOCKED);
					SCTP_SET_STATE(asoc, SCTP_STATE_COOKIE_WAIT);
					queue_only = 1;
				}
			}
			if ((net->flight_size > net->cwnd) &&
			    (asoc->sctp_cmt_on_off == 0)) {
				SCTP_STAT_INCR(sctps_send_cwnd_avoid);
				queue_only = 1;
			} else if (asoc->ifp_had_enobuf) {
				SCTP_STAT_INCR(sctps_ifnomemqueued);
				if (net->flight_size > (2 * net->mtu)) {
					queue_only = 1;
				}
				asoc->ifp_had_enobuf = 0;
			}
			un_sent = ((stcb->asoc.total_output_queue_size - stcb->asoc.total_flight) +
			           (stcb->asoc.stream_queue_cnt * sizeof(struct sctp_data_chunk)));
			if ((sctp_is_feature_off(inp, SCTP_PCB_FLAGS_NODELAY)) &&
			    (stcb->asoc.total_flight > 0) &&
			    (stcb->asoc.stream_queue_cnt < SCTP_MAX_DATA_BUNDLING) &&
			    (un_sent < (int)(stcb->asoc.smallest_mtu - SCTP_MIN_OVERHEAD))) {

				/*-
				 * Ok, Nagle is set on and we have data outstanding.
				 * Don't send anything and let SACKs drive out the
				 * data unless wen have a "full" segment to send.
				 */
				if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_NAGLE_LOGGING_ENABLE) {
					sctp_log_nagle_event(stcb, SCTP_NAGLE_APPLIED);
				}
				SCTP_STAT_INCR(sctps_naglequeued);
				nagle_applies = 1;
			} else {
				if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_NAGLE_LOGGING_ENABLE) {
					if (sctp_is_feature_off(inp, SCTP_PCB_FLAGS_NODELAY))
						sctp_log_nagle_event(stcb, SCTP_NAGLE_SKIPPED);
				}
				SCTP_STAT_INCR(sctps_naglesent);
				nagle_applies = 0;
			}
			if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_BLK_LOGGING_ENABLE) {

				sctp_misc_ints(SCTP_CWNDLOG_PRESEND, queue_only_for_init, queue_only,
					       nagle_applies, un_sent);
				sctp_misc_ints(SCTP_CWNDLOG_PRESEND, stcb->asoc.total_output_queue_size,
					       stcb->asoc.total_flight,
					       stcb->asoc.chunks_on_out_queue, stcb->asoc.total_flight_count);
			}
			if (queue_only_for_init)
				queue_only_for_init = 0;
			if ((queue_only == 0) && (nagle_applies == 0)) {
				/*-
				 * need to start chunk output
				 * before blocking.. note that if
				 * a lock is already applied, then
				 * the input via the net is happening
				 * and I don't need to start output :-D
				 */
				if (hold_tcblock == 0) {
					if (SCTP_TCB_TRYLOCK(stcb)) {
						hold_tcblock = 1;
						sctp_chunk_output(inp,
								  stcb,
								  SCTP_OUTPUT_FROM_USR_SEND, SCTP_SO_LOCKED);
					}
				} else {
					sctp_chunk_output(inp,
							  stcb,
							  SCTP_OUTPUT_FROM_USR_SEND, SCTP_SO_LOCKED);
				}
				if (hold_tcblock == 1) {
					SCTP_TCB_UNLOCK(stcb);
					hold_tcblock = 0;
				}
			}
			SOCKBUF_LOCK(&so->so_snd);
			/*-
			 * This is a bit strange, but I think it will
			 * work. The total_output_queue_size is locked and
			 * protected by the TCB_LOCK, which we just released.
			 * There is a race that can occur between releasing it
			 * above, and me getting the socket lock, where sacks
			 * come in but we have not put the SB_WAIT on the
			 * so_snd buffer to get the wakeup. After the LOCK
			 * is applied the sack_processing will also need to
			 * LOCK the so->so_snd to do the actual sowwakeup(). So
			 * once we have the socket buffer lock if we recheck the
			 * size we KNOW we will get to sleep safely with the
			 * wakeup flag in place.
			 */
			if (SCTP_SB_LIMIT_SND(so) <= (stcb->asoc.total_output_queue_size +
						      min(SCTP_BASE_SYSCTL(sctp_add_more_threshold), SCTP_SB_LIMIT_SND(so)))) {
				if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_BLK_LOGGING_ENABLE) {
#if defined(__APPLE__)
#if defined(APPLE_SNOWLEOPARD) || defined(APPLE_LION)
					sctp_log_block(SCTP_BLOCK_LOG_INTO_BLK,
						       so, asoc, uio_resid(uio));
#else
					sctp_log_block(SCTP_BLOCK_LOG_INTO_BLK,
						       so, asoc, uio->uio_resid);
#endif
#else
					sctp_log_block(SCTP_BLOCK_LOG_INTO_BLK,
						       so, asoc, uio->uio_resid);
#endif
				}
				be.error = 0;
#if !defined(__Panda__) && !defined(__Windows__)
				stcb->block_entry = &be;
#endif
#if defined(__APPLE__)
				sbunlock(&so->so_snd, 1);
#endif
				error = sbwait(&so->so_snd);
				stcb->block_entry = NULL;

				if (error || so->so_error || be.error) {
					if (error == 0) {
						if (so->so_error)
							error = so->so_error;
						if (be.error) {
							error = be.error;
						}
					}
					SOCKBUF_UNLOCK(&so->so_snd);
					goto out_unlocked;
				}

#if defined(__APPLE__)
				error = sblock(&so->so_snd, SBLOCKWAIT(flags));
#endif
				if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_BLK_LOGGING_ENABLE) {
					sctp_log_block(SCTP_BLOCK_LOG_OUTOF_BLK,
						       so, asoc, stcb->asoc.total_output_queue_size);
				}
			}
			SOCKBUF_UNLOCK(&so->so_snd);
			if (stcb->asoc.state & SCTP_STATE_ABOUT_TO_BE_FREED) {
				goto out_unlocked;
			}
		}
		SCTP_TCB_SEND_LOCK(stcb);
		if (sp) {
			if (sp->msg_is_complete == 0) {
				strm->last_msg_incomplete = 1;
				asoc->stream_locked = 1;
				asoc->stream_locked_on  = srcv->sinfo_stream;
			} else {
				sp->sender_all_done = 1;
				strm->last_msg_incomplete = 0;
				asoc->stream_locked = 0;
			}
		} else {
			SCTP_PRINTF("Huh no sp TSNH?\n");
			strm->last_msg_incomplete = 0;
			asoc->stream_locked = 0;
		}
		SCTP_TCB_SEND_UNLOCK(stcb);
#if defined(__APPLE__)
#if defined(APPLE_SNOWLEOPARD) || defined(APPLE_LION)
		if (uio_resid(uio) == 0) {
#else
		if (uio->uio_resid == 0) {
#endif
#else
		if (uio->uio_resid == 0) {
#endif
			got_all_of_the_send = 1;
		}
	} else {
		/* We send in a 0, since we do NOT have any locks */
		error = sctp_msg_append(stcb, net, top, srcv, 0);
		top = NULL;
		if (srcv->sinfo_flags & SCTP_EOF) {
			/*
			 * This should only happen for Panda for the mbuf
			 * send case, which does NOT yet support EEOR mode.
			 * Thus, we can just set this flag to do the proper
			 * EOF handling.
			 */
			got_all_of_the_send = 1;
		}
	}
	if (error) {
		goto out;
	}
dataless_eof:
	/* EOF thing ? */
	if ((srcv->sinfo_flags & SCTP_EOF) &&
	    (got_all_of_the_send == 1) &&
	    (stcb->sctp_ep->sctp_flags & SCTP_PCB_FLAGS_UDPTYPE)) {
		int cnt;
		SCTP_STAT_INCR(sctps_sends_with_eof);
		error = 0;
		if (hold_tcblock == 0) {
			SCTP_TCB_LOCK(stcb);
			hold_tcblock = 1;
		}
		cnt = sctp_is_there_unsent_data(stcb, SCTP_SO_LOCKED);
		if (TAILQ_EMPTY(&asoc->send_queue) &&
		    TAILQ_EMPTY(&asoc->sent_queue) &&
		    (cnt == 0)) {
			if (asoc->locked_on_sending) {
				goto abort_anyway;
			}
			/* there is nothing queued to send, so I'm done... */
			if ((SCTP_GET_STATE(asoc) != SCTP_STATE_SHUTDOWN_SENT) &&
			    (SCTP_GET_STATE(asoc) != SCTP_STATE_SHUTDOWN_RECEIVED) &&
			    (SCTP_GET_STATE(asoc) != SCTP_STATE_SHUTDOWN_ACK_SENT)) {
				struct sctp_nets *netp;

				if (stcb->asoc.alternate) {
					netp = stcb->asoc.alternate;
				} else {
					netp = stcb->asoc.primary_destination;
				}
				/* only send SHUTDOWN the first time through */
				sctp_send_shutdown(stcb, netp);
				if (SCTP_GET_STATE(asoc) == SCTP_STATE_OPEN) {
					SCTP_STAT_DECR_GAUGE32(sctps_currestab);
				}
				SCTP_SET_STATE(asoc, SCTP_STATE_SHUTDOWN_SENT);
				SCTP_CLEAR_SUBSTATE(asoc, SCTP_STATE_SHUTDOWN_PENDING);
				sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWN, stcb->sctp_ep, stcb,
				                 netp);
				sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWNGUARD, stcb->sctp_ep, stcb,
				                 asoc->primary_destination);
			}
		} else {
			/*-
			 * we still got (or just got) data to send, so set
			 * SHUTDOWN_PENDING
			 */
			/*-
			 * XXX sockets draft says that SCTP_EOF should be
			 * sent with no data.  currently, we will allow user
			 * data to be sent first and move to
			 * SHUTDOWN-PENDING
			 */
			if ((SCTP_GET_STATE(asoc) != SCTP_STATE_SHUTDOWN_SENT) &&
			    (SCTP_GET_STATE(asoc) != SCTP_STATE_SHUTDOWN_RECEIVED) &&
			    (SCTP_GET_STATE(asoc) != SCTP_STATE_SHUTDOWN_ACK_SENT)) {
				if (hold_tcblock == 0) {
					SCTP_TCB_LOCK(stcb);
					hold_tcblock = 1;
				}
				if (asoc->locked_on_sending) {
					/* Locked to send out the data */
					struct sctp_stream_queue_pending *sp;
					sp = TAILQ_LAST(&asoc->locked_on_sending->outqueue, sctp_streamhead);
					if (sp) {
						if ((sp->length == 0) && (sp->msg_is_complete == 0))
							asoc->state |= SCTP_STATE_PARTIAL_MSG_LEFT;
					}
				}
				asoc->state |= SCTP_STATE_SHUTDOWN_PENDING;
				if (TAILQ_EMPTY(&asoc->send_queue) &&
				    TAILQ_EMPTY(&asoc->sent_queue) &&
				    (asoc->state & SCTP_STATE_PARTIAL_MSG_LEFT)) {
				abort_anyway:
					if (free_cnt_applied) {
						atomic_add_int(&stcb->asoc.refcnt, -1);
						free_cnt_applied = 0;
					}
					sctp_abort_an_association(stcb->sctp_ep, stcb,
					                          SCTP_RESPONSE_TO_USER_REQ,
					                          NULL, SCTP_SO_LOCKED);
					/* now relock the stcb so everything is sane */
					hold_tcblock = 0;
					stcb = NULL;
					goto out;
				}
				sctp_timer_start(SCTP_TIMER_TYPE_SHUTDOWNGUARD, stcb->sctp_ep, stcb,
				                 asoc->primary_destination);
				sctp_feature_off(inp, SCTP_PCB_FLAGS_NODELAY);
			}
		}
	}
skip_out_eof:
	if (!TAILQ_EMPTY(&stcb->asoc.control_send_queue)) {
		some_on_control = 1;
	}
	if (queue_only_for_init) {
		if (hold_tcblock == 0) {
			SCTP_TCB_LOCK(stcb);
			hold_tcblock = 1;
		}
		if (SCTP_GET_STATE(&stcb->asoc) == SCTP_STATE_OPEN) {
			/* a collision took us forward? */
			queue_only = 0;
		} else {
			sctp_send_initiate(inp, stcb, SCTP_SO_LOCKED);
			SCTP_SET_STATE(&stcb->asoc, SCTP_STATE_COOKIE_WAIT);
			queue_only = 1;
		}
	}
	if ((net->flight_size > net->cwnd) &&
	    (stcb->asoc.sctp_cmt_on_off == 0)) {
		SCTP_STAT_INCR(sctps_send_cwnd_avoid);
		queue_only = 1;
	} else if (asoc->ifp_had_enobuf) {
		SCTP_STAT_INCR(sctps_ifnomemqueued);
		if (net->flight_size > (2 * net->mtu)) {
			queue_only = 1;
		}
		asoc->ifp_had_enobuf = 0;
	}
	un_sent = ((stcb->asoc.total_output_queue_size - stcb->asoc.total_flight) +
	           (stcb->asoc.stream_queue_cnt * sizeof(struct sctp_data_chunk)));
	if ((sctp_is_feature_off(inp, SCTP_PCB_FLAGS_NODELAY)) &&
	    (stcb->asoc.total_flight > 0) &&
	    (stcb->asoc.stream_queue_cnt < SCTP_MAX_DATA_BUNDLING) &&
	    (un_sent < (int)(stcb->asoc.smallest_mtu - SCTP_MIN_OVERHEAD))) {
		/*-
		 * Ok, Nagle is set on and we have data outstanding.
		 * Don't send anything and let SACKs drive out the
		 * data unless wen have a "full" segment to send.
		 */
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_NAGLE_LOGGING_ENABLE) {
			sctp_log_nagle_event(stcb, SCTP_NAGLE_APPLIED);
		}
		SCTP_STAT_INCR(sctps_naglequeued);
		nagle_applies = 1;
	} else {
		if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_NAGLE_LOGGING_ENABLE) {
			if (sctp_is_feature_off(inp, SCTP_PCB_FLAGS_NODELAY))
				sctp_log_nagle_event(stcb, SCTP_NAGLE_SKIPPED);
		}
		SCTP_STAT_INCR(sctps_naglesent);
		nagle_applies = 0;
	}
	if (SCTP_BASE_SYSCTL(sctp_logging_level) & SCTP_BLK_LOGGING_ENABLE) {
		sctp_misc_ints(SCTP_CWNDLOG_PRESEND, queue_only_for_init, queue_only,
		               nagle_applies, un_sent);
		sctp_misc_ints(SCTP_CWNDLOG_PRESEND, stcb->asoc.total_output_queue_size,
		               stcb->asoc.total_flight,
		               stcb->asoc.chunks_on_out_queue, stcb->asoc.total_flight_count);
	}
	if (queue_only_for_init)
		queue_only_for_init = 0;
	if ((queue_only == 0) && (nagle_applies == 0) && (stcb->asoc.peers_rwnd && un_sent)) {
		/* we can attempt to send too. */
		if (hold_tcblock == 0) {
			/* If there is activity recv'ing sacks no need to send */
			if (SCTP_TCB_TRYLOCK(stcb)) {
				sctp_chunk_output(inp, stcb, SCTP_OUTPUT_FROM_USR_SEND, SCTP_SO_LOCKED);
				hold_tcblock = 1;
			}
		} else {
			sctp_chunk_output(inp, stcb, SCTP_OUTPUT_FROM_USR_SEND, SCTP_SO_LOCKED);
		}
	} else if ((queue_only == 0) &&
	           (stcb->asoc.peers_rwnd == 0) &&
	           (stcb->asoc.total_flight == 0)) {
		/* We get to have a probe outstanding */
		if (hold_tcblock == 0) {
			hold_tcblock = 1;
			SCTP_TCB_LOCK(stcb);
		}
		sctp_chunk_output(inp, stcb, SCTP_OUTPUT_FROM_USR_SEND, SCTP_SO_LOCKED);
	} else if (some_on_control) {
		int num_out, reason, frag_point;

		/* Here we do control only */
		if (hold_tcblock == 0) {
			hold_tcblock = 1;
			SCTP_TCB_LOCK(stcb);
		}
		frag_point = sctp_get_frag_point(stcb, &stcb->asoc);
		(void)sctp_med_chunk_output(inp, stcb, &stcb->asoc, &num_out,
		                            &reason, 1, 1, &now, &now_filled, frag_point, SCTP_SO_LOCKED);
	}
	SCTPDBG(SCTP_DEBUG_OUTPUT1, "USR Send complete qo:%d prw:%d unsent:%d tf:%d cooq:%d toqs:%d err:%d\n",
	        queue_only, stcb->asoc.peers_rwnd, un_sent,
		stcb->asoc.total_flight, stcb->asoc.chunks_on_out_queue,
	        stcb->asoc.total_output_queue_size, error);

out:
#if defined(__APPLE__)
	sbunlock(&so->so_snd, 1);
#endif
out_unlocked:

	if (local_soresv && stcb) {
		atomic_subtract_int(&stcb->asoc.sb_send_resv, sndlen);
		local_soresv = 0;
	}
	if (create_lock_applied) {
		SCTP_ASOC_CREATE_UNLOCK(inp);
		create_lock_applied = 0;
	}
	if ((stcb) && hold_tcblock) {
		SCTP_TCB_UNLOCK(stcb);
	}
	if (stcb && free_cnt_applied) {
		atomic_add_int(&stcb->asoc.refcnt, -1);
	}
#ifdef INVARIANTS
#if !defined(__APPLE__)
	if (stcb) {
		if (mtx_owned(&stcb->tcb_mtx)) {
			panic("Leaving with tcb mtx owned?");
		}
		if (mtx_owned(&stcb->tcb_send_mtx)) {
			panic("Leaving with tcb send mtx owned?");
		}
	}
#endif
#endif
#ifdef __Panda__
	/*
	 * Handle the EAGAIN/ENOMEM cases to reattach the pak header
	 * to particle when pak is passed in, so that caller
	 * can try again with this pak
	 *
	 * NOTE: For other cases, including success case,
	 * we simply want to return the header back to free
	 * pool
	 */
	if (top) {
		if ((error == EAGAIN) || (error == ENOMEM)) {
			SCTP_ATTACH_CHAIN(i_pak, top, sndlen);
			top = NULL;
		} else {
			(void)SCTP_RELEASE_HEADER(i_pak);
		}
	} else {
		/* This is to handle cases when top has
		 * been reset to NULL but pak might not
		 * be freed
		 */
		if (i_pak) {
			(void)SCTP_RELEASE_HEADER(i_pak);
		}
	}
#endif
#ifdef INVARIANTS
	if (inp) {
		sctp_validate_no_locks(inp);
	} else {
		printf("Warning - inp is NULL so cant validate locks\n");
	}
#endif
	if (top) {
		sctp_m_freem(top);
	}
	if (control) {
		sctp_m_freem(control);
	}
	return (error);
}


/*
 * generate an AUTHentication chunk, if required
 */
struct mbuf *
sctp_add_auth_chunk(struct mbuf *m, struct mbuf **m_end,
    struct sctp_auth_chunk **auth_ret, uint32_t * offset,
    struct sctp_tcb *stcb, uint8_t chunk)
{
	struct mbuf *m_auth;
	struct sctp_auth_chunk *auth;
	int chunk_len;

	if ((m_end == NULL) || (auth_ret == NULL) || (offset == NULL) ||
	    (stcb == NULL))
		return (m);

	/* sysctl disabled auth? */
	if (SCTP_BASE_SYSCTL(sctp_auth_disable))
		return (m);

	/* peer doesn't do auth... */
	if (!stcb->asoc.peer_supports_auth) {
		return (m);
	}
	/* does the requested chunk require auth? */
	if (!sctp_auth_is_required_chunk(chunk, stcb->asoc.peer_auth_chunks)) {
		return (m);
	}
	m_auth = sctp_get_mbuf_for_msg(sizeof(*auth), 0, M_DONTWAIT, 1, MT_HEADER);
	if (m_auth == NULL) {
		/* no mbuf's */
		return (m);
	}
	/* reserve some space if this will be the first mbuf */
	if (m == NULL)
		SCTP_BUF_RESV_UF(m_auth, SCTP_MIN_OVERHEAD);
	/* fill in the AUTH chunk details */
	auth = mtod(m_auth, struct sctp_auth_chunk *);
	bzero(auth, sizeof(*auth));
	auth->ch.chunk_type = SCTP_AUTHENTICATION;
	auth->ch.chunk_flags = 0;
	chunk_len = sizeof(*auth) +
	    sctp_get_hmac_digest_len(stcb->asoc.peer_hmac_id);
	auth->ch.chunk_length = htons(chunk_len);
	auth->hmac_id = htons(stcb->asoc.peer_hmac_id);
	/* key id and hmac digest will be computed and filled in upon send */

	/* save the offset where the auth was inserted into the chain */
	if (m != NULL) {
		struct mbuf *cn;
		*offset = 0;
		cn = m;
		while(cn) {
			*offset += SCTP_BUF_LEN(cn);
			cn = SCTP_BUF_NEXT(cn);
		}
	}else
		*offset = 0;

	/* update length and return pointer to the auth chunk */
	SCTP_BUF_LEN(m_auth) = chunk_len;
	m = sctp_copy_mbufchain(m_auth, m, m_end, 1, chunk_len, 0);
	if (auth_ret != NULL)
		*auth_ret = auth;

	return (m);
}

#if defined(__FreeBSD__)  || defined(__APPLE__)
#ifdef INET6
int
sctp_v6src_match_nexthop(struct sockaddr_in6 *src6, sctp_route_t *ro)
{
	struct nd_prefix *pfx = NULL;
	struct nd_pfxrouter *pfxrtr = NULL;
	struct sockaddr_in6 gw6;

	if (ro == NULL || ro->ro_rt == NULL || src6->sin6_family != AF_INET6)
		return (0);

	/* get prefix entry of address */
	LIST_FOREACH(pfx, &MODULE_GLOBAL(nd_prefix), ndpr_entry) {
		if (pfx->ndpr_stateflags & NDPRF_DETACHED)
			continue;
		if (IN6_ARE_MASKED_ADDR_EQUAL(&pfx->ndpr_prefix.sin6_addr,
		    &src6->sin6_addr, &pfx->ndpr_mask))
			break;
	}
	/* no prefix entry in the prefix list */
	if (pfx == NULL) {
		SCTPDBG(SCTP_DEBUG_OUTPUT2, "No prefix entry for ");
		SCTPDBG_ADDR(SCTP_DEBUG_OUTPUT2, (struct sockaddr *)src6);
		return (0);
	}

	SCTPDBG(SCTP_DEBUG_OUTPUT2, "v6src_match_nexthop(), Prefix entry is ");
	SCTPDBG_ADDR(SCTP_DEBUG_OUTPUT2, (struct sockaddr *)src6);

	/* search installed gateway from prefix entry */
	for (pfxrtr = pfx->ndpr_advrtrs.lh_first; pfxrtr; pfxrtr =
	     pfxrtr->pfr_next) {
		memset(&gw6, 0, sizeof(struct sockaddr_in6));
		gw6.sin6_family = AF_INET6;
#if !defined(__Windows__) && !defined(__Userspace_os_Linux)
		gw6.sin6_len = sizeof(struct sockaddr_in6);
#endif
		memcpy(&gw6.sin6_addr, &pfxrtr->router->rtaddr,
		    sizeof(struct in6_addr));
		SCTPDBG(SCTP_DEBUG_OUTPUT2, "prefix router is ");
		SCTPDBG_ADDR(SCTP_DEBUG_OUTPUT2, (struct sockaddr *)&gw6);
		SCTPDBG(SCTP_DEBUG_OUTPUT2, "installed router is ");
		SCTPDBG_ADDR(SCTP_DEBUG_OUTPUT2, ro->ro_rt->rt_gateway);
		if (sctp_cmpaddr((struct sockaddr *)&gw6,
				ro->ro_rt->rt_gateway)) {
			SCTPDBG(SCTP_DEBUG_OUTPUT2, "pfxrouter is installed\n");
			return(1);
		}
	}
	SCTPDBG(SCTP_DEBUG_OUTPUT2, "pfxrouter is not installed\n");
	return (0);
}
#endif

int
sctp_v4src_match_nexthop(struct sctp_ifa *sifa, sctp_route_t *ro)
{
#ifdef INET
	struct sockaddr_in *sin, *mask;
	struct ifaddr *ifa;
	struct in_addr srcnetaddr, gwnetaddr;

	if (ro == NULL || ro->ro_rt == NULL ||
	    sifa->address.sa.sa_family != AF_INET) {
		return(0);
	}
	ifa = (struct ifaddr *)sifa->ifa;
	mask = (struct sockaddr_in *)(ifa->ifa_netmask);
	sin = (struct sockaddr_in *)&sifa->address.sin;
	srcnetaddr.s_addr = (sin->sin_addr.s_addr & mask->sin_addr.s_addr);
	SCTPDBG(SCTP_DEBUG_OUTPUT1, "match_nexthop4: src address is ");
	SCTPDBG_ADDR(SCTP_DEBUG_OUTPUT2, &sifa->address.sa);
	SCTPDBG(SCTP_DEBUG_OUTPUT1, "network address is %x\n", srcnetaddr.s_addr);

	sin = (struct sockaddr_in *)ro->ro_rt->rt_gateway;
	gwnetaddr.s_addr = (sin->sin_addr.s_addr & mask->sin_addr.s_addr);
	SCTPDBG(SCTP_DEBUG_OUTPUT1, "match_nexthop4: nexthop is ");
	SCTPDBG_ADDR(SCTP_DEBUG_OUTPUT2, ro->ro_rt->rt_gateway);
	SCTPDBG(SCTP_DEBUG_OUTPUT1, "network address is %x\n", gwnetaddr.s_addr);
	if (srcnetaddr.s_addr == gwnetaddr.s_addr) {
		return(1);
	}
#endif
	return(0);
}
#elif defined(__Userspace__)
/* TODO __Userspace__ versions of sctp_vXsrc_match_nexthop(). */
int
sctp_v6src_match_nexthop(struct sockaddr_in6 *src6, sctp_route_t *ro)
{
    return 0;
}
int
sctp_v4src_match_nexthop(struct sctp_ifa *sifa, sctp_route_t *ro)
{
    return 0;
}

#endif