// sverity-args: --top top
// The second write reads the first one's value instead of replacing it, so the entry value of a still reaches the output: a loop.
// expect: LNT-012 ERROR
module top(input logic [3:0] x, m, output logic [3:0] a);
  always_comb begin a = a ^ x; a = a & m; end
endmodule
