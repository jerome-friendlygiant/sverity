// The RTL witness matches (a three-stage chain on clkb under rst_a_n, N=2 with the bound L-1=2), but no initial asserted
// state clause covers rst_a_n: WARNING Unresolved, reset-contracts INCOMPLETE.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --config @DIR@/unknown_no_initial_state.json
// expect: CST-016 WARNING
// expect-count: CST-016 WARNING 1
// expect-confidence: CST-016 unresolved
// expect-coverage: reset-contracts INCOMPLETE
module top(input logic clka, clkb, rst_a_n, d, output logic q, r);
  logic s1, s2, s3;
  always_ff @(posedge clkb or negedge rst_a_n) if (!rst_a_n) s1 <= 1'b0; else s1 <= 1'b1;
  always_ff @(posedge clkb or negedge rst_a_n) if (!rst_a_n) s2 <= 1'b0; else s2 <= s1;
  always_ff @(posedge clkb or negedge rst_a_n) if (!rst_a_n) s3 <= 1'b0; else s3 <= s2;
  always_ff @(posedge clkb or negedge s3) if (!s3) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) r <= 1'b0; else r <= d;
endmodule
