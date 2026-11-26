// The released register q is also reset by rst_n, a competing asynchronous reset. Its reset is not the single plain test of
// the synchroniser output, so the witness is Unresolved and reset-contracts INCOMPLETE (CST-016 B5).
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --config @DIR@/unknown_receiver_extra_reset.json
// expect: CST-016 WARNING
// expect-coverage: reset-contracts INCOMPLETE
module top(input logic clka, clkb, rst_n, d, output logic q, r);
  logic a_rst, s1, s2;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) a_rst <= 1'b0; else a_rst <= 1'b1;
  always_ff @(posedge clkb or negedge a_rst) if (!a_rst) s1 <= 1'b0; else s1 <= 1'b1;
  always_ff @(posedge clkb or negedge a_rst) if (!a_rst) s2 <= 1'b0; else s2 <= s1;
  always_ff @(posedge clkb or negedge s2 or negedge rst_n) if (!s2 || !rst_n) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) r <= 1'b0; else r <= d;
endmodule
