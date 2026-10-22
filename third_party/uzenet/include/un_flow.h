#ifndef UN_FLOW_H
#define UN_FLOW_H
#include "un_wire.h"
/* One automatic policy: cover the existing 1 ms worker interval at 8N1.
 * Never admit more than one complete staged reply. These macros accept a
 * constant baud or a side-effect-free variable; no AVR runtime state needed.
 * Use the actual ESP baud (or an upper bound), not a rounded nominal rate.
 * Zero is invalid for entry; the minimum below also keeps constant use safe. */
#define UN_PA7_SERVICE_MS 1
#define UN_PA7_TX_BATCH_MAX UN_ENCODED_MAX
#define UN_PA7_TX_BATCH_FOR_BAUD(baud) \
    ((baud) < 10000UL ? 1U : \
     (baud) >= UN_PA7_TX_BATCH_MAX * 10000UL ? UN_PA7_TX_BATCH_MAX : \
     ((baud) + 9999UL) / 10000UL)
/* Empty hardware FIFO may still have one byte in the shift register. */
#define UN_PA7_RX_TAIL_RESERVE(baud) (UN_PA7_TX_BATCH_FOR_BAUD(baud) + 1U)
#endif
