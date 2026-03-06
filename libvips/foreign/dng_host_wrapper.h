/* Thin C wrapper around DNG SDK's dng_host for use with libraw.
 */

#ifndef VIPS_DNG_HOST_WRAPPER_H
#define VIPS_DNG_HOST_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Create a dng_host and wire it to a libraw processor.
 * Returns an opaque host pointer, or NULL on error / not available.
 */
void *vips_dng_host_attach(void *raw_processor);

/* Detach and free the dng_host from a libraw processor.
 */
void vips_dng_host_detach(void *raw_processor, void *host);

#ifdef __cplusplus
}
#endif

#endif /* VIPS_DNG_HOST_WRAPPER_H */
