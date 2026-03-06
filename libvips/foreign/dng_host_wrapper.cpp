/* Thin C++ wrapper that creates a dng_host and wires it to libraw.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_DNGSDK

#include <libraw/libraw.h>
#include <dng_host.h>

#include "dng_host_wrapper.h"

extern "C" void *
vips_dng_host_attach(void *raw_processor)
{
	libraw_data_t *lr = static_cast<libraw_data_t *>(raw_processor);
	LibRaw *ip = static_cast<LibRaw *>(lr->parent_class);

	try {
		dng_host *host = new dng_host;
		ip->set_dng_host(host);
		lr->rawparams.use_dngsdk = LIBRAW_DNG_ALL;
		return host;
	}
	catch (...) {
		return NULL;
	}
}

extern "C" void
vips_dng_host_detach(void *raw_processor, void *host)
{
	libraw_data_t *lr = static_cast<libraw_data_t *>(raw_processor);
	LibRaw *ip = static_cast<LibRaw *>(lr->parent_class);

	ip->set_dng_host(NULL);
	delete static_cast<dng_host *>(host);
}

#else /* !HAVE_DNGSDK */

#include "dng_host_wrapper.h"

extern "C" void *
vips_dng_host_attach(void *raw_processor)
{
	return NULL;
}

extern "C" void
vips_dng_host_detach(void *raw_processor, void *host)
{
}

#endif /* HAVE_DNGSDK */
