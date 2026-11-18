// sverity-args: --top top
// The reset loop writes words 0 to 3 of the memory and the other process word 7: no word has two writers.
// expect-none: LNT-009
module top(input wire clk, rst, we, input wire [7:0] d, output wire [7:0] q);
  reg [7:0] ram [0:7];
  integer k;
  always @(posedge clk) if (rst) for (k = 0; k < 4; k = k + 1) ram[k] <= 8'd0;
  always @(posedge clk) if (we) ram[7] <= d;
  assign q = ram[0];
endmodule
