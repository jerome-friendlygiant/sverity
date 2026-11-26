// sverity-args: --top top
// The reset reaches q through a conditional expression, not an if: the value it forces is not analysed. The output is the register
// itself: its undecided reset is Unresolved (RDC-025), not an unreset output.
// expect-coverage: reset-extraction INCOMPLETE
// expect-unresolved: RDC-025
module top(input logic clka, rst_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n) q <= rst_n ? d : 1'b0;
endmodule
