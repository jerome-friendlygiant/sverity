// sverity-args: --top top
// A cycle made only of block-local variables, closed through hierarchical reads: block locals are not exempt from a real loop.
// expect: LNT-012 ERROR
module top(input logic a, output logic y);
  always_comb begin : b1
    logic t;
    t = ~b2.u ^ a;
  end
  always_comb begin : b2
    logic u;
    u = b1.t;
  end
  assign y = b1.t;
endmodule
