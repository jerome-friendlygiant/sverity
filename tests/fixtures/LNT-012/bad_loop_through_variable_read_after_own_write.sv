// sverity-args: --top top
// `pick` is read after the procedure's own write, which is not feedback; but `eligible` decides the write, the write drives `grant`,
// and `grant` feeds `eligible` back through a continuous assignment: the loop through the other processes is real.
// expect: LNT-012 ERROR
module top(input logic req, output logic grant);
  logic eligible, pick;
  assign eligible = req & ~grant;
  always_comb begin
    pick = 1'b0;
    pick = eligible | pick;
  end
  assign grant = pick;
endmodule
