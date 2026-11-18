// sverity-args: --top top
// The carry is kept: a 9-bit operand (or a 9-bit result) widens the sum. A comparison with a constant operand is modular by intent.
// expect-none: LNT-036 WARNING
module top(input logic [7:0] a, b, c, output logic [7:0] y, output logic [8:0] w, output logic z);
  assign y = ({1'b0, a} + b) >> 1;
  assign w = (a + b) >> 1;
  assign z = (a + 8'd1) > c;
endmodule
