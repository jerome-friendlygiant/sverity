// sverity-args: --top top
// The region covers an operand of a continuous assignment: simulation drives a ^ b, synthesis drives a.
// expect: LNT-083 WARNING 6
module top(input logic a, b, output logic v);
  assign v = a
  // synopsys translate_off
     ^ b
  // synopsys translate_on
  ;
endmodule
