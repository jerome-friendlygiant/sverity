// sverity-args: --top top
// No constraints: the relationship is UNKNOWN. Must be a WARNING (unresolved), never ERROR and never silent.
// expect: CDC-003 WARNING
// expect: CST-004 WARNING
// expect-none: CDC-003 ERROR
// expect-coverage: clock-relationships INCOMPLETE
// expect-coverage: constraints INCOMPLETE
module top(input logic clka, clkb, rst_n, d, output logic q);
  logic a_q, b_q;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) b_q <= 1'b0; else b_q <= a_q;
  assign q = b_q;
endmodule
