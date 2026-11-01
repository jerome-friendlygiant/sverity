// sverity-args: --top top
// expect: LNT-041 WARNING 5
typedef struct packed { logic p; logic q; } pair_t;
module top(input logic a, output logic y);
  pair_t s;
  assign s.p = a;
  assign y = s.p;
endmodule
