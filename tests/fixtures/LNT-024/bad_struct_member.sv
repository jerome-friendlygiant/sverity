// sverity-args: --top top
// expect: LNT-024 WARNING 4
typedef struct packed { logic p; logic q; } pair_t;
module top(input logic a, output pair_t y);
  assign y.p = a;
endmodule
