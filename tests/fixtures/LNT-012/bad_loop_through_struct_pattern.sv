// sverity-args: --top top
// r comes from d, and d from r through x: a real loop written as a struct pattern.
// expect: LNT-012 ERROR
module top(input logic a, output logic y);
  typedef struct packed { logic d; logic r; } fields_t;
  fields_t s;
  wire x = s.r & a;
  assign s = '{d: x, r: s.d};
  assign y = s.d;
endmodule
