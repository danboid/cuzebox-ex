#define SDL_MAIN_HANDLED
#undef main
#define main identity_emulator_main
#include "../../main.c"
#undef main
#include "../../microui/mui_integration.c"
#define CHECK(c, msg) do { if (!(c)) { fprintf(stderr,"FAIL: %s (%d)\n",msg,__LINE__); return 1; } } while(0)
static uint32 canvas[640U*560U];
static void draw(void)
{
	memset(canvas,0,sizeof(canvas));
	mui_render_overlay(canvas,640U,640U,560U);
}
int main(int argc, char **argv)
{
	cu_state_esp_t *es = cu_esp_get_state();
	mu_Container *container;
	CHECK(argc == 5,"certificate, key, wrong key, CA arguments");
	appcfg_defaults(&main_cfg); main_headless = TRUE;
	main_cfgfile = "identity.cfg";
	cu_esp_set_config_path(main_cfgfile);
	cu_esp_load_config();
	es->emulation_model = es->serial_esp_model = 1;
	cu_esp_set_serial_route(CU_ESP_SERIAL_ESP_MODULE);
	es->at_firmware_profile = CU_ESP_AT_FW_UZENET;
	CHECK(cu_esp_validate_identity(argv[1],argv[3],argv[4]) != NULL,"reject mismatched key");
	CHECK(cu_esp_validate_identity(argv[1],"missing.key",argv[4]) != NULL,"reject missing key");
	CHECK(cu_esp_validate_identity(argv[1],argv[2],argv[4]) == NULL,"valid identity");
	/* The AVR can configure USART before releasing module reset. */
	cu_esp_uzebox_modify(CU_IO_UBRR0H, 1, 0);
	cu_esp_uzebox_modify(CU_IO_UBRR0L, 0x74, 0);
	cu_esp_uzebox_modify(CU_IO_UCSR0A, 2, 0);
	cu_esp_uzebox_modify(CU_IO_UCSR0C, 6, 0);
	cu_esp_uzebox_modify(CU_IO_UCSR0B, 0x18, 0);
	CHECK(mainui_apply_esp_identity(1,argv[1],argv[2],argv[4]) == NULL,"apply and save identity");
	CHECK(es->uart_tx_enabled && es->uart_rx_enabled && es->uart_baud_bits == 372 &&
	      es->uart_ucsr0c == 6 && es->uart_double_speed,"ESP reset preserves AVR USART");
	CHECK(es->ssl_pki_num[0] == 1 && !strcmp((char *)es->ssl_pki_key_path[1],argv[2]),"reset retains slot and key");
	CHECK(mainui_apply_esp_identity(0,argv[1],argv[3],argv[4]) != NULL && es->ssl_pki_num[0] == 1,"invalid apply preserves identity");
	main_cfgfile = "missing-directory/config.cfg";
	CHECK(mainui_apply_esp_identity(2,argv[1],argv[2],argv[4]) != NULL && es->ssl_pki_num[0] == 1,"failed save restores identity");
	main_cfgfile = "identity.cfg";
	CHECK(guicore_init(GUICORE_HEADLESS,"Identity settings test"),"offscreen canvas");
	mui_state.ctx = calloc(1U,sizeof(*mui_state.ctx)); CHECK(mui_state.ctx,"allocate GUI");
	mu_init(mui_state.ctx);
	mui_state.ctx->text_width = mui_text_width; mui_state.ctx->text_height = mui_text_height;
	mui_filedialog_init(&mui_rom_dialog); mui_filedialog_init(&mui_dir_dialog); mui_filedialog_init(&mui_cfg_file_dialog);
	mui_open_config_page(mui_state.ctx,2); mui_refresh_visibility();
	draw(); draw();
	CHECK(mui_identity_slot == 1 && !strcmp(mui_identity_cert,argv[1]),"GUI selects active identity");
	container = mui_find_container(mui_state.ctx,"DEVICES"); CHECK(container,"devices window");
	CHECK(container->content_size.x <= container->body.w,"no horizontal overflow");
	CHECK(container->content_size.y > container->body.h,"identity controls scroll");
	container->scroll.y = 120;
	draw(); draw();
	SDL_Surface *surface = SDL_CreateRGBSurfaceFrom(canvas,640,560,32,640*4,0x00ff0000,0x0000ff00,0x000000ff,0);
	CHECK(surface,"screenshot surface"); SDL_SaveBMP(surface,"identity-settings.bmp"); SDL_FreeSurface(surface);
	guicore_quit();
	puts("PASS: identity validation, atomic save, reset persistence, GUI selection and scrolling");
	return 0;
}
