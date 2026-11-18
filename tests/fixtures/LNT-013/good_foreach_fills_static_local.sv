// sverity-args: --top top
// The foreach writes every element of the static local before the return reads it; the array named in the header is not a read.
// expect-none: LNT-013
module top(input logic [7:0] a, output logic [7:0] y);
  function logic [7:0] flip(input logic [7:0] v);
    logic [7:0] r;
    foreach (r[k]) r[k] = ~v[k];
    return r;
  endfunction
  assign y = flip(a);
endmodule
