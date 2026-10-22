#include <assert.h>
#include <stdio.h>
#include "../../microui/microui.h"

static mu_Context ctx;
static mu_Container root;

int main(void) {
  mu_Rect a = {10, 10, 50, 20}, b = {10, 40, 50, 20};
  mu_init(&ctx);
  ctx.container_stack.items[ctx.container_stack.idx++] = &root;
  ctx.hover_root = &root;
  ctx.clip_stack.items[ctx.clip_stack.idx++] = mu_rect(0, 0, 100, 100);

  mu_input_mousemove(&ctx, 20, 15);
  mu_update_control(&ctx, 1, a, 0);
  assert(ctx.hover == 1);
  /* Move and press before the next UI frame. */
  mu_input_mousedown(&ctx, 20, 45, MU_MOUSE_LEFT);
  mu_update_control(&ctx, 1, a, 0);
  assert(ctx.focus != 1);
  mu_update_control(&ctx, 2, b, 0);
  assert(ctx.focus == 2);

  /* A held drag retains focus when leaving its original control. */
  ctx.mouse_pressed = 0;
  mu_input_mousemove(&ctx, 90, 90);
  mu_update_control(&ctx, 2, b, 0);
  assert(ctx.focus == 2);
  mu_input_mouseup(&ctx, 90, 90, MU_MOUSE_LEFT);
  mu_update_control(&ctx, 2, b, 0);
  assert(ctx.focus == 0);

  /* Clicking blank space must not activate the previously hovered control. */
  mu_input_mousemove(&ctx, 20, 15);
  mu_update_control(&ctx, 1, a, 0);
  mu_input_mousedown(&ctx, 90, 90, MU_MOUSE_LEFT);
  mu_update_control(&ctx, 1, a, 0);
  assert(ctx.focus == 0);
  puts("PASS: current pointer hit, stale hover rejection, drag and release");
  return 0;
}
