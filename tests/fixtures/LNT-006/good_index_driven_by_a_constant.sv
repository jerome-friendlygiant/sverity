// sverity-args: --top top
// The index net has one driver, a constant: it is always 0, inside the one-word array.
// expect-none: LNT-006
module top(input logic [1:0] d, output logic y);
  logic [0:0] idx;
  assign idx = '0;
  logic [0:0] words [1];
  assign words[0] = d[0];
  assign y = words[idx];
endmodule
