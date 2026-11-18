// sverity-args: --top top
// expect-none: LNT-022
module top(input logic [1:0] clocks, input logic d, output logic q);
  always @(posedge clocks[0]) q <= d;
  always @(negedge clocks[1]) q <= d;
endmodule
