// sverity-args: --top top
// The bound is an unsigned input, not a constant: nothing bounds the values of i from above.
// expect: LNT-005 WARNING 7
module top(input logic [7:0] d, input logic [3:0] lim, output logic [3:0] n);
  always_comb begin
    n = '0;
    for (int i = 0; i < lim; i++) n += d[i];
  end
endmodule
