// sverity-args: --top top
// Each generate iteration is its own continuous assign: at i == 3 the ?: takes 1'b0 and the wrapped read b[0] is dead, so the bits form a chain, not a ring.
// expect-none: LNT-012
module top(input logic [3:0] g, output logic [3:0] b);
  for (genvar i = 0; i < 4; i++) begin : gg
    assign b[i] = g[i] ^ ((i == 3) ? 1'b0 : b[(i+1)%4]);
  end
endmodule
