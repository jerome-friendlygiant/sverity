// sverity-args: --top top --sdc @DIR@/core_input_delay.sdc
// Boundary + mutation: declared clock + input delay produces no CST-011; switching to core_only.sdc triggers CST-011.
// expect-none: CST-011
module top(input logic clk, din, output logic q);
  always_ff @(posedge clk) q <= din;
endmodule
