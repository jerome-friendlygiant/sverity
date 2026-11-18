// The read of the memory sits in a function body, not in the process that calls it; two processes write the memory.
// sverity-args: --top top
// expect: LNT-082 WARNING 6
module top(input logic clk, input logic [1:0] wa, wb, ra, input logic [7:0] d, e, output logic [7:0] q);
  logic [7:0] mem [4];
  function automatic logic [7:0] get(logic [1:0] a); return mem[a]; endfunction
  always_ff @(posedge clk) mem[wa] <= d;
  always_ff @(posedge clk) mem[wb] <= e;
  always_ff @(posedge clk) q <= get(ra);
endmodule
