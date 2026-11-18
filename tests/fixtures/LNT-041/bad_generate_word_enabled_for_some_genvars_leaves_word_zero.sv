// sverity-args: --top top
// Word 0 is never written: the generate write is live for i = 1 to 3 only.
// expect: LNT-041 WARNING 5
module top(input logic clk, input logic [7:0] a, output logic [7:0] y);
  logic [7:0] m [4];
  for (genvar i = 0; i < 4; i++) begin : g
    always_ff @(posedge clk) if (i != 0) m[i] <= a;
  end
  assign y = m[0] ^ m[1] ^ m[2] ^ m[3];
endmodule
