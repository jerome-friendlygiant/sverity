// sverity-args: --top top
// expect: LNT-035 WARNING 6
// expect: LNT-035 WARNING 7
// expect-count: LNT-035 WARNING 2
module top(input logic [5:0] n, output logic [63:0] w, output logic [63:0] w2);
  assign w = {1 << n};              // unsized 1 is 32 bits inside the concatenation
  assign w2 = {1 << n, 8'h00};      // 40-bit concatenation with a 32-bit shift
endmodule
