// sverity-args: --top top --sdc @DIR@/async.sdc
// expect: CST-012 INFO
// expect-count: CST-012 INFO 1
// expect-none: CST-012 WARNING
module top(input logic clk_a, clk_b, output logic q, spare);
 always_ff @(posedge clk_a) q <= ~q;
 assign spare = clk_b;
endmodule
