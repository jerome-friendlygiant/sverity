// A two-stage chain on clkb under the primary input rst_a_n, which is not an NBA output on clkb. The conservative bound is
// L-1=1 edge, below N=2, and the RTL does not decide the order: WARNING Unresolved, reset-contracts INCOMPLETE.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --config @DIR@/unknown_conservative_bound.json
// expect: CST-016 WARNING
// expect-count: CST-016 WARNING 1
// expect-coverage: reset-contracts INCOMPLETE
module top(input logic clka, clkb, rst_a_n, d, output logic q, r);
  logic s1, s2;
  always_ff @(posedge clkb or negedge rst_a_n) if (!rst_a_n) s1 <= 1'b0; else s1 <= 1'b1;
  always_ff @(posedge clkb or negedge rst_a_n) if (!rst_a_n) s2 <= 1'b0; else s2 <= s1;
  always_ff @(posedge clkb or negedge s2) if (!s2) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) r <= 1'b0; else r <= d;
endmodule
