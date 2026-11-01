// sverity-args: --top top
// expect: LNT-056 WARNING 5
// expect-count: LNT-056 WARNING 1
module top(output logic [4:0] r, output logic [5:0] ok);
  assign r = {3{2'b10}};            // 6 bits into 5
  assign ok = {3{2'b10}};           // 6 bits into 6: no finding
endmodule
