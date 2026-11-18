// sverity-args: --top top
// expect: LNT-040 WARNING 8
// expect: LNT-040 WARNING 9
// expect-count: LNT-040 WARNING 2
module top(input bit index, input logic data, output logic a, b, safe, definite);
  localparam logic [1:0] TABLE = 2'bz0;
  localparam logic [1:0] DEFINED = 2'b10;
  always_comb if (TABLE[index]) a = data; else a = 0;
  assign b = TABLE[index] ? data : 1'b0;
  assign safe = DEFINED[index] ? data : 1'b0;
  assign definite = 2'bx1 ? data : 1'b0;
endmodule
