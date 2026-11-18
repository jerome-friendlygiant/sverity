// expect: LNT-015 WARNING 4
module top(input logic clk, d, output logic q);
  struct packed {logic a;} state;
  always @(posedge clk) state.a = d;
  assign q = state.a;
endmodule
