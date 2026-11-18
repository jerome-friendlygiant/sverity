// sverity-args: --top top
// The complete sum interval 0..510 fits the nine-bit operands' result.
// expect-none: LNT-036
module top(input logic [7:0] a, b, output logic [9:0] y);
  assign y = {1'b0, ({1'b0, a} + {1'b0, b})};
endmodule
