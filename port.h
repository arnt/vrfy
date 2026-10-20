/*
** Various portability definitions.
**
**	@(#)port.h              e07@nikhef.nl (Eric Wassenaar) 990511
*/

/*
** Distinguish between various BIND releases.
*/

#if defined(RES_PRF_STATS)
#define BIND_49
#else
#define BIND_48
#endif

#if defined(BIND_49) && defined(__BIND)
#define BIND_493
#endif

/*
** Define constants for fixed sizes.
*/

#ifndef INT16SZ
#define	INT16SZ		2	/* for systems without 16-bit ints */
#endif

#ifndef INT32SZ
#define	INT32SZ		4	/* for systems without 32-bit ints */
#endif

/*
** The following should depend on existing definitions.
*/

#include <stdbool.h>	/* bool is a keyword in C23 */
#define TRUE	true
#define FALSE	false

#if defined(BIND_493)
typedef u_char	qbuf_t;
#else
typedef char	qbuf_t;
#endif

typedef in_addr_t	ipaddr_t;

#if defined(apollo) || defined(_BSD_SIGNALS)
typedef int	sigtype_t;
#else
typedef void	sigtype_t;
#endif

typedef void	ptr_t;		/* generic pointer type */
typedef size_t	siz_t;		/* general size type */

#define linebufmode(a)	(void) setvbuf(a, (char *)NULL, _IOLBF, BUFSIZ);

#define PROTO(TYPES)	TYPES
