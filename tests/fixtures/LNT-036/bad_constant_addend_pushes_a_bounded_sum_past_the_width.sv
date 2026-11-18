// sverity-args: --top top
// Two 7-bit values zero-extended to 8 bits sum to at most 254; the addend 2 makes 256, which does not fit.
// expect: LNT-036 WARNING 8
module top(input logic [6:0] x, y, output logic gt);
  logic [7:0] xe, ye;
  assign xe = {1'b0, x};
  assign ye = {1'b0, y};
  assign gt = (xe + ye + 8'd2) > 8'd20;
endmodule
