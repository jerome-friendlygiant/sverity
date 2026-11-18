// sverity-args: --top top
// $readmemh in an always block runs while the memory is read: not a time-zero fill.
// expect: LNT-082 WARNING 7
module top(input logic clk, ld, input logic [3:0] a, output logic [7:0] q);
  logic [7:0] mem [16];
  always @(posedge ld) $readmemh("mem.hex", mem);
  always_ff @(posedge clk) q <= mem[a];
endmodule
