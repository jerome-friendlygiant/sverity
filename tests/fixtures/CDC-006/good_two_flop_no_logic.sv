// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect-none: CDC-006
// Near miss of bad_logic_between_stages.sv: the logic (s1 & ~s1) between the two stages is removed, so s2 is a plain register of s1.
module top(input logic clka, clkb, rst_n, d, en, output logic q);
  logic a_q, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) s1 <= 1'b0; else s1 <= a_q;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) s2 <= 1'b0; else s2 <= s1;
  assign q = s2;
endmodule
