// sverity-args: --top top
// expect: LNT-055 WARNING 5
// expect-count: LNT-055 WARNING 1
module top(input logic [7:0] a, b, output logic y);
  assign y = (a - b) <= (0 - 1);
endmodule
