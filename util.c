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
static char Version[] = "@(#)util.c	e07@nikhef.nl (Eric Wassenaar) 971113";
#endif

#include "vrfy.h"

/*
** FIXCRLF -- Fix trailing <CR><LF> combination in line
** ----------------------------------------------------
**
**	Unconditionally strip a trailing <LF> from an input line,
**	and optionally strip a preceding <CR>.
**	Both characters must be removed during SMTP input, since
**	they are part of the protocol.
**	But when reading local input, we must just remove the
**	trailing <LF>, being the UNIX canonical <NL> character,
**	and preserve a possible <CR>. This guarantees transparent
**	transmission of files ending in <CR><LF>.
**
**	Returns:
**		None.
**
**	Side effects:
**		The line is changed in place.
*/

void
fixcrlf(
	char *line,			/* the input line to fix */
	bool stripcr)			/* also strip CR, if set */
{
	register char *p;

	p = strchr(line, '\n');
	if (p != NULL)
	{
		/* move back to preceding CR if necessary */
		if (stripcr && ((p > line) && (p[-1] == '\r')))
			p--;

		/* properly terminate the line */
		*p = '\0';
	}
}

/*
** MAXSTR -- Ensure string does not exceed maximum size
** ----------------------------------------------------
**
**	Returns:
**		Pointer to the (possibly truncated) string.
**
**	If necessary, a new string is allocated, and is then
**	truncated, and the original string is left intact.
**	Otherwise the original string is truncated in place.
**
*/

char *
maxstr(
	char *string,			/* the string to check */
	int n,				/* the maximum allowed size */
	bool save)			/* allocate new string, if set */
{
	if (strlength(string) > n)
	{
		if (save)
			string = newstr(string);
		string[n] = '\0';
	}
	return(string);
}

/*
** PRINTABLE -- Expand quote bits/control chars in a string
** --------------------------------------------------------
**
**	Returns:
**		Pointer to static buffer containing the expansion.
**
**	The expanded string is silently truncated if it gets too long.
**	Well-formed UTF-8 is passed through as is.
*/

char *
printable(
	char *string)			/* the string to expand */
{
	static char buf[BUFSIZ];	/* expanded string buffer */
	register char *p = buf;
	register char *s = string;
	register char c;
	bool utf8 = validutf8(string);	/* set if non-ascii is readable */

	while ((c = *s++) != '\0')
	{
		if (p >= buf + sizeof(buf) - 4)
			break;

		/* C1 controls are shown like the raw bytes used to be */
		if (utf8 && (u_char)c == 0xC2 && (u_char)*s >= 0x80 &&
		    (u_char)*s <= 0x9F)
			c = *s++;
		else if (!isascii(c) && utf8)
		{
			*p++ = c;
			continue;
		}
		if (!isascii(c))
		{
			*p++ = '\\';
			c &= 0177;
		}
		if (iscntrl(c) && !isspace(c))
		{
			*p++ = '^';
			c ^= 0100;
		}
		*p++ = c;
	}
	*p = '\0';

	return(buf);
}

/*
** XALLOC -- Allocate or reallocate additional memory
** --------------------------------------------------
**
**	Returns:
**		Pointer to (re)allocated buffer space.
**		Aborts if the requested memory could not be obtained.
*/

ptr_t *
xalloc(
	register ptr_t *buf,		/* current start of buffer space */
	siz_t size)			/* number of bytes to allocate */
{
	if (buf == NULL)
		buf = malloc(size);
	else
		buf = realloc(buf, size);

	if (buf == NULL)
	{
		error("Out of memory");
		exit(EX_OSERR);
	}

	return(buf);
}

/*
** ITOA -- Convert integer value to ascii string
** ---------------------------------------------
**
**	Returns:
**		Pointer to static storage containing string.
*/

char *
itoa(
	int n)				/* value to convert */
{
	static char buf[30];		/* sufficient for 64-bit values */

	(void) sprintf(buf, "%d", n);
	return(buf);
}

/*
** NONASCII -- Check whether a string contains non-ASCII
** -----------------------------------------------------
**
**	Returns:
**		TRUE if any byte in the string is not ASCII.
**		FALSE otherwise.
*/

bool
nonascii(
	char *string)			/* the string to check */
{
	register char *p;

	for (p = string; *p != '\0'; p++)
	{
		if (!isascii(*p))
			return(TRUE);
	}

	return(FALSE);
}

/*
** VALIDUTF8 -- Check whether a string is well-formed UTF-8
** --------------------------------------------------------
**
**	Returns:
**		TRUE if the string is well-formed UTF-8 (RFC3629).
**		FALSE otherwise.
**
**	Overlong forms, surrogates and code points above U+10FFFF
**	are rejected. Plain ASCII is trivially well-formed.
*/

bool
validutf8(
	char *string)			/* the string to check */
{
	register u_char *p = (u_char *)string;
	register int n;
	unsigned long c, min;

	while (*p != '\0')
	{
		if (*p < 0x80)
		{
			p++;
			continue;
		}

		if (*p >= 0xC2 && *p <= 0xDF)
			n = 1, c = *p & 0x1F, min = 0x80;
		else if (*p >= 0xE0 && *p <= 0xEF)
			n = 2, c = *p & 0x0F, min = 0x800;
		else if (*p >= 0xF0 && *p <= 0xF4)
			n = 3, c = *p & 0x07, min = 0x10000;
		else
			return(FALSE);

		/* the terminating null byte fails this test too */
		for (p++; n > 0; n--, p++)
		{
			if ((*p & 0xC0) != 0x80)
				return(FALSE);
			c = (c << 6) | (*p & 0x3F);
		}

		if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))
			return(FALSE);
	}

	return(TRUE);
}

/*
** ALABEL -- Convert a domain name to the form used in the DNS
** -----------------------------------------------------------
**
**	Returns:
**		Pointer to the A-label form of the domain.
**		The domain itself if it is plain ASCII.
**		NULL if the domain is not a valid IDN.
**
**	Domains are kept in U-label form everywhere else, so call
**	this just before passing a name to the resolver.
*/

char *
alabel(
	char *domain)			/* domain name to convert */
{
	static char buf[MAXHOST+1];	/* converted domain name */
	char *a;			/* allocated by libidn2 */

	if (!nonascii(domain))
		return(domain);

	if (idn2_to_ascii_8z(domain, &a,
	    IDN2_NFC_INPUT | IDN2_NONTRANSITIONAL) != IDN2_OK)
		return(NULL);

	if (strlength(a) > MAXHOST)
	{
		idn2_free(a);
		return(NULL);
	}

	(void) strcpy(buf, a);
	idn2_free(a);
	return(buf);
}

/*
** ULABEL -- Convert a domain name from the DNS to readable form
** -------------------------------------------------------------
**
**	Returns:
**		Pointer to the U-label form of the domain.
**		The domain itself if it has no A-labels, or
**		if it cannot be converted.
*/

char *
ulabel(
	char *domain)			/* domain name to convert */
{
	static char buf[MAXHOST+1];	/* converted domain name */
	char *u;			/* allocated by libidn2 */

	if (strncasecmp(domain, "xn--", 4) != 0 &&
	    strstr(domain, ".xn--") == NULL && strstr(domain, ".XN--") == NULL)
		return(domain);

	if (idn2_to_unicode_8z8z(domain, &u, 0) != IDN2_OK)
		return(domain);

	if (strlength(u) > MAXHOST)
	{
		idn2_free(u);
		return(domain);
	}

	(void) strcpy(buf, u);
	idn2_free(u);
	return(buf);
}
