// sverity-args: --top top
// expect: LNT-057 WARNING 8
// expect-count: LNT-057 WARNING 1
module child #(parameter W = 8) (input logic [W-1:0] din, output logic [W-1:0] dout);
  assign dout = din;
endmodule
module top(input logic [7:0] a, output logic [3:0] q);
  child #(.W(4)) u4(.din(a), .dout(q));
endmodule
