// sverity-args: --top top
// expect: LNT-080 WARNING
module top(input logic clk, output logic q);
  int data[];
  always_ff @(posedge clk) q <= 1'b0;
endmodule
