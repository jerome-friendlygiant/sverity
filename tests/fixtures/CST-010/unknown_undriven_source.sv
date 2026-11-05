// sverity-args: --top top
// Lookalike of good_constant_and_loop_index_sources: `floating` truly has no driver, so its value is unknown.
// expect: CST-010 WARNING
module top(input logic clk, output logic q);
  logic floating;
  always_ff @(posedge clk) q <= floating;
endmodule
