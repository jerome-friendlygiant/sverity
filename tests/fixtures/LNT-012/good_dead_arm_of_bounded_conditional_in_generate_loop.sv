// sverity-args: --top top
// The gray-to-binary chain in its generate spelling: b[i+1] is read only where i < 3, and the dead arm's out-of-range select is not read.
// expect-none: LNT-012
module top(input logic [3:0] g, output logic [3:0] b);
  for (genvar i = 0; i < 4; i++) begin : gg
    assign b[i] = g[i] ^ ((i < 3) ? b[i+1] : 1'b0);
  end
endmodule
