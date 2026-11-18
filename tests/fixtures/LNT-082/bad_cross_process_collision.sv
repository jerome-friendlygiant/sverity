// Two processes write the memory at run-time addresses (two write ports) and a third reads it: a read can meet either write.
// expect: LNT-082 WARNING
module top(input logic clk, input logic [1:0] wa, input logic [1:0] wb, input logic [1:0] ra,
           input logic [7:0] d, input logic [7:0] e, output logic [7:0] q);
  logic [7:0] mem [4];
  always_ff @(posedge clk) mem[wa] <= d;
  always_ff @(posedge clk) mem[wb] <= e;
  always_ff @(posedge clk) q <= mem[ra];
endmodule
