// A is an NBA output on clkb and B is released on the same edge, but the chain has two stages (s3 copies s1). The release
// is on edge 2 < N=3: WARNING Established. The revert restores the third stage (s3 copies s2), which gives INFO Established.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --config @DIR@/mut_short_chain.json
// expect: CST-016 WARNING
// expect-count: CST-016 WARNING 1
// revert: // always_ff @(posedge clkb or negedge a_rst) if (!a_rst) s2 <= 1'b0; else s2 <= s1; => always_ff @(posedge clkb or negedge a_rst) if (!a_rst) s2 <= 1'b0; else s2 <= s1;
// revert: else s3 <= s1; => else s3 <= s2;
// revert-expect-none: CST-016 WARNING
module top(input logic clka, clkb, rst_n, d, output logic q, r);
  logic a_rst, s1, s2, s3;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) a_rst <= 1'b0; else a_rst <= 1'b1;
  always_ff @(posedge clkb or negedge a_rst) if (!a_rst) s1 <= 1'b0; else s1 <= 1'b1;
  // always_ff @(posedge clkb or negedge a_rst) if (!a_rst) s2 <= 1'b0; else s2 <= s1;
  always_ff @(posedge clkb or negedge a_rst) if (!a_rst) s3 <= 1'b0; else s3 <= s1;
  always_ff @(posedge clkb or negedge s3) if (!s3) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) r <= 1'b0; else r <= d;
endmodule
