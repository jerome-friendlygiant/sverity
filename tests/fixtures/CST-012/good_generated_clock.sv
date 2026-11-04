// sverity-args: --top top --sdc @DIR@/generated.sdc
// expect: CST-012 INFO
// expect-none: CST-012 WARNING
// expect-none: CST-012 ERROR
module top(input logic clk_a, clk_b, output logic q);
 always_ff @(posedge clk_b) q <= ~q;
endmodule
