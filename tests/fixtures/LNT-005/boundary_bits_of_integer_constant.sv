// sverity-args: --top top
// $bits of an int-typed parameter is 32 whatever the parameter holds: slang's warning is that the width of the type, not of the value, is
// compared. Simulation and synthesis agree, so it is advisory.
// expect: SLANG-bits-of-integer-constant INFO 7
module top #(parameter int W = 4)(input logic [W-1:0] a, output logic y);
  localparam int Wide = 7;
  assign y = a[0] ^ ($bits(Wide) == $bits(W));
endmodule
