// sverity-args: --top top --sdc @DIR@/unknown.sdc
// Dual-edge behavior is not modeled, so this remains unresolved even though the input clock is declared.
// expect-count: CDC-043 WARNING 1
// expect-unresolved: CDC-043
// expect-coverage: clock-relationships INCOMPLETE
module top(input logic clk, d, output logic q);
  logic both_edges;
  always @(posedge clk or negedge clk) both_edges <= d;
  always_ff @(posedge clk) q <= both_edges;
endmodule
