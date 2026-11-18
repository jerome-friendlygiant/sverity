// sverity-args: --top top
// expect-count: LNT-057 WARNING 1
module top #(parameter W=8)(input logic [15:0] d, output logic [3:0] a,b);
  assign a=8'(d);
  assign b=W'(d);
endmodule
