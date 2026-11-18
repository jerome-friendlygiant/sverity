// sverity-args: --top top
// The break ends the loop inside the function only: every bit of v is written by the caller's loop.
// expect-none: LNT-008
module top(input logic c, input logic [7:0] x, output logic [7:0] y);
  logic [7:0] v;
  function automatic logic ffs0(input logic [7:0] u);
    ffs0 = 1'b0;
    for (int k = 0; k < 8; k++) if (u[k]) begin ffs0 = k[0]; break; end
  endfunction
  always_comb for (int i = 0; i < 8; i++) v[i] = c ^ ffs0(x);
  assign y = v;
endmodule
