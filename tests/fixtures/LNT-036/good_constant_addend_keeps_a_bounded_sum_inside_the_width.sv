// sverity-args: --top top
// 127 + 127 + 1 = 255 fits in 8 bits: the whole chain's range proves no carry is lost.
// expect-none: LNT-036
module top(input logic [6:0] x, y, output logic gt);
  logic [7:0] xe, ye;
  assign xe = {1'b0, x};
  assign ye = {1'b0, y};
  assign gt = (xe + ye + 8'd1) > 8'd20;
endmodule
