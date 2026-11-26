// The release root b_rst is a mux of the chain output b_q and the primary input rst_n, selected by an opaque input sel. The
// release is not a single exact reset flop chain, so the order is WARNING Unresolved and reset-contracts is INCOMPLETE.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --config @DIR@/unknown_release_bypass.json
// expect: CST-016 WARNING
// expect-count: CST-016 WARNING 1
// expect-confidence: CST-016 unresolved
// expect-coverage: reset-contracts INCOMPLETE
module top(input logic clka, clkb, rst_n, sel, d, output logic q, r);
  logic a_rst, s1, b_q, b_rst;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) a_rst <= 1'b0; else a_rst <= 1'b1;
  always_ff @(posedge clkb or negedge a_rst) if (!a_rst) s1 <= 1'b0; else s1 <= 1'b1;
  always_ff @(posedge clkb or negedge a_rst) if (!a_rst) b_q <= 1'b0; else b_q <= s1;
  assign b_rst = sel ? rst_n : b_q;
  always_ff @(posedge clkb or negedge b_rst) if (!b_rst) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) r <= 1'b0; else r <= d;
endmodule
