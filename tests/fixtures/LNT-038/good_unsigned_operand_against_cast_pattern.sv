// sverity-args: --top top
// 2'(i) is signed and negative for i = 2 and 3, but no wider than the operand: unsigned, it is the bit pattern 2 or 3, which the
// 2-bit operand can take.
// expect-none: LNT-038
module top(input logic [1:0] d, output logic [3:0] y);
  for (genvar i = 0; i < 4; i++) begin : g
    assign y[i] = (d == 2'(i));
  end
endmodule
