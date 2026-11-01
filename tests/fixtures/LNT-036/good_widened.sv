// sverity-args: --top top
// expect-none: LNT-036
module top(input logic [7:0] a, b, output logic [7:0] s, output logic [15:0] p, output logic [8:0] c);
  assign p = a * b;                 // context is 16 bits: full product
  assign c = {1'b0, a} + {1'b0, b}; // widened operands keep the carry
  assign s = a + b;                 // modular add is not reported by default
endmodule
