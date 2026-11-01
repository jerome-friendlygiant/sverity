// sverity-args: --top top
// expect: LNT-056 WARNING 6
// expect: LNT-056 WARNING 7
// expect-count: LNT-056 WARNING 2
module top(output logic [3:0] lo, output logic [7:0] xs);
  assign lo = {4'h1, 2'b0};         // 6 bits into a 4-bit target
  assign xs = {4'h1, 2'b0};         // 6 bits into an 8-bit target
endmodule
