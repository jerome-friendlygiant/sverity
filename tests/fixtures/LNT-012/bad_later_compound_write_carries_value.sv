// sverity-args: --top top
// The compound write keeps every bit in place, so the entry value of x still reaches the output through x ^ a and then += b.
// expect: LNT-012 ERROR
module top(input logic [3:0] a, b, output logic [3:0] y);
  logic [3:0] x;
  always_comb begin x = x ^ a; x += b; end
  assign y = x;
endmodule
