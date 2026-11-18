// sverity-args: --top top
// Two combinational processes write the same word of an array: a fully assigned table is wiring, but two drivers of one word are not.
// expect: LNT-049 WARNING 5
module top(input logic [7:0] a, b, input logic [1:0] ra, output logic [7:0] y);
  logic [7:0] m [4];
  always_comb begin m[0] = a; m[1] = a; m[2] = a; m[3] = a; end
  always_comb m[2] = b;
  assign y = m[ra];
endmodule
