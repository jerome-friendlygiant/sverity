// sverity-args: --top top --sdc @DIR@/unknown_constant_clock.sdc
// expect: CST-009 WARNING
// expect-none: CST-014
// expect: CDC-003 ERROR
module top(input logic a, b, output logic q);
  logic source_q;
  always_ff @(posedge a) source_q <= ~source_q;
  always_ff @(posedge b) q <= source_q;
endmodule
