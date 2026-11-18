// sverity-args: --top top
// expect-count: LNT-057 WARNING 1
module top #(parameter W=4)(input logic [7:0] d, output logic [3:0] a,b);
  assign a={W{d[1:0]}};
  assign b=d << W;
endmodule
