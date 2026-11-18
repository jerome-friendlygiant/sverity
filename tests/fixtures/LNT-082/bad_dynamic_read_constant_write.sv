// sverity-args: --top top
// A dynamic read address may equal the constant write address of a memory that another process also writes at a run-time address.
// expect: LNT-082 WARNING 8
module top(input logic clk, input logic [1:0] wb, ra, input logic [7:0] d, e, output logic [7:0] q);
  logic [7:0] mem [4];
  always_ff @(posedge clk) mem[0] <= d;
  always_ff @(posedge clk) mem[wb] <= e;
  always_ff @(posedge clk) q <= mem[ra];
endmodule
