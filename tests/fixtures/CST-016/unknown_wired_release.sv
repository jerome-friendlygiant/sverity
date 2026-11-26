// The released reset b_rst is a wor net: a continuous driver from the synchroniser and a constant one. A second driver
// can keep it deasserted, so the witness is Unresolved and reset-contracts INCOMPLETE (CST-016 B4).
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --config @DIR@/unknown_wired_release.json
// expect: CST-016 WARNING
// expect-coverage: reset-contracts INCOMPLETE
module top(input logic clka, clkb, rst_n, d, output logic q, r);
  logic a_rst, s1, s2; wor b_rst;
  assign b_rst = s2; assign b_rst = 1'b1;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) a_rst <= 1'b0; else a_rst <= 1'b1;
  always_ff @(posedge clkb or negedge a_rst) if (!a_rst) s1 <= 1'b0; else s1 <= 1'b1;
  always_ff @(posedge clkb or negedge a_rst) if (!a_rst) s2 <= 1'b0; else s2 <= s1;
  always_ff @(posedge clkb or negedge b_rst) if (!b_rst) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) r <= 1'b0; else r <= d;
endmodule
