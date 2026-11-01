// sverity-args: --top top
// expect: LNT-056 WARNING 5
// expect-count: LNT-056 WARNING 1
module top(output logic [7:0] w);
  assign w = {4'hFF, 4'h1};         // 4'hFF needs 8 bits inside the concatenation
endmodule
