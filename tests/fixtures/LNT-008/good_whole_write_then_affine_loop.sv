// sverity-args: --top top
// A whole write, then a constant loop writing v[k*2] and v[k*2+1] inside a generate loop: every index is known.
// expect-none: LNT-008
module top #(parameter int unsigned W = 8) (input logic [W-1:0] d, output logic [W-1:0] y);
  for (genvar r = 0; r < 2; r++) begin : g_round
    logic [W-1:0] v;
    always_comb begin
      v = d;
      for (int k = 0; k < W/2; k++) begin
        v[k * 2]     = d[k];
        v[k * 2 + 1] = d[k + W/2];
      end
    end
    if (r == 1) begin : g_out
      assign y = v;
    end
  end
endmodule
