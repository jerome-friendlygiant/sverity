// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Each bit has its own 2-flop synchroniser, but a bus can still be captured inconsistent: WARNING + proof obligation.
// expect: CDC-009 WARNING
// expect-none: CDC-004
module top(input logic clka, clkb, rst_n, input logic [1:0] d, output logic [1:0] q);
  logic [1:0] a_q, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= '0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, a_q};
  assign q = s2;
endmodule
