// sverity-args: --top top
// expect: LNT-041 WARNING 7
// revert: assign s.p = a; => assign s = {a, a};
// revert-expect-none: LNT-041
typedef struct packed { logic p; logic q; } pair_t;
module top(input logic a, output logic y);
  pair_t s;
  assign s.p = a;
  assign y = s.p;
endmodule
