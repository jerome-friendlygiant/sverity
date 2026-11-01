// sverity-args: --top top
// Body W=8 is shared by u0 and u1, body W=4 is u2; the product is truncated in both. Grouped count = instances.
// expect-count: LNT-036 WARNING 3
module child #(parameter W = 8) (input logic [W-1:0] a, b, output logic [W-1:0] p);
  assign p = a * b;
endmodule
module top(input logic [7:0] a, b, input logic [3:0] c, d, output logic [7:0] p1, p2, output logic [3:0] p3);
  child u0(.a(a), .b(b), .p(p1));
  child u1(.a(a), .b(b), .p(p2));
  child #(.W(4)) u2(.a(c), .b(d), .p(p3));
endmodule
