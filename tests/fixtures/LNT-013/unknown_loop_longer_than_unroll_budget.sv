// sverity-args: --top top
// The loop writes every bit, but it is longer than the flow analysis unrolls (8192 steps): the iterations after the budget are
// not walked, so the bits they write cannot be called unassigned. The result is Unresolved, not a latch.
// expect: LNT-013 WARNING 6
module top(input logic c, output logic [8999:0] y);
  always @* begin
    for (int i = 0; i < 9000; i++) y[i] = c;
  end
endmodule
