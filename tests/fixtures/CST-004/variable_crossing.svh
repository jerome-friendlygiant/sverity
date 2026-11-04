module top(input logic clka, clkb, d, output logic q);
  logic launch;
  always_ff @(posedge clka) launch <= d;
  always_ff @(posedge clkb) q <= launch;
endmodule
