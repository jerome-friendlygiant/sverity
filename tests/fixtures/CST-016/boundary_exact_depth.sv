// A is an NBA output on clkb, the same root and edge as the two-stage chain B. Its structural bound is L=2 strictly later
// edges, which meets N=2: INFO Established.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --config @DIR@/boundary_exact_depth.json
// expect: CST-016 INFO
// expect-count: CST-016 INFO 2
// expect-none: CST-016 WARNING
// expect-coverage: reset-contracts COMPLETE
module top(input logic clka, clkb, rst_n, d, output logic q, r);
  logic a_rst, s1, s2;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) a_rst <= 1'b0; else a_rst <= 1'b1;
  always_ff @(posedge clkb or negedge a_rst) if (!a_rst) s1 <= 1'b0; else s1 <= 1'b1;
  always_ff @(posedge clkb or negedge a_rst) if (!a_rst) s2 <= 1'b0; else s2 <= s1;
  always_ff @(posedge clkb or negedge s2) if (!s2) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) r <= 1'b0; else r <= d;
endmodule
