// sverity-args: --top top
// The width-16 body is reported once; the width-8 body is clean. expect-count checks the shared body is counted once.
// expect-count: LNT-034 WARNING 1
module child #(parameter W = 8) (input logic c, input logic [W-1:0] a, input logic [7:0] b, output logic [7:0] o);
  assign o = c ? a : b;
endmodule
module top(input logic c, input logic [7:0] a, input logic [15:0] w, output logic [7:0] o1, o2, o3);
  child u0(.c(c), .a(a), .b(a), .o(o1));
  child u1(.c(c), .a(a), .b(a), .o(o2));
  child #(.W(16)) u2(.c(c), .a(w), .b(a), .o(o3));
endmodule
