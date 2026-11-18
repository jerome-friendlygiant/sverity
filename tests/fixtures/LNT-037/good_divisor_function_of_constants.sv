// sverity-args: --top top
// Lookalike of the divisor read through a function: the function body reads no design state, its result is a constant.
// expect-none: LNT-037
module top(input logic [7:0] a, output logic [7:0] y);
  function automatic logic [7:0] den();
    return 8'd3;
  endfunction
  assign y = a / den();
endmodule
