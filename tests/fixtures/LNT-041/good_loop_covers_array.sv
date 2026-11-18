// sverity-args: --top top
// A constant loop writes every element of the array.
// expect-none: LNT-041
module top(input logic c, output logic y);
  logic a [4];
  always_comb begin
    for (int k = 0; k < 4; k++) a[k] = c;
  end
  assign y = a[1];
endmodule
