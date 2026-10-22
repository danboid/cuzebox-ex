/* Controller gameplay setup. Included by mui_integration.c after its UI helpers.
** Draft state keeps Apply/Cancel transactional without changing savestate layouts. */
static struct {
 boole active;
 ginput_controller_options_t options[GINPUT_MAX_CONTROLLERS];
 cu_state_vdev_t devices;
 cu_state_multitap_t topology;
 auint destination;
 int learn_target;
 boole learn_all;
 boole released;
 boole release_guard;
 char status[160];
} mui_padmap;

static char const* const mui_padmap_targets[12] = {
 "B", "Y", "SELECT", "START", "UP", "DOWN", "LEFT", "RIGHT", "A", "X", "L", "R"
};
/* Sources use the existing normalized SNES-position bits; labels describe SDL positions. */
static char const* const mui_padmap_sources[16] = {
 "SOUTH (A)", "WEST (X)", "BACK / GUIDE", "START", "UP / STICK UP", "DOWN / STICK DOWN",
 "LEFT / STICK LEFT", "RIGHT / STICK RIGHT", "EAST (B)", "NORTH (Y)", "LEFT SHOULDER",
 "RIGHT SHOULDER", "LEFT TRIGGER", "RIGHT TRIGGER", "LEFT STICK CLICK", "RIGHT STICK CLICK"
};

static cu_vdev_t* mui_padmap_device(void)
{
 auint port = mui_padmap.destination / 4U, slot = mui_padmap.destination % 4U;
 auint id = mui_padmap.topology.port[port].slot_vdev[slot];
 if (id >= CU_VDEV_MAX || !mui_padmap.devices.list[id].used){ return NULL; }
 return &mui_padmap.devices.list[id];
}

static void mui_padmap_begin(void)
{
 memset(&mui_padmap, 0, sizeof(mui_padmap));
 cu_vdev_get_state(&mui_padmap.devices);
 cu_multitap_get_state(&mui_padmap.topology);
 for (auint i = 0; i < GINPUT_MAX_CONTROLLERS; ++i){ mui_padmap.options[i] = ginput_get_controller_options(i); }
 mui_padmap.learn_target = -1;
 mui_padmap.active = TRUE;
 ginput_set_mapping_capture(TRUE);
 snprintf(mui_padmap.status, sizeof(mui_padmap.status), "Choose a destination and controller.");
}

static boole mui_padmap_create(void)
{
 cu_vdev_t* v = mui_padmap_device();
 auint port = mui_padmap.destination / 4U, slot = mui_padmap.destination % 4U;
 if (v){ return v->type == CU_VDEV_TYPE_PAD16; }
 for (auint id = 0; id < CU_VDEV_MAX; ++id){
  v = &mui_padmap.devices.list[id];
  if (v->used){ continue; }
  memset(v, 0, sizeof(*v)); v->used = TRUE; v->type = CU_VDEV_TYPE_PAD16;
  v->haptic_binding = CU_VDEV_INVALID; v->haptic_accept_any = TRUE;
  v->sm_scale_x_pct = v->sm_scale_y_pct = 100U;
  snprintf(v->name, sizeof(v->name), "Port %u Slot %u pad", (unsigned)(port + 1U), (unsigned)(slot + 1U));
  for (auint b = 0; b < CU_VDEV_MAX_BINDINGS; ++b){ v->binding[b].host_index = CU_VDEV_INVALID; }
  mui_padmap.topology.port[port].slot_vdev[slot] = id;
  return TRUE;
 }
 snprintf(mui_padmap.status, sizeof(mui_padmap.status), "No free virtual devices. Remove an unused device in VIRT DEV.");
 return FALSE;
}

static void mui_padmap_learn(int target, boole all)
{
 mui_padmap.learn_target = target; mui_padmap.learn_all = all;
 mui_padmap.released = FALSE; mui_padmap.release_guard = TRUE;
}

static void mui_padmap_poll(void)
{
 cu_vdev_t* v = mui_padmap_device();
 auint raw = 0U;
 if (!v || v->binding[0].type != CU_VDEV_HOST_GAMECONTROLLER || v->binding[0].host_index >= GINPUT_MAX_CONTROLLERS){ return; }
 if (!ginput_host_get_controller_buttons_ex(v->binding[0].host_index, &raw, mui_padmap.options[v->binding[0].host_index])){
  if (mui_padmap.learn_target >= 0){
   mui_padmap.learn_target = -1; mui_padmap.release_guard = FALSE;
   snprintf(mui_padmap.status, sizeof(mui_padmap.status), "Controller disconnected. Reconnect it to continue.");
  }
  return;
 }
 if (!raw){ mui_padmap.released = TRUE; mui_padmap.release_guard = FALSE; return; }
 if (mui_padmap.learn_target < 0 || !mui_padmap.released){ return; }
 /* Reject simultaneous inputs, including diagonals, until controls return to neutral. */
 mui_padmap.released = FALSE; mui_padmap.release_guard = TRUE;
 if ((raw & (raw - 1U)) != 0U){ return; }
 auint target = 1U << mui_padmap.learn_target;
 for (auint i = 0; i < CU_VDEV_REMAP_SLOTS; ++i){
  v->binding[0].map_low16[i] &= ~target;
  if (raw == (1U << i)){ v->binding[0].map_low16[i] = target; }
 }
 snprintf(mui_padmap.status, sizeof(mui_padmap.status), "Mapped %s. Release the control.", mui_padmap_targets[mui_padmap.learn_target]);
 if (mui_padmap.learn_all && mui_padmap.learn_target < 11){ ++mui_padmap.learn_target; }
 else { mui_padmap.learn_target = -1; }
}

static boole mui_padmap_duplicate(auint host)
{
 auint current = mui_padmap.topology.port[mui_padmap.destination / 4U].slot_vdev[mui_padmap.destination % 4U];
 for (auint p = 0; p < 2; ++p){
  auint count = mui_padmap.topology.port[p].tap_present ? 4U : 1U;
  for (auint s = 0; s < count; ++s){
   auint id = mui_padmap.topology.port[p].slot_vdev[s];
   if (p * 4U + s == mui_padmap.destination || id >= CU_VDEV_MAX){ continue; }
   if (id == current){ return TRUE; }
   cu_vdev_t const* other = &mui_padmap.devices.list[id];
   for (auint b = 0; b < CU_VDEV_MAX_BINDINGS; ++b){
    if (other->used && other->binding[b].type == CU_VDEV_HOST_GAMECONTROLLER && other->binding[b].host_index == host){ return TRUE; }
   }
  }
 }
 return FALSE;
}

static void mui_padmap_apply(void)
{
 for (auint i = 0; i < GINPUT_MAX_CONTROLLERS; ++i){ ginput_set_controller_options(i, mui_padmap.options[i]); }
 cu_vdev_set_state(&mui_padmap.devices);
 /* Preserve live protocol state: settings do not restore stale activity / command state. */
 for (auint p = 0; p < 2U; ++p){
  cu_multitap_set_tap_present(p, mui_padmap.topology.port[p].tap_present);
  for (auint s = 0; s < 4U; ++s){ cu_multitap_set_slot_vdev(p, s, mui_padmap.topology.port[p].slot_vdev[s]); }
 }
 mainui_touch_config();
 mui_vdev_editor_synced = FALSE;
}

static void mui_build_controller_mapping_window(mu_Context* ctx)
{
 char text[192];
 if (!mui_window_is_open(ctx, MUI_WIN_CONTROLLER_MAPPING)){
  if (mui_padmap.active){ mui_padmap.active = FALSE; ginput_set_mapping_capture(FALSE); }
  return;
 }
 if (!mui_padmap.active){ mui_padmap_begin(); }
 mui_padmap_poll();
 if (!mui_window_begin(ctx, MUI_WIN_CONTROLLER_MAPPING)){ return; }
 mu_layout_row(ctx, 2, (int[]){ 220, -1 }, 24);
 if (mu_button(ctx, "APPLY")){ mui_padmap_apply(); mui_window_close(ctx, MUI_WIN_CONTROLLER_MAPPING); }
 if (mu_button(ctx, "CANCEL")){ mui_window_close(ctx, MUI_WIN_CONTROLLER_MAPPING); }
 mu_layout_row(ctx, 2, (int[]){ 236, -1 }, 20);
 for (auint p = 0; p < 2; ++p){
  int enabled = mui_padmap.topology.port[p].tap_present != 0U;
  snprintf(text, sizeof(text), "PORT %u MULTITAP", (unsigned)(p + 1U));
  if (mu_checkbox(ctx, text, &enabled)){
   mui_padmap.topology.port[p].tap_present = enabled;
   if (!enabled && mui_padmap.destination / 4U == p){ mui_padmap.destination = p * 4U; }
   mui_padmap.learn_target = -1;
  }
 }
 {
  char labels[8][40]; char const* choices[8]; auint destinations[8], count = 0, choice = 0;
  for (auint p = 0; p < 2; ++p){
   for (auint s = 0; s < (mui_padmap.topology.port[p].tap_present ? 4U : 1U); ++s){
    if (mui_padmap.topology.port[p].tap_present){ snprintf(labels[count], sizeof(labels[count]), "PORT %u / SLOT %u", (unsigned)(p + 1U), (unsigned)(s + 1U)); }
    else { snprintf(labels[count], sizeof(labels[count]), "PORT %u", (unsigned)(p + 1U)); }
    destinations[count] = p * 4U + s; choices[count] = labels[count];
    if (destinations[count] == mui_padmap.destination){ choice = count; } ++count;
   }
  }
  if (mui_labeled_choice_popup(ctx, "DESTINATION", "padmap_destination", choices[choice], choices, count, &choice, 160, 290)){
   mui_padmap.destination = destinations[choice]; mui_padmap.learn_target = -1;
  }
 }
 cu_vdev_t* v = mui_padmap_device();
 if (v && v->type != CU_VDEV_TYPE_PAD16){
  mu_layout_row(ctx, 1, (int[]){ -1 }, 20); mu_label(ctx, "Mouse/keyboard slot: use VIRT DEV.");
 }else{
  char labels[GINPUT_MAX_CONTROLLERS + 1U][100]; char const* choices[GINPUT_MAX_CONTROLLERS + 1U];
  auint choice = v && v->binding[0].type == CU_VDEV_HOST_GAMECONTROLLER && v->binding[0].host_index < GINPUT_MAX_CONTROLLERS ? v->binding[0].host_index + 1U : 0U;
  snprintf(labels[0], sizeof(labels[0]), "NONE"); choices[0] = labels[0];
  for (auint i = 0; i < GINPUT_MAX_CONTROLLERS; ++i){
   snprintf(labels[i + 1U], sizeof(labels[0]), "%u: %.70s", (unsigned)(i + 1U), ginput_get_controller_present(i) ? ginput_get_controller_name(i) : "DISCONNECTED");
   choices[i + 1U] = labels[i + 1U];
  }
  if (mui_labeled_choice_popup(ctx, "CONTROLLER", "padmap_host", choices[choice], choices, GINPUT_MAX_CONTROLLERS + 1U, &choice, 160, 290) && mui_padmap_create()){
   v = mui_padmap_device();
   if (v->binding[0].type != CU_VDEV_HOST_GAMECONTROLLER){
    for (auint i = 0; i < 16; ++i){ v->binding[0].map_low16[i] = i < 12U ? 1U << i : 0U; }
   }
   auint flags = choice ? (CU_VDEV_BIND_BUTTONS | (v->binding[0].flags & CU_VDEV_BIND_HAPTIC)) : 0U;
   flags = mui_binding_haptic_defaults(v, 0U, choice ? CU_VDEV_HOST_GAMECONTROLLER : CU_VDEV_HOST_NONE,
       choice ? choice - 1U : CU_VDEV_INVALID, flags);
   v->binding[0].type = choice ? CU_VDEV_HOST_GAMECONTROLLER : CU_VDEV_HOST_NONE;
   v->binding[0].host_index = choice ? choice - 1U : CU_VDEV_INVALID;
   v->binding[0].flags = flags;
   mui_padmap.learn_target = -1;
  }
  mu_layout_row(ctx, 1, (int[]){ -1 }, 18);
  if (v && v->binding[0].type == CU_VDEV_HOST_GAMECONTROLLER){
   if (mui_padmap_duplicate(v->binding[0].host_index)){ mu_label(ctx, "WARNING: shared controller assignment."); }
   else if (v->binding[1].type != CU_VDEV_HOST_NONE){ mu_label(ctx, "Extra binding: see VIRT DEV."); }
   else { mu_label(ctx, "Select a row, then press input."); }
   auint host = v->binding[0].host_index;
   if (host >= GINPUT_MAX_CONTROLLERS){ mu_label(ctx, "Invalid host slot. Assign a controller above."); mui_window_end(ctx, MUI_WIN_CONTROLLER_MAPPING); return; }
   auint raw = 0U, mapped = 0U;
   (void)ginput_host_get_controller_buttons_ex(v->binding[0].host_index, &raw, mui_padmap.options[v->binding[0].host_index]);
   for (auint i = 0; i < 16U; ++i){ if (raw & (1U << i)){ mapped |= v->binding[0].map_low16[i]; } }
   mu_layout_row(ctx, 3, (int[]){ 190, 64, -1 }, 22);
   if (mu_button(ctx, "CONFIGURE ALL")){ mui_padmap_learn(0, TRUE); }
   if (mu_button(ctx, "SKIP")){
    if (mui_padmap.learn_target >= 0){
     auint mask = 1U << mui_padmap.learn_target;
     for (auint i = 0; i < 16; ++i){ v->binding[0].map_low16[i] &= ~mask; }
     if (mui_padmap.learn_all && mui_padmap.learn_target < 11){ mui_padmap_learn(mui_padmap.learn_target + 1, TRUE); }
     else { mui_padmap.learn_target = -1; }
    }
   }
   if (mu_button(ctx, "STOP LEARNING")){ mui_padmap.learn_target = -1; }
   for (auint t = 0; t < 12; ++t){
    char source[100] = "UNASSIGNED"; auint found = 0;
    for (auint i = 0; i < 16U; ++i){ if (v->binding[0].map_low16[i] & (1U << t)){ if (!found){ snprintf(source, sizeof(source), "%s", mui_padmap_sources[i]); } ++found; } }
    snprintf(text, sizeof(text), "%s%s: %s%s", (mapped & (1U << t)) ? "* " : "", mui_padmap_targets[t], mui_padmap.learn_target == (int)t ? "PRESS INPUT..." : source, found > 1 ? " (+MORE)" : "");
    mu_layout_row(ctx, 2, (int[]){ -96, 84 }, 18); mu_push_id(ctx, &t, sizeof(t));
    if (mu_button(ctx, text)){ mui_padmap_learn((int)t, FALSE); }
    if (mu_button(ctx, "CLEAR")){ for (auint i = 0; i < 16; ++i){ v->binding[0].map_low16[i] &= ~(1U << t); } mui_padmap.learn_target = -1; }
    mu_pop_id(ctx);
   }
   mu_layout_row(ctx, 1, (int[]){ -1 }, 22);
   if (mu_button(ctx, "RESTORE DEFAULT BUTTONS")){
    for (auint i = 0; i < 16; ++i){ v->binding[0].map_low16[i] = i < 12U ? 1U << i : 0U; }
    mui_padmap.learn_target = -1;
   }
   mu_layout_row(ctx, 1, (int[]){ -1 }, 20);
   int stick = mui_padmap.options[host].left_stick;
   if (mu_checkbox(ctx, "LEFT STICK AS D-PAD", &stick)){ mui_padmap.options[host].left_stick = stick; }
   static char const* const dz_names[] = {"10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%"};
   auint dz = mui_padmap.options[host].deadzone / 10U;
   dz = dz < 1U ? 0U : dz > 9U ? 8U : dz - 1U;
   if (mui_labeled_choice_popup(ctx, "DEAD ZONE", "padmap_deadzone", dz_names[dz], dz_names, 9U, &dz, 160, 120)){ mui_padmap.options[host].deadzone = (dz + 1U) * 10U; }
  }else{ mu_label(ctx, "Choose a physical controller above."); }
 }
 mu_layout_row(ctx, 1, (int[]){ -1 }, 18); mu_label(ctx, mui_padmap.status);
 mui_window_end(ctx, MUI_WIN_CONTROLLER_MAPPING);
}
