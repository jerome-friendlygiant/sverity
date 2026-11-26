// A one-stage synchroniser releases b after a with one coincident edge allowed (structural bound 1). The chain is shorter
// than the RDC-006.min_stages policy (2), so the witness is Unresolved and reset-contracts INCOMPLETE (CST-016 chain depth).
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --config @DIR@/unknown_one_stage_chain.json
// expect: CST-016 WARNING
// expect-coverage: reset-contracts INCOMPLETE
module top(input logic clka, clkb, rst_n, d, output logic q, r);
  logic a_rst, s1;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) a_rst <= 1'b0; else a_rst <= 1'b1;
  always_ff @(posedge clkb or negedge a_rst) if (!a_rst) s1 <= 1'b0; else s1 <= 1'b1;
  always_ff @(posedge clkb or negedge s1) if (!s1) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) r <= 1'b0; else r <= d;
endmodule
