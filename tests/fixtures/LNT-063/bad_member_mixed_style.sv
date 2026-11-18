// expect: LNT-063 WARNING 4
module top(input logic clk, d, output logic q);
  struct packed {logic a;} state;
  initial state.a = 0;
  always @(posedge clk) state.a <= d;
  assign q = state.a;
endmodule
