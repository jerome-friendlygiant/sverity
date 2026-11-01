// sverity-args: --top top
// expect: LNT-057 WARNING 5
// expect-count: LNT-057 WARNING 1
module top #(parameter W = 4) (input logic [7:0] a, output logic [W-1:0] o);
  assign o = a;                     // 8 bits into a W-bit target
endmodule
