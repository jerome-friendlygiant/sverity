// sverity-args: --top top
// Unknown/mutation: sequential block with two clock edges has unidentified clock (CST-001 WARNING Unresolved).
// expect: CST-001 WARNING
// expect-coverage: clock-extraction INCOMPLETE
// revert: always_ff @(posedge clk1 or posedge clk2) q <= d; => always_ff @(posedge clk1) q <= d;
// revert-expect-none: CST-001 WARNING
module top(input logic clk1, clk2, d, output logic q);
  always_ff @(posedge clk1 or posedge clk2) q <= d;
endmodule
