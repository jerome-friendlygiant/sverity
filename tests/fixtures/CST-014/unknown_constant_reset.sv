// sverity-args: --top top --sdc @DIR@/unknown_constant_reset.sdc
// A released reset says nothing about the value registers start from, so the reset checks stay.
// expect: CST-009 WARNING
// expect-none: CST-014
// expect: CDC-003 ERROR
module top(input logic a, b, rn, d, output logic q);
  logic s;
  always_ff @(posedge a or negedge rn) if (!rn) s <= 1'b0; else s <= d;
  always_ff @(posedge b) q <= s;
endmodule
