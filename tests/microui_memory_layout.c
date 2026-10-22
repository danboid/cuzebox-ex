#include <assert.h>
#include <stdlib.h>
#include "microui.h"
int main(void){
 mu_Context *ctx=calloc(1,sizeof(*ctx)); int widths[18];
 mu_init(ctx); ctx->layout_stack.idx=1;
 ctx->layout_stack.items[0].body=mu_rect(0,0,800,600);
 for(int i=0;i<18;i++)widths[i]=20+i;
 mu_layout_row(ctx,18,widths,20);
 for(int i=0;i<18;i++){mu_Rect r=mu_layout_next(ctx);assert(r.w==20+i);}
 assert(ctx->layout_stack.items[0].item_index==18);
 free(ctx);return 0;
}
