#!/usr/bin/env python3
"""Guard against polling-only UARTs triggering the USART TX interrupt."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[2]
source = (root / "cu_avr.c").read_text(encoding="utf-8")

pattern = re.compile(
    r"UCSR0A.*TXC0.*&&.*UCSR0B.*TXCIE0.*?VECT_USART0_TX",
    re.DOTALL,
)
if not pattern.search(source):
    print("UART IRQ gate regression: TXC0 is not visibly gated by TXCIE0", file=sys.stderr)
    raise SystemExit(1)

print("uart TX-complete IRQ gate OK: TXC0 requires TXCIE0")
