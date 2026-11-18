// The only writer is a function body called from a clocked process.
// sverity-args: --top top
// expect: LNT-082 WARNING 8
module top(input logic clk, input logic [1:0] wa, ra, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] mem [4];
  function automatic void put(logic [1:0] a, logic [7:0] v); mem[a] = v; endfunction
  always_ff @(posedge clk) put(wa, d);
  always_ff @(posedge clk) q <= mem[ra];
endmodule
