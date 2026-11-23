// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Boundary: supported SDC subset commands (create_clock + set_clock_groups) emit no CST-009 warning.
// expect-none: CST-009
module top(input logic clka, clkb, d, output logic q);
  logic a_q;
  always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb) q <= a_q;
endmodule
