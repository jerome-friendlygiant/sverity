// sverity-args: --top top --sdc @DIR@/partial_contract.sdc
// q[0] has set_output_delay while q[1] carries a synchronized CDC value without a capture contract: CST-012 must warn and mark constraints INCOMPLETE.
// expect: CST-012 WARNING 13
// expect-unresolved: CST-012 13
// expect-coverage: constraints INCOMPLETE
module top(input logic clka, clkb, d0, d1, output logic [1:0] q);
  logic b0;
  logic a1, s1_1, s2_1;
  always_ff @(posedge clkb) b0 <= d0;
  assign q[0] = b0;
  always_ff @(posedge clka) a1 <= d1;
  always_ff @(posedge clkb) begin s1_1 <= a1; s2_1 <= s1_1; end
  assign q[1] = s2_1;
endmodule
