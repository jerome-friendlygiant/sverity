// sverity-args: --top top
// Both operands are nets assigned `{4'b0, x}`, so each is at most 15 and the sum at most 30: no carry out of 8 bits to lose.
// expect-none: LNT-036
module top(input logic [3:0] x, y, output logic gt);
  logic [7:0] xe, ye;
  assign xe = {4'b0, x};
  assign ye = {4'b0, y};
  assign gt = (xe + ye) > 8'd20;
endmodule
