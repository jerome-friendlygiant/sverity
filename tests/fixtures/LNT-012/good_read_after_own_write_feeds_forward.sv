// sverity-args: --top top
// Same shape, but `grant` does not come back to `eligible`: the read of `pick` after its own write is one activation's order.
// expect-none: LNT-012
module top(input logic req, stop, output logic grant);
  logic eligible, pick;
  assign eligible = req & ~stop;
  always_comb begin
    pick = 1'b0;
    pick = eligible | pick;
  end
  assign grant = pick;
endmodule
