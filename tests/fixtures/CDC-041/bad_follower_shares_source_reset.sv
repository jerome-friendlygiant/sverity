// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: always_ff @(posedge clkb or negedge rst_b_n) if (!rst_b_n) {s2, s1} => always_ff @(posedge clkb or negedge rst_a_n) if (!rst_a_n) {s2, s1}
// revert-expect-none: CDC-041 WARNING
// The synchroniser is reset apart from its source; a follower past the required depth shares the source's reset and must not hide that.
// expect: CDC-041 WARNING
module top(input logic clka, clkb, rst_a_n, rst_b_n, d, output logic q);
  logic a_q, s1, s2, s3;
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_b_n) if (!rst_b_n) {s2, s1} <= '0; else {s2, s1} <= {s1, a_q};
  always_ff @(posedge clkb or negedge rst_a_n) if (!rst_a_n) s3 <= 1'b0; else s3 <= s2;
  assign q = s3;
endmodule
