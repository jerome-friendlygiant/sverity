// expect: LNT-080 WARNING 3
module top(input logic clk, output logic q);
  struct {event changed;} state;
  always @(posedge clk) q <= 0;
endmodule
