// Orders two readings and reports the spread between them.
// Interface:
//   a, b   unsigned 8-bit readings
//   lo, hi smaller and larger reading
//   span   hi - lo
//   equal  readings are identical
// Purely combinational.
module top (
  input  logic [7:0] a,
  input  logic [7:0] b,
  output logic [7:0] lo,
  output logic [7:0] hi,
  output logic [7:0] span,
  output logic       equal
);
  logic gt;

  always_comb begin
    gt    = (a > b);
    equal = (a == b);
    lo    = gt ? b : a;
    hi    = gt ? a : b;
    span  = hi - lo;
  end
endmodule
