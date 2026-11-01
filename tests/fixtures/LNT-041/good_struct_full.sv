// sverity-args: --top top
// expect-none: LNT-041
typedef struct packed { logic p; logic [1:0] q; } pair_t;
module top(input logic a, input logic [1:0] b, output logic [2:0] y);
  pair_t s;
  assign s.p = a;
  assign s.q = b;
  assign y = s;
endmodule
