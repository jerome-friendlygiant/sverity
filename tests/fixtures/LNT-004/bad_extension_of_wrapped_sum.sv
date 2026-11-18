// sverity-args: --top top
// The sum in the concatenation is evaluated in 8 bits and may wrap before the extension carries it on.
// expect: LNT-004 WARNING 5
module top(input logic [7:0] a, b, output logic [15:0] y);
  assign y = {a + b};
endmodule
