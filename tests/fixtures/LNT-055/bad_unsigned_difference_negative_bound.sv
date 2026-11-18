// sverity-args: --top top
// Both negative-one spellings become the maximum unsigned comparison value.
// expect: LNT-055 WARNING 7
// expect: LNT-055 WARNING 8
// expect-count: LNT-055 WARNING 2
module top(input logic [7:0] a, b, output logic x, y);
  assign x = (a - b) <= 32'shffffffff;
  assign y = (a - b) <= -1;
endmodule
