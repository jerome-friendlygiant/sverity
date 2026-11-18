// sverity-args: --top top
// expect-count: LNT-057 WARNING 1
module top #(parameter W=4)(input logic [7:0] a, output logic [W-1:0] x[0:1], output logic [3:0] y[0:1]);
  assign x='{a, 4'b0};
  assign y='{a, 4'b0};
endmodule
