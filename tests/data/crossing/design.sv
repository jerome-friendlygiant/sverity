module top(input logic clka, clkb, value, output logic result);
  logic src;
  `include "launch.svh"
  always_ff @(posedge clkb) result <= src;
endmodule
