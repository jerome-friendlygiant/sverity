// sverity-args: --top top --sdc @DIR@/unknown_initial_write.sdc
// An initial block writes m, so it is not undriven and the constraint cannot be the source of its value.
// expect: CST-009 WARNING
// expect-none: CST-014
// expect: CDC-028 ERROR
module top(input logic a, b, output logic q);
  logic m;
  initial m = 1'b1;
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) if (m) q <= source_q;
endmodule
