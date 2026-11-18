// sverity-args: --top top
// Lookalike of the generate chain: the arm that is constant per iteration never selects 1'b0, so every iteration reads b[(i+1)%4] and the bits form a ring.
// expect: LNT-012 ERROR
module top(input logic [3:0] g, output logic [3:0] b);
  for (genvar i = 0; i < 4; i++) begin : gg
    assign b[i] = g[i] ^ ((i == 9) ? 1'b0 : b[(i+1)%4]);
  end
endmodule
