// sverity-args: --top top
// x++ reads x and writes it back: with nothing assigning x first, the block is a combinational loop.
// expect: LNT-012 ERROR
module top(output logic [3:0] y);
  logic [3:0] x;
  always @* x++;
  assign y = x;
endmodule
