// The declared polarity (active high) contradicts the only reset use in the RTL (active low): a decided conflict.
// The state clause that depends on the conflicting source is rejected.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --config @DIR@/polarity_high.json
// expect: CST-016 WARNING
// expect-count: CST-016 WARNING 2
// expect-coverage: reset-contracts INCOMPLETE
module top(input logic clka, clkb, rst_a_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) q <= 1'b0; else q <= d;
endmodule
