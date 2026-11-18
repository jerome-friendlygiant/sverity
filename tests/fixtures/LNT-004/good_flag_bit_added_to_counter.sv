// sverity-args: --top top
// A single unsigned bit added to a wider counter zero-extends losslessly: a flag or an increment, INFO.
// expect: LNT-004 INFO 6
// expect-none: LNT-004 WARNING
module top(input logic clk, en, output logic [5:0] acc);
  always_ff @(posedge clk) acc <= acc + en;
endmodule
