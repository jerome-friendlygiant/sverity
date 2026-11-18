// sverity-args: --top top
// The divisor is a function result that reads the free input d: the function body is followed and nothing keeps it away from zero.
// expect-unresolved: LNT-037 8
module top(input logic [7:0] a, d, output logic [7:0] y);
  function automatic logic [7:0] den();
    return d;
  endfunction
  assign y = a / den();
endmodule
