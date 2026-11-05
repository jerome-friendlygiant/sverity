// sverity-args: --top top
// A struct pattern moves each field on its own, like a concatenation: r comes only from b, so there is no loop through x.
// expect-none: LNT-012
module top(input logic a, b, output logic y);
  typedef struct packed { logic d; logic r; } fields_t;
  fields_t s;
  wire x = s.r & a;
  assign s = '{d: x, r: b};
  assign y = s.d;
endmodule
