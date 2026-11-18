// sverity-args: --top top
// Body W=4 is shared by u0 and u1, body W=8 is u2: one grouped record with count = instances (3).
// expect: LNT-055 WARNING 6
// expect-count: LNT-055 WARNING 3
module child #(parameter W = 4) (input logic [W-1:0] a, b, output logic y);
  assign y = (a - b) < 0;
endmodule
module top(input logic [3:0] a4, b4, input logic [7:0] a8, b8, output logic y0, y1, y2);
  child u0(.a(a4), .b(b4), .y(y0));
  child u1(.a(a4), .b(b4), .y(y1));
  child #(.W(8)) u2(.a(a8), .b(b8), .y(y2));
endmodule
