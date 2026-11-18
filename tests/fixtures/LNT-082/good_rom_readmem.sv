// sverity-args: --top top
// A table filled at time zero by $readmemh and only read: no writer at run time, so no collision obligation.
// expect-none: LNT-082
module top(input logic clk, input logic [3:0] a, output logic [7:0] q);
  logic [7:0] rom [16];
  initial $readmemh("rom.hex", rom);
  always_ff @(posedge clk) q <= rom[a];
endmodule
