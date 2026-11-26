// A three-stage reset synchroniser on clkb, released under the primary input rst_a_n. The order declares rst_a_n before the
// chain root with N=2. The conservative bound is L-1=2 strictly later edges, which meets N: INFO Established.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --config @DIR@/good_cascaded_chain.json
// expect: CST-016 INFO
// expect-count: CST-016 INFO 2
// expect-none: CST-016 WARNING
// expect-coverage: reset-contracts COMPLETE
module top(input logic clka, clkb, rst_a_n, d, output logic q, r);
  logic s1, s2, s3;
  always_ff @(posedge clkb or negedge rst_a_n) if (!rst_a_n) s1 <= 1'b0; else s1 <= 1'b1;
  always_ff @(posedge clkb or negedge rst_a_n) if (!rst_a_n) s2 <= 1'b0; else s2 <= s1;
  always_ff @(posedge clkb or negedge rst_a_n) if (!rst_a_n) s3 <= 1'b0; else s3 <= s2;
  always_ff @(posedge clkb or negedge s3) if (!s3) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) r <= 1'b0; else r <= d;
endmodule
