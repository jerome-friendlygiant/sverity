// expect: LNT-048 WARNING 3
module top(input logic sel, output logic q);
  logic mem[2] = '{0,1};
  assign q = mem[sel];
endmodule
