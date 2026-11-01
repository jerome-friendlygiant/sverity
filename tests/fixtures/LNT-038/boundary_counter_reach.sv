// sverity-args: --top top
// expect: LNT-038 WARNING 10
// expect: LNT-038 WARNING 11
// expect-count: LNT-038 WARNING 2
module top(input logic clk, output logic a, b, c, d);
  logic [7:0] cnt;
  always_ff @(posedge clk) cnt <= cnt + 1;
  assign a = (cnt > 254);           // 255 is reachable: no finding
  assign b = (cnt == 255);          // reachable: no finding
  assign c = (cnt == 256);          // one past the maximum: never true
  assign d = (cnt > 255);           // never true
endmodule
