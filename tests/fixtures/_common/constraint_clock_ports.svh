// Shared design objects for clock-declaration constraint fixtures.
module top(input logic clk, other, div, leaf, d, output logic q);
  always_ff @(posedge div) q <= d;
endmodule
