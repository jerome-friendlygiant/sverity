// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: ALL rst_b_n => rst_a_n
// revert-expect-none: CDC-041 WARNING
// The multi-bit synchroniser is reset by a different reset than its source.
// expect: CDC-041 WARNING
module top(input logic clka, clkb, rst_a_n, rst_b_n, input logic [1:0] d, output logic [1:0] q);
  logic [1:0] a_q, s1, s2;
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) a_q <= '0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_b_n) if (!rst_b_n) {s2, s1} <= '0; else {s2, s1} <= {s1, a_q};
  assign q = s2;
endmodule
