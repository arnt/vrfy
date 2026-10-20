/*
 * Copyright (c) 1983 Eric P. Allman
 * Copyright (c) 1988 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted provided
 * that: (1) source distributions retain this entire copyright notice and
 * comment, and (2) distributions including binaries display the following
 * acknowledgement:  ``This product includes software developed by the
 * University of California, Berkeley and its contributors'' in the
 * documentation or other materials provided with the distribution and in
 * all advertising materials mentioning features or use of this software.
 * Neither the name of the University nor the names of its contributors may
 * be used to endorse or promote products derived from this software without
 * specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef lint
static char Version[] = "@(#)conn.c	e07@nikhef.nl (Eric Wassenaar) 970826";
#endif

#include "vrfy.h"

extern int verbose;
extern int debug;

#define setalarm(n)	(void) alarm((unsigned int)(n))

char *CurHostName = NULL;	/* remote host we are connecting to */
char *MyHostName = NULL;	/* my own fully qualified host name */

int ConnTimeout = CONNTIMEOUT;	/* timeout in secs for connect */
int ReadTimeout = READTIMEOUT;	/* timeout in secs for read reply */

/*
** SFGETS -- Read an input line, using timeout
** -------------------------------------------
**
**	Returns:
**		Pointer to start of input line.
**		NULL on error (including timeout).
*/

/*
 * sigsetjmp, because glibc's setjmp does not save the signal mask.
 * SIGALRM would stay blocked after the first timeout.
 */
static sigjmp_buf Timeout;

sigtype_t
/*ARGSUSED*/
timer(int sig)
{
	siglongjmp(Timeout, 1);
	/*NOTREACHED*/
}


char *
sfgets(
	char *buf,			/* input buffer */
	int size,			/* size of input buffer */
	FILE *fp)			/* input channel */
{
	register char *p = NULL;

	if (sigsetjmp(Timeout, 1) != 0)
	{
		errno = ETIMEDOUT;
		return(NULL);
	}

	(void) signal(SIGALRM, timer);
	setalarm(ReadTimeout);
	while ((p == NULL) && !feof(fp) && !ferror(fp))
	{
		errno = 0;
		p = fgets(buf, size, fp);
		if (errno == EINTR)
			clearerr(fp);
	}
	setalarm(0);

	if ((p == NULL) && feof(fp) && (errno == 0))
		errno = ECONNRESET;
	if ((p == NULL) && ferror(fp) && (errno == 0))
		errno = EIO;
	return(p);
}

/*
** LOOKUP -- Fetch the ipv4 and ipv6 addresses of a host
** -----------------------------------------------------
**
**	Returns:
**		Status code indicating success or failure.
**
**	Outputs:
**		Sets res to the address list, which the caller frees.
**		Sets h_errno for giveresponse().
*/

static int
lookup(
	char *host,			/* host name to look up */
	struct addrinfo **res)		/* resulting address list */
{
	struct addrinfo hints;
	int status;

	(void) memset((char *)&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_CANONNAME;

	errno = 0;
	h_errno = 0;

	status = EAI_NONAME;
	if (alabel(host) != NULL)
		status = getaddrinfo(alabel(host), NULL, &hints, res);
	if (status == 0)
		return(EX_SUCCESS);

	/* cannot contact nameserver, force retry */
	if (status == EAI_AGAIN || errno == ETIMEDOUT || errno == ECONNREFUSED)
	{
		h_errno = TRY_AGAIN;
		return(EX_TEMPFAIL);
	}

	/* no address found by nameserver */
#ifdef EAI_NODATA
	h_errno = (status == EAI_NODATA) ? NO_ADDRESS : HOST_NOT_FOUND;
#else
	h_errno = HOST_NOT_FOUND;
#endif
	return(EX_NOHOST);
}

/*
** MAKECONNECTION -- Establish SMTP connection to remote host
** ----------------------------------------------------------
**
**	Returns:
**		Status code indicating success or failure.
**
**	Outputs:
**		Sets outfile and infile to the output and input channel.
**		Sets CurHostName to the name of the remote host.
**
**	IPv6 addresses are tried before IPv4 addresses.
*/

int
makeconnection(
	char *host,			/* host name to connect to */
	FILE **outfile,			/* smtp output channel */
	FILE **infile)			/* smtp input channel */
{
	static int family[] = { AF_INET6, AF_INET };
	struct addrinfo *res, *ai;
	struct servent *sp;
	struct sockaddr *sa;
	socklen_t salen;
	static char hostname[MAXHOST+1];
	char name[NI_MAXHOST];
	struct sockaddr_storage inaddr[MAXADDRS];
	int naddrs;
	ipaddr_t addr;
	int status;
	int sock;
	register int i, f;

/*
 * Reset state.
 */
	(void) memset((char *)inaddr, 0, sizeof(inaddr));
	CurHostName = NULL;

	errno = 0;
	h_errno = 0;

	if (host == NULL || host[0] == '\0')
		host = "localhost";

/*
 * Check for dotted quad, potentially within brackets.
 */
	addr = numeric_addr(host);
	if (addr == NOT_DOTTED_QUAD)
		addr = inet_addr(host);

/*
 * Fetch the ip addresses of the given host.
 */
	if (addr != NOT_DOTTED_QUAD)
	{
		struct sockaddr_in *sin = (struct sockaddr_in *)&inaddr[0];

		sin->sin_family = AF_INET;
		sin->sin_addr.s_addr = addr;
		naddrs = 1;

		if (getnameinfo((struct sockaddr *)sin, sizeof(*sin),
		    name, sizeof(name), NULL, 0, NI_NAMEREQD) == 0)
			host = ulabel(name);
	}
	else
	{
		status = lookup(host, &res);
		if (status != EX_SUCCESS)
			return(status);

		if (res->ai_canonname != NULL)
		{
			(void) strncpy(name, res->ai_canonname, sizeof(name));
			name[sizeof(name) - 1] = '\0';
			host = ulabel(name);
		}

		naddrs = 0;
		for (f = 0; f < 2; f++)
		{
			for (ai = res; ai != NULL; ai = ai->ai_next)
			{
				if (ai->ai_family != family[f] ||
				    naddrs >= MAXADDRS)
					continue;
				(void) memcpy((char *)&inaddr[naddrs],
					(char *)ai->ai_addr, ai->ai_addrlen);
				naddrs++;
			}
		}
		freeaddrinfo(res);
	}

	(void) strncpy(hostname, host, MAXHOST);
	hostname[MAXHOST] = '\0';
	CurHostName = hostname;

/*
 * Try to make connection to each of the addresses in turn.
 */
	sp = getservbyname("smtp", "tcp");
	if (sp == NULL)
		return(EX_OSERR);

	for (i = 0; i < naddrs; i++)
	{
		sa = (struct sockaddr *)&inaddr[i];
		if (sa->sa_family == AF_INET6)
		{
			salen = sizeof(struct sockaddr_in6);
			((struct sockaddr_in6 *)sa)->sin6_port = sp->s_port;
		}
		else
		{
			salen = sizeof(struct sockaddr_in);
			((struct sockaddr_in *)sa)->sin_port = sp->s_port;
		}

		/* the kernel may lack ipv6, and then ipv4 is our hope */
		sock = socket(sa->sa_family, SOCK_STREAM, 0);
		if (sock < 0 && errno == EAFNOSUPPORT)
			continue;
		if (sock < 0)
			return(EX_TEMPFAIL);

		if (verbose >= 2 || debug)
		{
			if (getnameinfo(sa, salen, name, sizeof(name),
			    NULL, 0, NI_NUMERICHOST) != 0)
				(void) strcpy(name, "?");
			printf("connecting to %s (%s) port %d\n", CurHostName,
				name, ntohs(sp->s_port));
		}

		if (sigsetjmp(Timeout, 1) != 0)
		{
			(void) close(sock);
			errno = ETIMEDOUT;
			continue;
		}

		(void) signal(SIGALRM, timer);
		setalarm(ConnTimeout);
		if (connect(sock, sa, salen) < 0)
		{
			int err = errno;
			setalarm(0);
			(void) close(sock);
			errno = err;
			if (errno == EINTR)
				errno = ETIMEDOUT;
			if (errno == ECONNREFUSED)
				return(EX_TEMPFAIL);
			continue;
		}
		setalarm(0);

		*outfile = fdopen(sock, "w");
		*infile  = fdopen(dup(sock), "r");

		if (*infile == NULL || *outfile == NULL)
		{
			int err = errno;
			if (*infile != NULL)
				(void) fclose(*infile);
			if (*outfile != NULL)
				(void) fclose(*outfile);
			errno = err;
			return(EX_TEMPFAIL);
		}

		if (debug)
			printf("connected to %s\n", CurHostName);

		errno = 0;
		h_errno = 0;
		return(EX_SUCCESS);
	}

	return(EX_TEMPFAIL);
}

/*
** SETMYHOSTNAME -- Determine own fqdn hostname
** --------------------------------------------
**
**	Returns:
**		None.
**
**	Outputs:
**		Sets MyHostName to the local hostname.
*/

void
setmyhostname(void)
{
	static char hostname[MAXHOST+1];
	int status;

	if (MyHostName == NULL)
	{
		status = getmyhostname(hostname);
		if (status != EX_SUCCESS)
		{
			giveresponse(status);
			exit(status);
		}

		MyHostName = hostname;
	}
}

/*
** GETMYHOSTNAME -- Determine own fqdn hostname
** --------------------------------------------
**
**	Returns:
**		Status code indicating success or failure.
**
**	Outputs:
**		Stores hostname in given buffer.
*/

int
getmyhostname(
	char *hostname)			/* buffer to store host name */
{
	struct addrinfo *res;
	int status;

	errno = 0;
	h_errno = 0;

	if (gethostname(hostname, MAXHOST) < 0)
	{
		perror("gethostname");
		return(EX_OSERR);
	}
	hostname[MAXHOST] = '\0';

	status = lookup(hostname, &res);
	if (status != EX_SUCCESS)
		return(status);

	if (res->ai_canonname != NULL)
		(void) strncpy(hostname, res->ai_canonname, MAXHOST);
	hostname[MAXHOST] = '\0';
	freeaddrinfo(res);
	return(EX_SUCCESS);
}

/*
** INTERNET -- Check whether given name has an internet address
** ------------------------------------------------------------
**
**	Returns:
**		TRUE if an internet address exists.
**		FALSE otherwise.
**
**	The given name can be a dotted quad, perhaps between
**	square brackets. If not, an A or AAAA record must exist.
**
**	Note that we do not check the status after a negative return
**	from lookup. Failure can be due to nameserver timeout,
**	in which case the result is still undecided.
**	Currently we consider this an error, so that we won't retry
**	such host during recursive lookups.
*/

bool
internet(
	char *host)			/* host name to check */
{
	ipaddr_t addr;
	struct addrinfo *res;

	/* check dotted quad between brackets */
	addr = numeric_addr(host);
	if (addr != NOT_DOTTED_QUAD)
		return(TRUE);

	/* check plain dotted quad */
	addr = inet_addr(host);
	if (addr != NOT_DOTTED_QUAD)
		return(TRUE);

	/* check if nameserver can resolve it */
	if (lookup(host, &res) == EX_SUCCESS)
	{
		freeaddrinfo(res);
		return(TRUE);
	}

	/* probably not, but could be nameserver timeout */
	return(FALSE);
}

/*
** NUMERIC_ADDR -- Check if we have a dotted quad between brackets
** ---------------------------------------------------------------
**
**	Returns:
**		The numeric address if yes.
**		NOT_DOTTED_QUAD if not.
*/

ipaddr_t
numeric_addr(
	char *host)			/* host name to check */
{
	ipaddr_t addr;
	register char *p;

	if (host[0] != '[')
		return(NOT_DOTTED_QUAD);

	p = strchr(host+1, ']');
	if (p == NULL || p[1] != '\0')
		return(NOT_DOTTED_QUAD);

	*p = '\0';
	addr = inet_addr(host+1);
	*p = ']';

	return(addr);
}
