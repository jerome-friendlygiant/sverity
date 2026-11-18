// sverity-args: --top top
// Lookalike of the zero-extended nets: one operand net takes the full 8 bits, so the sum can exceed 255.
// expect: LNT-036 WARNING 8
module top(input logic [3:0] x, input logic [7:0] y, output logic gt);
  logic [7:0] xe, ye;
  assign xe = {4'b0, x};
  assign ye = y;
  assign gt = (xe + ye) > 8'd20;
endmodule
