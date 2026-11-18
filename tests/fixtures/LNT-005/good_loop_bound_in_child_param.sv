// sverity-args: --top top
// P1 inside a parameterised child: the bound is a localparam int unsigned derived from the instance parameter W.
// The child is elaborated with W = 4, so the loop tests i < 4 and the comparison is proven value-preserving.
// expect: LNT-005 INFO 11
// expect-none: LNT-005 WARNING
module leaf #(parameter int unsigned W = 4) (input logic [W-1:0] d, output logic [W-1:0] q);
  localparam int unsigned LIMIT = W;
  if (W > 0) begin : g_on
    always_comb begin
      q = '0;
      for (int i = 0; i < LIMIT; i++) q[i] = d[i];
    end
  end
endmodule
module top(input logic [3:0] d, output logic [3:0] q);
  leaf #(.W(4)) u_leaf (.d(d), .q(q));
endmodule
