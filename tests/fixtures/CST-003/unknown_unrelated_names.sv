// sverity-args: --top top -I@FIXTURES@/_common --sdc @DIR@/ancestor_chain_good.sdc
// Derivation alone must not establish synchrony.
// expect-none: CST-003
// expect: CST-004 WARNING
// expect-coverage: clock-relationships INCOMPLETE
module top(input logic clk, div, leaf, d, output logic q, q2);
  always_ff @(posedge clk) q <= d;
  always_ff @(posedge div) q2 <= q;
endmodule
