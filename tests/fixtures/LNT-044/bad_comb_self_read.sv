// sverity-args: --top top
// x = x ^ a reads x before its own assignment, and x is assigned again later in the block
// expect: LNT-044 WARNING 7
module top(input logic a, output logic y);
  logic x;
  always_comb begin
    x = x ^ a;
    x = a;
    y = x;
  end
endmodule
