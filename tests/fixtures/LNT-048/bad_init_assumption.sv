// sverity-args: --top top
// expect: LNT-048 WARNING
module top(input logic clk, d, output logic q);
  logic r = 1'b0;
  always_ff @(posedge clk) r <= d;
  assign q = r;
endmodule
