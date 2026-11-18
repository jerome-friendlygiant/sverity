// sverity-args: --top top
// The width is `bits_for(N)`, a constant user function of the parameter N (reads only its argument and `$clog2`): it is evaluated for each
// specialisation like `$clog2(N)`, not undecided. Both specialisations (N = 8 gives 3 bits, N = 4 gives 2) truncate the 8-bit value.
// expect: LNT-057 WARNING 12
// expect: LNT-057 WARNING 12
package util_pkg;
  function automatic integer bits_for(integer value);
    return (value == 1) ? 1 : $clog2(value);
  endfunction
endpackage
module counter #(parameter int N = 8) (input logic [7:0] x, output logic [util_pkg::bits_for(N)-1:0] idx);
  assign idx = x;
endmodule
module top(input logic [7:0] x, output logic [2:0] a, output logic [1:0] b);
  counter #(.N(8)) wide(.x, .idx(a));
  counter #(.N(4)) narrow(.x, .idx(b));
endmodule
