// sverity-args: --top top
// Two tri-state drivers with unrelated enables: whether they are ever enabled together is not provable here, so the overlap is
// reported as unresolved (never silently accepted as a bus).
// expect: LNT-009 WARNING 6
module top(input logic a, b, en_a, en_b, output wire t);
  assign t = en_a ? a : 1'bz;
  assign t = en_b ? b : 1'bz;
endmodule
