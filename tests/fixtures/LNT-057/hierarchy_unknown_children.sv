// sverity-args: --top top
// The W=8 body matches its actual; the W=4 body truncates. Grouped count = instances of the truncating body.
// expect-count: LNT-057 WARNING 1
module child #(parameter W = 8) (input logic [W-1:0] din, output logic [W-1:0] dout);
  assign dout = din;
endmodule
module top(input logic [7:0] a, b, output logic [7:0] q1, q2, output logic [3:0] q3);
  child u0(.din(a), .dout(q1));
  child u1(.din(a), .dout(q2));
  child #(.W(4)) u2(.din(a), .dout(q3));
endmodule
