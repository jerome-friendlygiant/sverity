// sverity-args: --top top
// A module-level counter that every procedure sets in its loop header is the loop's own: the loop writes every bit of y, also with
// a call in the body that does not touch the counter.
// expect-none: LNT-024
module top(input logic [7:0] a, input logic b, output logic [7:0] y);
  integer k;
  function logic gate(input logic x, input logic en); gate = x & en; endfunction
  always @* for (k = 0; k < 8; k = k + 1) y[k] = gate(a[k], b);
endmodule
