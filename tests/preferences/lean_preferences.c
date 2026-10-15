/* Core preferences must work when the ESP backend is compiled out. */
#define SDL_MAIN_HANDLED
#include "../../configcfg.h"

/* No controller bindings are active in this core-only fixture. */
auint cu_vdev_default_map_for_host_type(cu_vdev_host_type_t type, auint slot)
{
	(void)type;
	(void)slot;
	fputs("FAIL: unexpected controller-map migration\n", stderr);
	exit(1);
}

int main(void)
{
	app_config_t expected, loaded;
	appcfg_defaults(&expected);
	expected.audio_master_volume = 137U;
	expected.audio_freqscale = TRUE;
	expected.frame_rate_limiter = FALSE;
	if (!appcfg_save(&expected, "_obj_/lean-preferences.cfg") ||
	    !appcfg_load_or_create(&loaded, "_obj_/lean-preferences.cfg") ||
	    loaded.audio_master_volume != 137U || !loaded.audio_freqscale ||
	    loaded.frame_rate_limiter){
		fputs("FAIL: core preferences without ESP\n", stderr);
		return 1;
	}
	puts("PASS: core preferences save/reload with ESP compiled out");
	return 0;
}
