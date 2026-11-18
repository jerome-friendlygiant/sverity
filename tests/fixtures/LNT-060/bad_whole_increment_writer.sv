// sverity-args: --top top
// expect: LNT-060 WARNING
// expect-confidence: LNT-060 established
module top(input logic clk, output logic [1:0] y);
  logic [1:0] q;
  always @(posedge clk) q++;
  assign y = q;
endmodule
