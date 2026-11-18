// sverity-args: --top top
// A reset loop writes every word of ram, another process writes the word at ad: every address collides with the loop's write.
// expect: LNT-009 ERROR 8
module top(input wire clk, rst, we, input wire [3:0] ad, input wire [7:0] d, output wire [7:0] q);
  reg [7:0] ram [0:15];
  integer k;
  always @(posedge clk) if (rst) for (k = 0; k < 16; k = k + 1) ram[k] <= 8'd0;
  always @(posedge clk) if (we) ram[ad] <= d;
  assign q = ram[0];
endmodule
