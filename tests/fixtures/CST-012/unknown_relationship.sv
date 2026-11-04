// sverity-args: --top top --sdc @DIR@/unknown.sdc
// expect: CST-012 WARNING
// expect-none: CST-012 INFO
// expect-coverage: constraints INCOMPLETE
// revert: posedge clk_b => posedge clk_a
// revert-expect-none: CST-012 WARNING
module top(input logic clk_a, clk_b, output logic q);
 always_ff @(posedge clk_b) q <= ~q;
endmodule
