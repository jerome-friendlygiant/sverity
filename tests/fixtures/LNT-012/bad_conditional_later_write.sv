// sverity-args: --top top
// When c is low the first write is the final one, so a depends on itself on that path.
// expect: LNT-012 ERROR
module top(input logic c, input logic [3:0] x, b, output logic [3:0] a);
  always_comb begin a = a ^ x; if (c) a = b; end
endmodule
