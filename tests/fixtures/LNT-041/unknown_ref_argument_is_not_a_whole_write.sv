// sverity-args: --top top
// The only write of `mem` is through a 2-bit run-time index, which reaches 4 of its 8 elements. The `ref` argument of the call is listed as
// a driver of the whole array, but the function only reads it: it cannot be taken to close the gap, nor to leave it, so which elements are
// driven stays Unresolved.
// expect-unresolved: LNT-041 7
module top(input logic clk, input logic [1:0] a, input logic [7:0] d, output logic [7:0] q);
  logic [7:0] mem [8];
  function automatic logic [7:0] rd(ref logic [7:0] m [8], input logic [2:0] x); return m[x]; endfunction
  always_ff @(posedge clk) mem[a] <= d;
  always_ff @(posedge clk) q <= rd(mem, 3'd7);
endmodule
