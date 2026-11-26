// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// The register is clocked by a logic-generated clock, which the clock extraction cannot identify, and it has no reset. The rule has no Unknown outcome: the unreset output is still decided as one WARNING, and the clock gap is reported as INCOMPLETE scope.
// expect-count: RDC-025 WARNING 1
// expect-coverage: clock-extraction INCOMPLETE
module top(input logic clka, rst_n, d, output logic q);
  logic clk_g;
  assign clk_g = clka & rst_n;
  always_ff @(posedge clk_g) q <= d;
endmodule
