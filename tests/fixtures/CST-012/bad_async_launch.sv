// sverity-args: --top top --sdc @DIR@/bad_async_launch.sdc
// expect-none: CST-009
// An asynchronous output contract is a boundary crossing.
// expect: CST-012 ERROR
// revert: posedge clk_b => posedge clk_a
// revert-expect-none: CST-012 ERROR
module top(input logic clk_a, clk_b, output logic q);
  always_ff @(posedge clk_b) q <= ~q;
endmodule
