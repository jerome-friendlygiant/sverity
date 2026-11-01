// sverity-args: --top top
// Body W=8 is shared by u0 and u1, body W=4 is u2: one grouped record with count = instances.
// expect-count: LNT-035 INFO 3
module child #(parameter W = 8) (input logic signed [W-1:0] s, input logic [5:0] n, output logic [63:0] w);
  assign w = s << n;
endmodule
module top(input logic [5:0] n, input logic signed [7:0] s, input logic signed [3:0] t, output logic [63:0] o1, o2, o3);
  child u0(.s(s), .n(n), .w(o1));
  child u1(.s(s), .n(n), .w(o2));
  child #(.W(4)) u2(.s(t), .n(n), .w(o3));
endmodule
