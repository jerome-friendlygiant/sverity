// sverity-args: --top top
// Two procedures share a module integer counter and call a function in their loops. Each sets the counter in its loop header
// before it reads it, so the counter carries nothing between them: no combinational loop.
// expect-none: LNT-012
module top(input logic [7:0] a, output logic [7:0] p, q);
  integer n;
  function logic flip(input logic x); flip = ~x; endfunction
  always @* for (n = 0; n < 8; n = n + 1) p[n] = flip(a[n]);
  always @* for (n = 0; n < 8; n = n + 1) q[n] = flip(a[n]) ^ a[0];
endmodule
