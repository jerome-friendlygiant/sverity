// sverity-args: --top top
// expect: CDC-008 WARNING
// expect-unresolved: CDC-008
// expect-coverage: clock-relationships INCOMPLETE
module top(input logic clk, a, b, output logic q);
  always_ff @(posedge clk) q <= a & b;
endmodule
