// Copies the low k_in bits of a bus into the result.
// Interface:
//   a      input bus of W bits
//   k_in   number of low bits forwarded from a to y
//   y      result bus of W bits
//   nonzero, par  reduction flags over y
// Used to build narrow views of a wide status bus.
module top #(
  parameter int W = 16
) (
  input  logic [W-1:0] a,
  input  logic [4:0]   k_in,
  output logic [W-1:0] y,
  output logic         nonzero,
  output logic         par
);
  always_comb begin
    for (int i = 0; i < W; i++)
      if (i < int'(k_in)) y[i] = a[i];
  end

  // summary flags over the forwarded bits
  assign nonzero = |y;
  assign par     = ^y;
endmodule
