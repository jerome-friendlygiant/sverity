// sverity-args: --top top
// expect: LNT-038 WARNING 7
// expect-count: LNT-038 WARNING 1
module top(input logic clk, output logic hit);
  logic [7:0] cnt;
  always_ff @(posedge clk) cnt <= cnt + 1;
  assign hit = (cnt == 300);        // an 8-bit counter never reaches 300
endmodule
